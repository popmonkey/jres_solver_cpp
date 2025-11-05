#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <memory> // For std::unique_ptr
#include <vector>
#include <map>
#include <cmath>     // For std::ceil, std::floor
#include <chrono>    // For time parsing
#include <iomanip>   // For std::get_time, std::setw, std::setfill
#include <sstream>   // For std::stringstream
#include <algorithm> // For std::find_if
#include <ctime>     // For time_t, tm, timegm/_mkgmtime

// --- 3rd Party Libs ---
#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

// --- COIN-OR Cbc ---
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include "CoinPackedVector.hpp"
#include "CoinBuild.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Data Structures ---

/**
 * @brief Represents a single team member.
 */
struct TeamMember
{
    std::string name;
    bool isDriver = false;
    bool isSpotter = false;
    int preferredStints = 3; // Default value
    int minimumRestHours = 0; // Default value
};

/**
 * @brief Represents the main race data input.
 */
struct RaceData
{
    double avgLapTimeInSeconds;
    double pitTimeInSeconds;
    double fuelTankSize;
    double fuelUsePerLap;
    int durationHours;
    std::string raceStartUTC;
    std::string firstStintDriver; // Optional, may be empty
    std::vector<TeamMember> teamMembers;
    json availability; // Keep availability as raw JSON for easy lookup
};

// --- JSON Deserialization ---
// These functions tell nlohmann/json how to convert
// JSON data into our C++ structs.

// Helper for TeamMember
void from_json(const json &j, TeamMember &member)
{
    j.at("name").get_to(member.name);
    member.isDriver = j.value("isDriver", false);
    member.isSpotter = j.value("isSpotter", false);
    member.preferredStints = j.value("preferredStints", 3);
    member.minimumRestHours = j.value("minimumRestHours", 0);
}

// Helper for RaceData
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
// These functions tell nlohmann/json how to convert
// our C++ structs *back* into JSON.

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


/**
 * @brief Holds all configuration and data for the solver.
 */
struct SolverContext
{
    bool quiet;
    int timeLimit;
    std::string spotterMode;
    bool allowNoSpotter;
    double optimalityGap;
    RaceData raceData;
    std::string outputPath; // Optional
};

/**
 * @brief Manages the Cbc variable indices for a participant model.
 */
struct ParticipantModel
{
    std::string prefix;
    // Maps (participantName, stint) -> CbcColumnIndex
    std::map<std::pair<std::string, int>, int> workVars;
    std::map<std::pair<std::string, int>, int> switchVars;
    int maxWorkStintsVar = -1;
    int minWorkStintsVar = -1;

    ParticipantModel(std::string p) : prefix(p) {}
};

/**
 * @brief Represents one line in the final schedule output.
 */
struct ScheduleEntry {
    int stint;
    std::string driver;
    std::string spotter;
};

/**
 * @brief Helper to convert ISO 8601 string to a time_point.
 */
namespace TimeHelpers
{
    // --- THIS IS THE FIX ---
    // Portable version of timegm (which converts a UTC tm struct to time_t)
    // std::mktime assumes local time, which is the source of our bug.
    std::time_t timegm_portable(std::tm *tm)
    {
#if defined(_WIN32) || defined(_WIN64)
        // Windows
        return _mkgmtime(tm);
#else
        // macOS, Linux, and other POSIX
        return timegm(tm);
#endif
    }
    // --- END FIX ---


    // Z-suffix indicates UTC
    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string)
    {
        std::tm tm = {};
        std::stringstream ss(utc_string);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        // We ignore the fractional seconds (.%fZ) as we only care about the hour

        // --- THIS IS THE FIX ---
        // Use our portable timegm_portable instead of std::mktime
        return std::chrono::system_clock::from_time_t(timegm_portable(&tm));
        // --- END FIX ---
    }

    // Formats a time_point into the key format (e.g., "2025-01-01T14:00:00.000Z")
    std::string timePointToKey(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time); // Use gmtime for UTC
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:00:00.000Z");
        return ss.str();
    }
} // namespace TimeHelpers


