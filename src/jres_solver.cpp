/**
 * @file jres_solver.cpp
 * @brief JRES Solver Library
 */

// --- C++ Standard Libs ---
#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <memory>    // For std::unique_ptr
#include <vector>
#include <map>
#include <cmath>     // For std::ceil, std::floor
#include <chrono>    // For time parsing
#include <iomanip>   // For std::get_time, std::setw, std::setfill
#include <sstream>   // For std::stringstream
#include <algorithm> // For std::find_if
#include <ctime>     // For time_t, tm, timegm/_mkgmtime
#include <cstring>   // For strlen, strcpy

// --- Our Public API Header ---
#include "jres_solver.hpp" // Note: This is a C-compatible header

// --- 3rd Party Libs ---
#include "nlohmann/json.hpp"

// --- COIN-OR Cbc ---
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include "CoinPackedVector.hpp"
#include "CoinBuild.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Data Structures ---

struct TeamMember
{
    std::string name;
    bool isDriver = false;
    bool isSpotter = false;
    int preferredStints = 3;
    int minimumRestHours = 0;
};

struct RaceData
{
    double avgLapTimeInSeconds;
    double pitTimeInSeconds;
    double fuelTankSize;
    double fuelUsePerLap;
    double durationHours;
    std::string raceStartUTC;
    std::string firstStintDriver;
    std::vector<TeamMember> teamMembers;
    json availability;
};

// --- JSON Deserialization ---
void from_json(const json &j, TeamMember &member)
{
    j.at("name").get_to(member.name);
    member.isDriver = j.value("isDriver", false);
    member.isSpotter = j.value("isSpotter", false);
    member.preferredStints = j.value("preferredStints", 3);
    member.minimumRestHours = j.value("minimumRestHours", 0);
}

void from_json(const json &j, RaceData &data)
{
    j.at("avgLapTimeInSeconds").get_to(data.avgLapTimeInSeconds);
    j.at("pitTimeInSeconds").get_to(data.pitTimeInSeconds);
    j.at("fuelTankSize").get_to(data.fuelTankSize);
    j.at("fuelUsePerLap").get_to(data.fuelUsePerLap);
    j.at("durationHours").get_to(data.durationHours);
    j.at("raceStartUTC").get_to(data.raceStartUTC);
    j.at("teamMembers").get_to(data.teamMembers);
    j.at("availability").get_to(data.availability);
    data.firstStintDriver = j.value("firstStintDriver", "");
}

// --- JSON Serialization ---
void to_json(json &j, const TeamMember &member)
{
    j = json{
        {"name", member.name},
        {"isDriver", member.isDriver},
        {"isSpotter", member.isSpotter},
        {"preferredStints", member.preferredStints},
        {"minimumRestHours", member.minimumRestHours}};
}

void to_json(json &j, const RaceData &data)
{
    j = json{
        {"avgLapTimeInSeconds", data.avgLapTimeInSeconds},
        {"pitTimeInSeconds", data.pitTimeInSeconds},
        {"fuelTankSize", data.fuelTankSize},
        {"fuelUsePerLap", data.fuelUsePerLap},
        {"durationHours", data.durationHours},
        {"raceStartUTC", data.raceStartUTC},
        {"teamMembers", data.teamMembers},
        {"availability", data.availability},
        {"firstStintDriver", data.firstStintDriver}};
}


struct SolverContext
{
    bool quiet;
    int timeLimit;
    std::string spotterMode;
    bool allowNoSpotter;
    double optimalityGap;
    RaceData raceData;
};

struct ParticipantModel
{
    std::string prefix;
    std::map<std::pair<std::string, int>, int> workVars;
    std::map<std::pair<std::string, int>, int> switchVars;
    int maxWorkStintsVar = -1;
    int minWorkStintsVar = -1;

    ParticipantModel(std::string p) : prefix(p) {}
};

struct ScheduleEntry {
    int stint;
    std::string driver;
    std::string spotter;
};

namespace TimeHelpers
{
    std::time_t timegm_portable(std::tm *tm)
    {
#if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(tm);
#else
        return timegm(tm);
#endif
    }

    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string)
    {
        std::tm tm = {};
        std::stringstream ss(utc_string);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return std::chrono::system_clock::from_time_t(timegm_portable(&tm));
    }

    std::string timePointToKey(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:00:00.000Z");
        return ss.str();
    }
} // namespace TimeHelpers