/**
 * @brief Adds a generic participant model (variables, constraints, objectives)
 * to the CbcModel. This is a port of _add_participant_model from Python.
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

    // --- 1. Create Variables ---
    p_model.maxWorkStintsVar = model.solver()->getNumCols();
    model.solver()->addCol(CoinPackedVector(), 0.0, COIN_DBL_MAX, 0.0); // No bool
    model.solver()->setInteger(p_model.maxWorkStintsVar); // Set as integer
    model.solver()->setColName(p_model.maxWorkStintsVar, prefix + "MaxStints");

    p_model.minWorkStintsVar = model.solver()->getNumCols();
    model.solver()->addCol(CoinPackedVector(), 0.0, COIN_DBL_MAX, 0.0); // No bool
    model.solver()->setInteger(p_model.minWorkStintsVar); // Set as integer
    model.solver()->setColName(p_model.minWorkStintsVar, prefix + "MinStints");

    for (const auto &p : participants)
    {
        for (int s = 0; s < totalStints; ++s)
        {
            // --- Work Variables (Binary) ---
            int workVarIdx = model.solver()->getNumCols();
            p_model.workVars[{p.name, s}] = workVarIdx;
            model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0); // No bool
            model.solver()->setInteger(workVarIdx); // Set as integer (binary)
            model.solver()->setColName(workVarIdx, prefix + "_" + p.name + "_s" + std::to_string(s));

            // --- Switch Variables (Binary) ---
            if (s > 0)
            {
                int switchVarIdx = model.solver()->getNumCols();
                p_model.switchVars[{p.name, s}] = switchVarIdx;
                model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0); // No bool
                model.solver()->setInteger(switchVarIdx); // Set as integer (binary)
                model.solver()->setColName(switchVarIdx, prefix + "Switch_" + p.name + "_s" + std::to_string(s));
            }
        }
    }

    // --- 2. Add Objective Function Components ---
    // A. Balance Objective (Minimize (max - min) * 1000)
    model.solver()->setObjCoeff(p_model.maxWorkStintsVar, 1000.0);
    model.solver()->setObjCoeff(p_model.minWorkStintsVar, -1000.0);

    // B. Switch Objective (Minimize sum(switches) * 100)
    for (const auto &pair : p_model.switchVars)
    {
        model.solver()->setObjCoeff(pair.second, 100.0);
    }

    // C. Preference Objective (Maximize sum(preferred_stints) * 1)
    // (We *subtract* from the minimization objective)
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

    // --- 3. Add Constraints ---
    double totalLaps = totalStints * stintLaps;
    double equalShareLaps = totalLaps / participants.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    int minStintsPerParticipant = (stintLaps > 0) ? static_cast<int>(std::ceil(minLapsPerParticipant / stintLaps)) : 0;

    for (const auto &p : participants)
    {
        // --- Availability Constraints ---
        for (int s = 0; s < totalStints; ++s)
        {
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(stintWithPitSeconds));
            // Check the hour *before* the exact end time (e.g., 2:00:00 is in the 1:00 block)
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
                isAvailable = false; // No availability data = unavailable
            }

            if (!isAvailable)
            {
                // Fix this variable to 0: workVar[p, s] = 0
                int workVarIdx = p_model.workVars.at({p.name, s});
                model.solver()->setColBounds(workVarIdx, 0.0, 0.0);
            }
        }

        // --- Switch Constraints ---
        // switchVar[s] >= workVar[s] - workVar[s-1]
        // ...or: switchVar[s] - workVar[s] + workVar[s-1] >= 0
        for (int s = 1; s < totalStints; ++s)
        {
            CoinPackedVector row;
            row.insert(p_model.switchVars.at({p.name, s}), 1.0);
            row.insert(p_model.workVars.at({p.name, s}), -1.0);
            row.insert(p_model.workVars.at({p.name, s - 1}), 1.0);
            model.solver()->addRow(row, 0.0, COIN_DBL_MAX);
        }

        // --- Max/Min Stint Count Constraints ---
        CoinPackedVector totalStintsRow;
        for (int s = 0; s < totalStints; ++s)
        {
            totalStintsRow.insert(p_model.workVars.at({p.name, s}), 1.0);
        }
        
        // Define totalParticipantStints
        // totalStintsRow - maxWork <= 0  (maxWork >= total)
        CoinPackedVector maxRow = totalStintsRow;
        maxRow.insert(p_model.maxWorkStintsVar, -1.0);
        model.solver()->addRow(maxRow, -COIN_DBL_MAX, 0.0);

        // totalStintsRow - minWork >= 0  (minWork <= total)
        CoinPackedVector minRow = totalStintsRow;
        minRow.insert(p_model.minWorkStintsVar, -1.0);
        model.solver()->addRow(minRow, 0.0, COIN_DBL_MAX);

        // --- Fair Share Constraint (Drivers only) ---
        if (prefix == "Drive")
        {
            // totalStintsRow >= minStintsPerParticipant
            model.solver()->addRow(totalStintsRow, minStintsPerParticipant, COIN_DBL_MAX);
        }

        // --- Max Consecutive Stints ---
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < totalStints - maxConsecutive; ++s)
        {
            // sum(workVar[s]...workVar[s + maxConsecutive]) <= maxConsecutive
            CoinPackedVector consecutiveRow;
            for (int i = 0; i <= maxConsecutive; ++i)
            {
                consecutiveRow.insert(p_model.workVars.at({p.name, s + i}), 1.0);
            }
            model.solver()->addRow(consecutiveRow, -COIN_DBL_MAX, maxConsecutive);
        }

        // --- Minimum Rest Constraints ---
        int minRestHours = p.minimumRestHours;
        if (minRestHours > 0 && stintWithPitSeconds > 0)
        {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / stintWithPitSeconds));
            
            // *** THIS IS THE FIX ***
            // Only add the constraint if the rest period is achievable (i.e., shorter than the race)
            if (minRestStints > 0 && minRestStints <= totalStints)
            {
                // We need to add binary helper variables
                std::vector<int> restAchievedVars;
                CoinPackedVector oneRestRow; // sum(restAchievedVars) >= 1
                
                int possibleRestStarts = totalStints - minRestStints + 1;
                for (int s = 0; s < possibleRestStarts; ++s)
                {
                    int restVarIdx = model.solver()->getNumCols();
                    restAchievedVars.push_back(restVarIdx);
                    model.solver()->addCol(CoinPackedVector(), 0.0, 1.0, 0.0); // No bool
                    model.solver()->setInteger(restVarIdx); // Set as integer (binary)
                    oneRestRow.insert(restVarIdx, 1.0);

                    // Enforce rest: sum(workVar[s]...workVar[s+rest-1]) <= M * (1 - restVar)
                    // ...or: sum(workVar) + M*restVar <= M
                    CoinPackedVector enforceRestRow;
                    for (int i = 0; i < minRestStints; ++i)
                    {
                        enforceRestRow.insert(p_model.workVars.at({p.name, s + i}), 1.0);
                    }
                    double M = minRestStints + 1;
                    enforceRestRow.insert(restVarIdx, M);
                    model.solver()->addRow(enforceRestRow, -COIN_DBL_MAX, M);
                }

                // MustHaveOneRest: sum(restAchievedVars) >= 1
                // Only add if we actually created helper variables
                if (oneRestRow.getNumElements() > 0) 
                {
                    model.solver()->addRow(oneRestRow, 1.0, COIN_DBL_MAX);
                }
            }
        }
    } // end for each participant

    return p_model;
}

/**
 * @brief Prints schedule to console and saves to file (if requested).
 */