/**
 * @brief Internal function to add a participant model.
 */
ParticipantModel add_participant_model(
    CbcModel &model,
    const std::vector<TeamMember> &participants,
    int totalStints,
    const std::string &prefix,
    const SolverContext &ctx,
    double stintWithPitSeconds,
    int stintLaps)
{
    ParticipantModel p_model(prefix);
    if (participants.empty())
    {
        return p_model;
    }

    auto raceStartUTC = TimeHelpers::stringToTimePoint(ctx.raceData.raceStartUTC);

    // Create Variables
    p_model.maxWorkStintsVar = model.solver()->getNumCols();
    model.solver()->addCol(CoinPackedVector(), 0.0, COIN_DBL_MAX, 0.0);
    model.solver()->setInteger(p_model.maxWorkStintsVar);
    model.solver()->setColName(p_model.maxWorkStintsVar, prefix + "MaxStints");

    p_model.minWorkStintsVar = model.solver()->getNumCols();
    model.solver()->addCol(CoinPackedVector(), 0.0, COIN_DBL_MAX, 0.0);
    model.solver()->setInteger(p_model.minWorkStintsVar);
    model.solver()->setColName(p_model.minWorkStintsVar, prefix + "MinStints");

    for (const auto &p : participants)
    {
        for (int s = 0; s < totalStints; ++s)
        {
            // Work Vars
            int workVarIdx = model.solver()->getNumCols();
            p_model.workVars[{p.name, s}] = workVarIdx;
            model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0);
            model.solver()->setInteger(workVarIdx);
            model.solver()->setColName(workVarIdx, prefix + "_" + p.name + "_s" + std::to_string(s));

            // Switch Vars
            if (s > 0)
            {
                int switchVarIdx = model.solver()->getNumCols();
                p_model.switchVars[{p.name, s}] = switchVarIdx;
                model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0);
                model.solver()->setInteger(switchVarIdx);
                model.solver()->setColName(switchVarIdx, prefix + "Switch_" + p.name + "_s" + std::to_string(s));
            }
        }
    }

    // Add Objective Function Components
    model.solver()->setObjCoeff(p_model.maxWorkStintsVar, 1000.0);
    model.solver()->setObjCoeff(p_model.minWorkStintsVar, -1000.0);

    for (const auto &pair : p_model.switchVars)
    {
        model.solver()->setObjCoeff(pair.second, 100.0);
    }

    for (int s = 0; s < totalStints; ++s)
    {
        auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
        std::string availabilityKey = TimeHelpers::timePointToKey(stintStart);

        for (const auto &p : participants)
        {
            if (ctx.raceData.availability.contains(p.name) &&
                ctx.raceData.availability[p.name].contains(availabilityKey) &&
                ctx.raceData.availability[p.name][availabilityKey] == "Preferred")
            {
                int workVarIdx = p_model.workVars.at({p.name, s});
                model.solver()->setObjCoeff(workVarIdx, -1.0);
            }
        }
    }

    // Add Constraints
    double totalLaps = totalStints * stintLaps;
    double equalShareLaps = totalLaps / participants.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    int minStintsPerParticipant = (stintLaps > 0) ? static_cast<int>(std::ceil(minLapsPerParticipant / stintLaps)) : 0;

    for (const auto &p : participants)
    {
        // Availability
        for (int s = 0; s < totalStints; ++s)
        {
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(stintWithPitSeconds));
            auto stintEndCheck = stintEnd - std::chrono::seconds(1);

            std::string startKey = TimeHelpers::timePointToKey(stintStart);
            std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);

            bool isAvailable = true;
            if (ctx.raceData.availability.contains(p.name))
            {
                auto p_avail = ctx.raceData.availability[p.name];
                if (p_avail.value(startKey, "Unavailable") == "Unavailable" ||
                    p_avail.value(endKey, "Unavailable") == "Unavailable")
                {
                    isAvailable = false;
                }
            } else {
                isAvailable = false;
            }

            if (!isAvailable)
            {
                int workVarIdx = p_model.workVars.at({p.name, s});
                model.solver()->setColBounds(workVarIdx, 0.0, 0.0);
            }
        }

        // Switch Constraints
        for (int s = 1; s < totalStints; ++s)
        {
            CoinPackedVector row;
            row.insert(p_model.switchVars.at({p.name, s}), 1.0);
            row.insert(p_model.workVars.at({p.name, s}), -1.0);
            row.insert(p_model.workVars.at({p.name, s - 1}), 1.0);
            model.solver()->addRow(row, 0.0, COIN_DBL_MAX);
        }

        // Max/Min Stint Count
        CoinPackedVector totalStintsRow;
        for (int s = 0; s < totalStints; ++s)
        {
            totalStintsRow.insert(p_model.workVars.at({p.name, s}), 1.0);
        }
        
        CoinPackedVector maxRow = totalStintsRow;
        maxRow.insert(p_model.maxWorkStintsVar, -1.0);
        model.solver()->addRow(maxRow, -COIN_DBL_MAX, 0.0);

        CoinPackedVector minRow = totalStintsRow;
        minRow.insert(p_model.minWorkStintsVar, -1.0);
        model.solver()->addRow(minRow, 0.0, COIN_DBL_MAX);

        // Fair Share
        if (prefix == "Drive")
        {
            model.solver()->addRow(totalStintsRow, minStintsPerParticipant, COIN_DBL_MAX);
        }

        // Max Consecutive
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < totalStints - maxConsecutive; ++s)
        {
            CoinPackedVector consecutiveRow;
            for (int i = 0; i <= maxConsecutive; ++i)
            {
                consecutiveRow.insert(p_model.workVars.at({p.name, s + i}), 1.0);
            }
            model.solver()->addRow(consecutiveRow, -COIN_DBL_MAX, maxConsecutive);
        }

        // Minimum Rest
        int minRestHours = p.minimumRestHours;
        if (minRestHours > 0 && stintWithPitSeconds > 0)
        {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / stintWithPitSeconds));
            
            if (minRestStints > 0 && minRestStints <= totalStints)
            {
                std::vector<int> restAchievedVars;
                CoinPackedVector oneRestRow;
                
                int possibleRestStarts = totalStints - minRestStints + 1;
                for (int s = 0; s < possibleRestStarts; ++s)
                {
                    int restVarIdx = model.solver()->getNumCols();
                    restAchievedVars.push_back(restVarIdx);
                    model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0);
                    model.solver()->setInteger(restVarIdx);
                    oneRestRow.insert(restVarIdx, 1.0);

                    CoinPackedVector enforceRestRow;
                    for (int i = 0; i < minRestStints; ++i)
                    {
                        enforceRestRow.insert(p_model.workVars.at({p.name, s + i}), 1.0);
                    }
                    double M = minRestStints + 1;
                    enforceRestRow.insert(restVarIdx, M);
                    model.solver()->addRow(enforceRestRow, -COIN_DBL_MAX, M);
                }

                if (oneRestRow.getNumElements() > 0) 
                {
                    model.solver()->addRow(oneRestRow, 1.0, COIN_DBL_MAX);
                }
            }
        }
    } 

    return p_model;
}