void output_results(const SolverContext &ctx, const std::vector<ScheduleEntry> &schedule, double solveDuration)
{
    json outputJson;
    outputJson["raceData"] = ctx.raceData;
    outputJson["solveDurationSeconds"] = solveDuration;
    
    json scheduleJson = json::array();

    if (!ctx.quiet) {
        std::cout << "\n--- 🏁 Race Schedule ---" << std::endl;
        bool hasSpotters = (ctx.spotterMode != "none");

        for(const auto& entry : schedule) {
            std::stringstream ss;
            ss << "Stint " << std::setw(3) << entry.stint
               << ": Driver: " << std::setw(15) << std::left << entry.driver;
            
            json entryJson;
            entryJson["stint"] = entry.stint;
            entryJson["driver"] = entry.driver;

            if (hasSpotters) {
                ss << " | Spotter: " << std::setw(15) << std::left << entry.spotter;
                entryJson["spotter"] = entry.spotter;
            }
            std::cout << ss.str() << std::endl;
            scheduleJson.push_back(entryJson);
        }
    } else {
        // Just build the JSON, don't print
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
    }
    
    outputJson["schedule"] = scheduleJson;

    if (!ctx.outputPath.empty()) {
        try {
            std::ofstream o(ctx.outputPath);
            o << std::setw(4) << outputJson << std::endl;
            if (!ctx.quiet) {
                std::cout << "\n[App] Schedule saved to " << ctx.outputPath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[App] Error: Failed to write output file: " << e.what() << std::endl;
        }
    }
}


/**
 * @brief Main function to formulate and solve the schedule.
 */
void solve_schedule(const SolverContext &ctx)
{
    if (!ctx.quiet)
    {
        std::cout << "[Solver] solve_schedule() called." << std::endl;
        std::cout << "[Solver] Mode: " << ctx.spotterMode << std::endl;
        std::cout << "[Solver] Team Members: " << ctx.raceData.teamMembers.size() << std::endl;
    }

    // --- 1. Calculate Race Parameters ---
    double lapTimeSeconds = ctx.raceData.avgLapTimeInSeconds;
    double pitTimeSeconds = ctx.raceData.pitTimeInSeconds;
    int stintLaps = (ctx.raceData.fuelUsePerLap > 0) ? static_cast<int>(ctx.raceData.fuelTankSize / ctx.raceData.fuelUsePerLap) : 0;
    double stintWithPitSeconds = (stintLaps * lapTimeSeconds) + pitTimeSeconds;
    double raceDurationSeconds = ctx.raceData.durationHours * 3600.0;
    int totalStints = (stintWithPitSeconds > 0) ? static_cast<int>(std::ceil(raceDurationSeconds / stintWithPitSeconds)) : 0;

    if (!ctx.quiet)
    {
        std::cout << "[Solver] Race duration: " << raceDurationSeconds << "s" << std::endl;
        std::cout << "[Solver] Stint laps: " << stintLaps << std::endl;
        std::cout << "[Solver] Stint duration: " << stintWithPitSeconds << "s" << std::endl;
        std::cout << "[Solver] Total stints: " << totalStints << std::endl;
    }

    if (totalStints <= 0)
    {
        throw std::runtime_error("Invalid race parameters: totalStints must be > 0.");
    }

    // --- 2. Initialize Solver and Model ---
    // This is now the "main" or "driver" model
    std::unique_ptr<OsiClpSolverInterface> mainSolver(new OsiClpSolverInterface);
    mainSolver->setObjSense(1.0); // Minimize (default)
    CbcModel mainModel(*mainSolver);

    // Set solver parameters
    mainModel.setDblParam(CbcModel::CbcAllowableFractionGap, ctx.optimalityGap);
    mainModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(ctx.timeLimit));
    if (ctx.quiet) {
        mainModel.setLogLevel(0);
        mainSolver->setLogLevel(0);
    } else {
        mainModel.setLogLevel(1);
        mainSolver->setLogLevel(1);
    }

    // --- 3. Filter Participant Pools ---
    std::vector<TeamMember> driverPool;
    std::vector<TeamMember> spotterPool;
    for (const auto& member : ctx.raceData.teamMembers) {
        if (member.isDriver) {
            driverPool.push_back(member);
        }
        if (member.isSpotter) {
            spotterPool.push_back(member);
        }
    }
    if (!ctx.quiet) {
        std::cout << "[Solver] Driver pool size: " << driverPool.size() << std::endl;
        std::cout << "[Solver] Spotter pool size: " << spotterPool.size() << std::endl;
    }

    // --- 4. Add Driver Model ---
    if (driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }

    ParticipantModel driverModel = add_participant_model(
        mainModel, driverPool, totalStints, "Drive", ctx, stintWithPitSeconds, stintLaps
    );

    // --- 5. Add Core Constraints ---
    // A. OneDriver_Stint_s: sum(driveVar[p, s]) == 1
    for (int s = 0; s < totalStints; ++s)
    {
        CoinPackedVector oneDriverRow;
        for (const auto &p : driverPool)
        {
            oneDriverRow.insert(driverModel.workVars.at({p.name, s}), 1.0);
        }
        mainModel.solver()->addRow(oneDriverRow, 1.0, 1.0); // == 1
    }

    // B. FirstStintDriver
    if (!ctx.raceData.firstStintDriver.empty())
    {
        std::string firstName = ctx.raceData.firstStintDriver;
        auto it = std::find_if(driverPool.begin(), driverPool.end(), 
                             [&](const TeamMember& m){ return m.name == firstName; });
        
        if (it != driverPool.end())
        {
            if (!ctx.quiet) {
                std::cout << "[Solver] Adding constraint: First stint driver is " << firstName << std::endl;
            }
            // Fix variable: driveVar[firstName, 0] = 1
            int varIdx = driverModel.workVars.at({firstName, 0});
            mainModel.solver()->setColBounds(varIdx, 1.0, 1.0);
        }
        else
        {
            std::cerr << "[Solver] Warning: FirstStintDriver '" << firstName << "' is not in the driver pool. Constraint ignored." << std::endl;
        }
    }

    // --- 6. Add Spotter Model (based on mode) ---
    ParticipantModel spotterModel("Spot"); // Create, even if empty

    if (ctx.spotterMode == "integrated")
    {
        if (!ctx.quiet) {
            std::cout << "[Solver] Adding integrated spotter model..." << std::endl;
        }
        if (spotterPool.empty() && !ctx.allowNoSpotter) {
            throw std::runtime_error("Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
        }

        // Add spotter model to the *main model*
        spotterModel = add_participant_model(
            mainModel, spotterPool, totalStints, "Spot", ctx, stintWithPitSeconds, stintLaps
        );

        // A. Spotter Coverage Constraint
        for (int s = 0; s < totalStints; ++s) {
            CoinPackedVector spotterCoverageRow;
            for (const auto& p : spotterPool) {
                spotterCoverageRow.insert(spotterModel.workVars.at({p.name, s}), 1.0);
            }

            if (ctx.allowNoSpotter) {
                // sum(spotVar[p, s]) <= 1
                mainModel.solver()->addRow(spotterCoverageRow, 0.0, 1.0);
            } else {
                // sum(spotVar[p, s]) == 1
                mainModel.solver()->addRow(spotterCoverageRow, 1.0, 1.0);
            }
        }

        // B. No Dual-Duty Constraint
        for (const auto& p : ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < totalStints; ++s) {
                    // driveVar[p, s] + spotVar[p, s] <= 1
                    CoinPackedVector dualDutyRow;
                    dualDutyRow.insert(driverModel.workVars.at({p.name, s}), 1.0);
                    dualDutyRow.insert(spotterModel.workVars.at({p.name, s}), 1.0);
                    mainModel.solver()->addRow(dualDutyRow, 0.0, 1.0);
                }
            }
        }
    }
    // Note: The "sequential" logic will happen *after* the main solve.

    // --- 7. Solve the Main Model (Drivers, or Drivers + Spotters) ---
    if (!ctx.quiet) {
        std::cout << "[Solver] Calling CbcModel::branchAndBound() for main model..." << std::endl;
    }

    auto solveStart = std::chrono::high_resolution_clock::now();
    mainModel.branchAndBound();
    auto solveEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> solveDuration = solveEnd - solveStart;

    if (!ctx.quiet) {
        std::cout << "[Solver] Main solve finished." << std::endl;
        std::cout << "[Solver] Status: " << mainModel.status() << " (secondary " << mainModel.secondaryStatus() << ")" << std::endl;
        std::cout << "[Solver] Is proven optimal? " << mainModel.isProvenOptimal() << std::endl;
    }

    // --- 8. Process Main Model Results (And Solve Spotters if Sequential) ---
    std::vector<ScheduleEntry> schedule;
    if (mainModel.isProvenOptimal() || mainModel.isProvenInfeasible() == 0) // Found a solution
    {
        const double* mainSolution = mainModel.solver()->getColSolution();
        
        // --- 8A. Process Driver Results (ALWAYS) ---
        for (int s = 0; s < totalStints; ++s) {
            ScheduleEntry entry;
            entry.stint = s + 1; // 1-indexed
            entry.driver = "N/A";
            entry.spotter = "N/A"; // Default

            for (const auto& p : driverPool) {
                int varIdx = driverModel.workVars.at({p.name, s});
                if (mainSolution[varIdx] > 0.5) {
                    entry.driver = p.name;
                    break;
                }
            }
            schedule.push_back(entry); // Add entry with driver, spotter is "N/A"
        }

        // --- 8B. Process Spotter Results (Based on Mode) ---
        if (ctx.spotterMode == "integrated") 
        {
            if (!ctx.quiet) {
                std::cout << "[Solver] Processing integrated spotter results..." << std::endl;
            }
            for (int s = 0; s < totalStints; ++s) {
                if (spotterPool.empty()) break;
                for (const auto& p : spotterPool) {
                    int varIdx = spotterModel.workVars.at({p.name, s});
                    if (mainSolution[varIdx] > 0.5) {
                        schedule[s].spotter = p.name;
                        break;
                    }
                }
            }
        } 
        else if (ctx.spotterMode == "sequential")
        {
            if (!ctx.quiet) {
                std::cout << "[Solver] Starting sequential spotter solve..." << std::endl;
            }
            if (spotterPool.empty()) {
                if (!ctx.quiet) std::cout << "[Solver] Spotter pool is empty, skipping sequential solve." << std::endl;
            } else {
                // --- Create a NEW, separate model for spotters ---
                std::unique_ptr<OsiClpSolverInterface> spotterSolver(new OsiClpSolverInterface);
                spotterSolver->setObjSense(1.0); // Minimize
                CbcModel spotterCbcModel(*spotterSolver);

                // Set solver parameters
                spotterCbcModel.setDblParam(CbcModel::CbcAllowableFractionGap, ctx.optimalityGap);
                spotterCbcModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(ctx.timeLimit));
                if (ctx.quiet) spotterCbcModel.setLogLevel(0);
                else spotterCbcModel.setLogLevel(1);

                // --- Add spotter model to the new CbcModel ---
                ParticipantModel seqSpotterModel = add_participant_model(
                    spotterCbcModel, spotterPool, totalStints, "Spot", ctx, stintWithPitSeconds, stintLaps
                );

                // --- Add spotter constraints to the new CbcModel ---
                // A. Spotter Coverage
                for (int s = 0; s < totalStints; ++s) {
                    CoinPackedVector row;
                    for (const auto& p : spotterPool) {
                        row.insert(seqSpotterModel.workVars.at({p.name, s}), 1.0);
                    }
                    if (ctx.allowNoSpotter) spotterCbcModel.solver()->addRow(row, 0.0, 1.0); // <= 1
                    else spotterCbcModel.solver()->addRow(row, 1.0, 1.0); // == 1
                }

                // B. No Dual-Duty (based on *fixed* driver schedule)
                for (int s = 0; s < totalStints; ++s) {
                    const std::string& driverName = schedule[s].driver;
                    if (driverName == "N/A") continue;

                    // Check if this driver is *also* in the spotter pool
                    auto it = std::find_if(spotterPool.begin(), spotterPool.end(), 
                                         [&](const TeamMember& m){ return m.name == driverName; });
                    
                    if (it != spotterPool.end()) {
                        // This person is driving stint 's' and is a spotter.
                        // They CANNOT spot this stint.
                        // spotVar[driverName, s] = 0
                        int varIdx = seqSpotterModel.workVars.at({driverName, s});
                        spotterCbcModel.solver()->setColBounds(varIdx, 0.0, 0.0);
                    }
                }

                // --- Solve the sequential spotter model ---
                if (!ctx.quiet) std::cout << "[Solver] Calling CbcModel::branchAndBound() for sequential spotter model..." << std::endl;
                
                auto seqSolveStart = std::chrono::high_resolution_clock::now();
                spotterCbcModel.branchAndBound();
                auto seqSolveEnd = std::chrono::high_resolution_clock::now();
                
                solveDuration += (seqSolveEnd - seqSolveStart); // Add to total time

                if (spotterCbcModel.isProvenOptimal() || spotterCbcModel.isProvenInfeasible() == 0) {
                    const double* spotterSolution = spotterCbcModel.solver()->getColSolution();
                    for (int s = 0; s < totalStints; ++s) {
                        for (const auto& p : spotterPool) {
                            int varIdx = seqSpotterModel.workVars.at({p.name, s});
                            if (spotterSolution[varIdx] > 0.5) {
                                schedule[s].spotter = p.name;
                                break;
                            }
                        }
                    }
                } else {
                     std::cerr << "[Solver] Error: Could not find a valid spotter schedule in sequential mode." << std::endl;
                }
            }
        }
        
        output_results(ctx, schedule, solveDuration.count());
    }
    else
    {
        std::cerr << "[Solver] Error: Could not find a valid solution." << std::endl;
        if (mainModel.isProvenInfeasible()) {
            std::cerr << "[Solver] The model is infeasible. No solution exists." << std::endl;
        }
    }

}