/**
 * @brief Internal solve function.
 * @return A JSON object with the results.
 */
json solve_schedule_internal(const SolverContext &ctx)
{
    // Calculate Race Parameters
    double lapTimeSeconds = ctx.raceData.avgLapTimeInSeconds;
    double pitTimeSeconds = ctx.raceData.pitTimeInSeconds;
    int stintLaps = (ctx.raceData.fuelUsePerLap > 0) ? static_cast<int>(ctx.raceData.fuelTankSize / ctx.raceData.fuelUsePerLap) : 0;
    double stintWithPitSeconds = (stintLaps * lapTimeSeconds) + pitTimeSeconds;
    double raceDurationSeconds = ctx.raceData.durationHours * 3600.0;
    int totalStints = (stintWithPitSeconds > 0) ? static_cast<int>(std::ceil(raceDurationSeconds / stintWithPitSeconds)) : 0;

    if (totalStints <= 0)
    {
        throw std::runtime_error("Invalid race parameters: totalStints must be > 0.");
    }

    // Initialize Solver and Model
    std::unique_ptr<OsiClpSolverInterface> mainSolver(new OsiClpSolverInterface);
    mainSolver->setObjSense(1.0);
    CbcModel mainModel(*mainSolver);

    mainModel.setDblParam(CbcModel::CbcAllowableFractionGap, ctx.optimalityGap);
    mainModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(ctx.timeLimit));
    mainModel.setLogLevel(ctx.quiet ? 0 : 1);
    mainSolver->setLogLevel(ctx.quiet ? 0 : 1);

    // Filter Participant Pools
    std::vector<TeamMember> driverPool;
    std::vector<TeamMember> spotterPool;
    for (const auto& member : ctx.raceData.teamMembers) {
        if (member.isDriver) driverPool.push_back(member);
        if (member.isSpotter) spotterPool.push_back(member);
    }

    if (driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }

    // Add Driver Model
    ParticipantModel driverModel = add_participant_model(
        mainModel, driverPool, totalStints, "Drive", ctx, stintWithPitSeconds, stintLaps
    );

    // Add Core Constraints
    for (int s = 0; s < totalStints; ++s)
    {
        CoinPackedVector oneDriverRow;
        for (const auto &p : driverPool)
        {
            oneDriverRow.insert(driverModel.workVars.at({p.name, s}), 1.0);
        }
        mainModel.solver()->addRow(oneDriverRow, 1.0, 1.0);
    }

    if (!ctx.raceData.firstStintDriver.empty())
    {
        std::string firstName = ctx.raceData.firstStintDriver;
        auto it = std::find_if(driverPool.begin(), driverPool.end(), 
                             [&](const TeamMember& m){ return m.name == firstName; });
        if (it != driverPool.end())
        {
            int varIdx = driverModel.workVars.at({firstName, 0});
            mainModel.solver()->setColBounds(varIdx, 1.0, 1.0);
        }
    }

    // Add Spotter Model
    ParticipantModel spotterModel("Spot");

    if (ctx.spotterMode == "integrated")
    {
        if (spotterPool.empty() && !ctx.allowNoSpotter) {
            throw std::runtime_error("Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
        }
        spotterModel = add_participant_model(
            mainModel, spotterPool, totalStints, "Spot", ctx, stintWithPitSeconds, stintLaps
        );
        for (int s = 0; s < totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : spotterPool) {
                row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (ctx.allowNoSpotter) mainModel.solver()->addRow(row, 0.0, 1.0);
            else mainModel.solver()->addRow(row, 1.0, 1.0);
        }
        for (const auto& p : ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < totalStints; ++s) {
                    CoinPackedVector row;
                    row.insert(driverModel.workVars.at({p.name, s}), 1.0);
                    row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
                    mainModel.solver()->addRow(row, 0.0, 1.0);
                }
            }
        }
    }

    // Solve
    auto solveStart = std::chrono::high_resolution_clock::now();
    mainModel.branchAndBound();
    auto solveEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> solveDuration = solveEnd - solveStart;

    // Process Results
    json outputJson;
    outputJson["raceData"] = ctx.raceData;

    std::vector<ScheduleEntry> schedule;
    if (!mainModel.isProvenOptimal() && mainModel.isProvenInfeasible())
    {
        throw std::runtime_error("Model is infeasible. No solution exists.");
    }

    const double* mainSolution = mainModel.solver()->getColSolution();
    for (int s = 0; s < totalStints; ++s) {
        ScheduleEntry entry;
        entry.stint = s + 1;
        entry.driver = "N/A";
        entry.spotter = "N/A";
        for (const auto& p : driverPool) {
            if (mainSolution[driverModel.workVars.at({p.name, s})] > 0.5) {
                entry.driver = p.name;
                break;
            }
        }
        schedule.push_back(entry);
    }

    if (ctx.spotterMode == "integrated") 
    {
        for (int s = 0; s < totalStints; ++s) {
            if (spotterPool.empty()) break;
            for (const auto& p : spotterPool) {
                if (mainSolution[spotterModel.workVars.at({p.name, s})] > 0.5) {
                    schedule[s].spotter = p.name;
                    break;
                }
            }
        }
    } 
    else if (ctx.spotterMode == "sequential" && !spotterPool.empty())
    {
        std::unique_ptr<OsiClpSolverInterface> spotterSolver(new OsiClpSolverInterface);
        spotterSolver->setObjSense(1.0);
        CbcModel spotterCbcModel(*spotterSolver);
        spotterCbcModel.setDblParam(CbcModel::CbcAllowableFractionGap, ctx.optimalityGap);
        spotterCbcModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(ctx.timeLimit));
        spotterCbcModel.setLogLevel(ctx.quiet ? 0 : 1);

        ParticipantModel seqSpotterModel = add_participant_model(
            spotterCbcModel, spotterPool, totalStints, "Spot", ctx, stintWithPitSeconds, stintLaps
        );

        for (int s = 0; s < totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : spotterPool) {
                row.insert(seqSpotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (ctx.allowNoSpotter) spotterCbcModel.solver()->addRow(row, 0.0, 1.0);
            else spotterCbcModel.solver()->addRow(row, 1.0, 1.0);
        }

        for (int s = 0; s < totalStints; ++s) {
            const std::string& driverName = schedule[s].driver;
            if (driverName == "N/A") continue;
            auto it = std::find_if(spotterPool.begin(), spotterPool.end(), 
                                 [&](const TeamMember& m){ return m.name == driverName; });
            if (it != spotterPool.end()) {
                int varIdx = seqSpotterModel.workVars.at({driverName, s});
                spotterCbcModel.solver()->setColBounds(varIdx, 0.0, 0.0);
            }
        }

        auto seqSolveStart = std::chrono::high_resolution_clock::now();
        spotterCbcModel.branchAndBound();
        auto seqSolveEnd = std::chrono::high_resolution_clock::now();
        solveDuration += (seqSolveEnd - seqSolveStart);

        if (spotterCbcModel.isProvenOptimal() || spotterCbcModel.isProvenInfeasible() == 0) {
            const double* spotterSolution = spotterCbcModel.solver()->getColSolution();
            for (int s = 0; s < totalStints; ++s) {
                for (const auto& p : spotterPool) {
                    if (spotterSolution[seqSpotterModel.workVars.at({p.name, s})] > 0.5) {
                        schedule[s].spotter = p.name;
                        break;
                    }
                }
            }
        } else {
            // Could not find a sequential spotter solution, but we don't fail the whole solve
        }
    }
    
    // Build the final JSON output
    outputJson["solveDurationSeconds"] = solveDuration.count();
    json scheduleJson = json::array();
    bool hasSpotters = (ctx.spotterMode != "none");
    for(const auto& entry : schedule) {
        json entryJson;
        entryJson["stint"] = entry.stint;
        entryJson["driver"] = entry.driver;
        if (hasSpotters) {
            entryJson["spotter"] = entry.spotter;
        }
        scheduleJson.push_back(entryJson);
    }
    outputJson["schedule"] = scheduleJson;
    outputJson["success"] = true;
    return outputJson;
}