/**
 * @brief Main entry point for the C++ Race Solver.
 */
int main(int argc, char **argv)
{
    // --- 1. Parse Command-Line Arguments ---
    cxxopts::Options options("solver", "A C++ port of the JRES endurance race solver.");
    options.add_options()("i,input", "Path to the race_data.json file. Reads from stdin if not provided.", cxxopts::value<std::string>())("o,output", "Optional. Path to save the schedule as a JSON file.", cxxopts::value<std::string>())("t,time-limit", "Maximum time in seconds to let the solver run.", cxxopts::value<int>()->default_value("30"))("q,quiet", "Suppress INFO logs.", cxxopts::value<bool>()->default_value("false"))("s,spotter-mode", "Method for scheduling spotters (none, integrated, sequential).", cxxopts::value<std::string>()->default_value("none"))("allow-no-spotter", "Allow stints to have no spotter assigned.", cxxopts::value<bool>()->default_value("false"))("g,optimality-gap", "Solver stops when the gap to optimal is less than this (e.g., 0.01 for 1%).", cxxopts::value<double>()->default_value("0.0"))("h,help", "Print usage.");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    // --- 2. Load Input JSON Data ---
    json rawJsonData;
    bool quiet = result["quiet"].as<bool>(); // Get this early
    try
    {
        if (result.count("input"))
        {
            std::string inputPath = result["input"].as<std::string>();
            if (!quiet)
                std::cout << "[App] Loading data from file: " << inputPath << std::endl;
            std::ifstream f(inputPath);
            if (!f.is_open())
            {
                throw std::runtime_error("Could not open input file: " + inputPath);
            }
            rawJsonData = json::parse(f);
        }
        else
        {
            if (!quiet)
                std::cout << "[App] Loading data from stdin..." << std::endl;
            rawJsonData = json::parse(std::cin);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[App] Error: " << e.what() << std::endl;
        return 1;
    }

    // --- 3. Build Solver Context ---
    SolverContext ctx;
    try
    {
        ctx.quiet = quiet;
        ctx.timeLimit = result["time-limit"].as<int>();
        ctx.spotterMode = result["spotter-mode"].as<std::string>();
        ctx.allowNoSpotter = result["allow-no-spotter"].as<bool>();
        ctx.optimalityGap = result["optimality-gap"].as<double>();
        ctx.outputPath = result.count("output") ? result["output"].as<std::string>() : "";

        // This line automatically calls our from_json functions
        ctx.raceData = rawJsonData.get<RaceData>();

        if (!quiet)
        {
            std::cout << "[App] JSON data parsed and validated." << std::endl;
        }
    }
    catch (const json::exception &e)
    {
        std::cerr << "[App] Error: Failed to parse JSON into data structures." << std::endl;
        std::cerr << "Message: " << e.what() << std::endl;
        std::cerr << "Field ID: " << e.id << std::endl;
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "[App] Error: " << e.what() << std::endl;
        return 1;
    }

    // --- 4. Call the Solver ---
    try
    {
        solve_schedule(ctx);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[Solver] Critical Error: " << e.what() << '\n';
        return 1;
    }

    if (!quiet)
    {
        std::cout << "[App] Solver finished." << std::endl;
    }

    return 0;
}