// --- C-API Implementation ---

/**
 * @brief Allocates a C-string from a std::string and returns it.
 * The caller is responsible for freeing this with free_solver_result.
 */
char* create_output_string(const std::string& s) {
    char* cstr = new char[s.length() + 1];
    std::strcpy(cstr, s.c_str());
    return cstr;
}

/**
 * @brief The main C-API function.
 */
int solve_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson)
{
    try {
        // Parse input JSON string
        json rawJsonData = json::parse(raceDataJson);

        // Build SolverContext
        SolverContext ctx;
        ctx.quiet = options.quiet;
        ctx.timeLimit = options.timeLimit;
        ctx.spotterMode = options.spotterMode;
        ctx.allowNoSpotter = options.allowNoSpotter;
        ctx.optimalityGap = options.optimalityGap;

        // Parse JSON into RaceData struct
        ctx.raceData = rawJsonData.get<RaceData>();

        // Run the solver
        json resultJson = solve_schedule_internal(ctx);

        // Allocate and return output string
        *outputJson = create_output_string(resultJson.dump(4));
        return 0; // Success

    } catch (const std::exception& e) {
        // Handle errors
        json errorJson;
        errorJson["success"] = false;
        errorJson["error"] = e.what();
        *outputJson = create_output_string(errorJson.dump(4));
        return -1; // Failure
    }
}

/**
 * @brief The C-API function to free memory.
 */
void free_solver_result(char* resultJson)
{
    if (resultJson != nullptr) {
        delete[] resultJson;
    }
}
