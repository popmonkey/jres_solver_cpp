#include "jres_solver_impl.hpp"

// --- C++ Standard Libs ---
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <map>
#include <cmath>     // For std::ceil, std::floor
#include <chrono>    // For time parsing
#include <iomanip>   // For std::get_time, std::setw, std::setfill
#include <sstream>   // For std::stringstream
#include <algorithm> // For std::find_if
#include <ctime>     // For time_t, tm, timegm/_mkgmtime

// --- 3rd Party Libs ---
#include "nlohmann/json.hpp"

// --- COIN-OR Cbc ---
// All Cbc includes are isolated to this file
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include "CoinPackedVector.hpp"
#include "CoinBuild.hpp"

using json = nlohmann::json;

// --- Internal-Only Data Structures ---

// This struct is only used by the implementation
struct ParticipantModel
{
    std::string prefix;
    std::map<std::pair<std::string, int>, int> workVars;
    std::map<std::pair<std::string, int>, int> switchVars;
    int maxWorkStintsVar = -1;
    int minWorkStintsVar = -1;

    ParticipantModel(std::string p) : prefix(p) {}
};


// --- Internal-Only Helper Namespaces ---

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

    std::string timePointToString(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
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


// --- Class Implementation ---

JresSolverImpl::JresSolverImpl(const SolverContext& ctx)
    : m_ctx(ctx), // Copy the context
      m_stintWithPitSeconds(0.0),
      m_totalStints(0),
      m_stintLaps(0)
{
    // Calculate Race Parameters
    double lapTimeSeconds = m_ctx.raceData.avgLapTimeInSeconds;
    double pitTimeSeconds = m_ctx.raceData.pitTimeInSeconds;
    m_stintLaps = (m_ctx.raceData.fuelUsePerLap > 0) ? static_cast<int>(m_ctx.raceData.fuelTankSize / m_ctx.raceData.fuelUsePerLap) : 0;
    m_stintWithPitSeconds = (m_stintLaps * lapTimeSeconds) + pitTimeSeconds;
    double raceDurationSeconds = m_ctx.raceData.durationHours * 3600.0;
    m_totalStints = (m_stintWithPitSeconds > 0) ? static_cast<int>(std::ceil(raceDurationSeconds / m_stintWithPitSeconds)) : 0;

    if (m_totalStints <= 0)
    {
        throw std::runtime_error("Invalid race parameters: totalStints must be > 0.");
    }

    // Initialize Solver and Model
    m_mainSolver = std::make_unique<OsiClpSolverInterface>();
    m_mainSolver->setObjSense(1.0);
    m_mainModel = std::make_unique<CbcModel>(*m_mainSolver);

    m_mainModel->setDblParam(CbcModel::CbcAllowableFractionGap, m_ctx.optimalityGap);
    m_mainModel->setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(m_ctx.timeLimit));
    m_mainModel->setLogLevel(0);  // m_ctx.quiet ? 0 : 1
    m_mainSolver->setLogLevel(0); // m_ctx.quiet ? 0 : 1

    // Filter Participant Pools
    for (const auto& member : m_ctx.raceData.teamMembers) {
        if (member.isDriver) m_driverPool.push_back(member);
        if (member.isSpotter) m_spotterPool.push_back(member);
    }

    if (m_driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }
}

// Required for unique_ptr to forward-declared types
JresSolverImpl::~JresSolverImpl() = default;


json JresSolverImpl::solve()
{
    // Note: All setup is now done in the constructor.
    // m_mainModel, m_driverPool, m_totalStints, etc. are all ready.

    // Add Driver Model
    ParticipantModel driverModel = add_participant_model(
        *m_mainModel, m_driverPool, "Drive", m_stintWithPitSeconds, m_stintLaps
    );

    // Add Core Constraints
    for (int s = 0; s < m_totalStints; ++s)
    {
        CoinPackedVector oneDriverRow;
        for (const auto &p : m_driverPool)
        {
            oneDriverRow.insert(driverModel.workVars.at({p.name, s}), 1.0);
        }
        m_mainModel->solver()->addRow(oneDriverRow, 1.0, 1.0);
    }

    if (!m_ctx.raceData.firstStintDriver.empty())
    {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), 
                             [&](const TeamMember& m){ return m.name == firstName; });
        if (it != m_driverPool.end())
        {
            int varIdx = driverModel.workVars.at({firstName, 0});
            m_mainModel->solver()->setColBounds(varIdx, 1.0, 1.0);
        }
    }

    // Add Spotter Model
    ParticipantModel spotterModel("Spot");

    if (m_ctx.spotterMode == SpotterMode::Integrated)
    {
        if (m_spotterPool.empty() && !m_ctx.allowNoSpotter) {
            throw std::runtime_error("Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
        }
        spotterModel = add_participant_model(
            *m_mainModel, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );
        for (int s = 0; s < m_totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : m_spotterPool) {
                row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (m_ctx.allowNoSpotter) m_mainModel->solver()->addRow(row, 0.0, 1.0);
            else m_mainModel->solver()->addRow(row, 1.0, 1.0);
        }
        for (const auto& p : m_ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < m_totalStints; ++s) {
                    CoinPackedVector row;
                    row.insert(driverModel.workVars.at({p.name, s}), 1.0);
                    row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
                    m_mainModel->solver()->addRow(row, 0.0, 1.0);
                }
            }
        }
    }

    // Solve
    auto solveStart = std::chrono::high_resolution_clock::now();
    m_mainModel->branchAndBound();
    auto solveEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> solveDuration = solveEnd - solveStart;

    // Process Results
    json outputJson;
    outputJson["raceData"] = m_ctx.raceData;

    std::vector<ScheduleEntry> schedule;
    if (!m_mainModel->isProvenOptimal() && m_mainModel->isProvenInfeasible())
    {
        throw std::runtime_error("Model is infeasible. No solution exists.");
    }

    const double* mainSolution = m_mainModel->solver()->getColSolution();
    
    // Setup for Time Calculation
    auto currentTimePoint = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);
    double stintDurationSeconds = m_stintLaps * m_ctx.raceData.avgLapTimeInSeconds;
    double pitSeconds = m_ctx.raceData.pitTimeInSeconds;

    for (int s = 0; s < m_totalStints; ++s) {
        ScheduleEntry entry;
        entry.stint = s + 1;
        entry.driver = "N/A";
        entry.spotter = "N/A";
        entry.laps = m_stintLaps;
        
        // Calculate times
        entry.startTimeUTC = TimeHelpers::timePointToString(currentTimePoint);
        auto endTimePoint = currentTimePoint + std::chrono::seconds(static_cast<long>(stintDurationSeconds));
        entry.endTimeUTC = TimeHelpers::timePointToString(endTimePoint);
        
        // Advance time (Stint + Pit)
        currentTimePoint = endTimePoint + std::chrono::seconds(static_cast<long>(pitSeconds));

        for (const auto& p : m_driverPool) {
            if (mainSolution[driverModel.workVars.at({p.name, s})] > 0.5) {
                entry.driver = p.name;
                break;
            }
        }
        schedule.push_back(entry);
    }

    if (m_ctx.spotterMode == SpotterMode::Integrated)
    {
        for (int s = 0; s < m_totalStints; ++s) {
            if (m_spotterPool.empty()) break;
            for (const auto& p : m_spotterPool) {
                if (mainSolution[spotterModel.workVars.at({p.name, s})] > 0.5) {
                    schedule[s].spotter = p.name;
                    break;
                }
            }
        }
    } 
    else if (m_ctx.spotterMode == SpotterMode::Sequential && !m_spotterPool.empty())
    {
        std::unique_ptr<OsiClpSolverInterface> spotterSolver(new OsiClpSolverInterface);
        spotterSolver->setObjSense(1.0);
        CbcModel spotterCbcModel(*spotterSolver);
        spotterCbcModel.setDblParam(CbcModel::CbcAllowableFractionGap, m_ctx.optimalityGap);
        spotterCbcModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(m_ctx.timeLimit));
        spotterCbcModel.setLogLevel(m_ctx.quiet ? 0 : 1);

        ParticipantModel seqSpotterModel = add_participant_model(
            spotterCbcModel, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );

        for (int s = 0; s < m_totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : m_spotterPool) {
                row.insert(seqSpotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (m_ctx.allowNoSpotter) spotterCbcModel.solver()->addRow(row, 0.0, 1.0);
            else spotterCbcModel.solver()->addRow(row, 1.0, 1.0);
        }

        for (int s = 0; s < m_totalStints; ++s) {
            const std::string& driverName = schedule[s].driver;
            if (driverName == "N/A") continue;
            auto it = std::find_if(m_spotterPool.begin(), m_spotterPool.end(), 
                                 [&](const TeamMember& m){ return m.name == driverName; });
            if (it != m_spotterPool.end()) {
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
            for (int s = 0; s < m_totalStints; ++s) {
                for (const auto& p : m_spotterPool) {
                    if (spotterSolution[seqSpotterModel.workVars.at({p.name, s})] > 0.5) {
                        schedule[s].spotter = p.name;
                        break;
                    }
                }
            }
        }
    }
    
    // Build the final JSON output
    outputJson["solveDurationSeconds"] = solveDuration.count();
    json scheduleJson = json::array();
    bool hasSpotters = (m_ctx.spotterMode != SpotterMode::None);
    for(const auto& entry : schedule) {
        json entryJson;
        entryJson["stint"] = entry.stint;
        entryJson["startTimeUTC"] = entry.startTimeUTC;
        entryJson["endTimeUTC"] = entry.endTimeUTC;
        entryJson["laps"] = entry.laps;
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


ParticipantModel JresSolverImpl::add_participant_model(
    CbcModel &model,
    const std::vector<TeamMember> &participants,
    const std::string &prefix,
    double stintWithPitSeconds,
    int stintLaps)
{
    ParticipantModel p_model(prefix);
    if (participants.empty())
    {
        return p_model;
    }

    auto raceStartUTC = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);

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
        for (int s = 0; s < m_totalStints; ++s)
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

    for (int s = 0; s < m_totalStints; ++s)
    {
        auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
        std::string availabilityKey = TimeHelpers::timePointToKey(stintStart);

        for (const auto &p : participants)
        {
            if (m_ctx.raceData.availability.contains(p.name) &&
                m_ctx.raceData.availability[p.name].contains(availabilityKey) &&
                m_ctx.raceData.availability[p.name][availabilityKey] == "Preferred")
            {
                int workVarIdx = p_model.workVars.at({p.name, s});
                model.solver()->setObjCoeff(workVarIdx, -1.0);
            }
        }
    }

    // Add Constraints
    double totalLaps = m_totalStints * stintLaps;
    double equalShareLaps = totalLaps / participants.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    int minStintsPerParticipant = (stintLaps > 0) ? static_cast<int>(std::ceil(minLapsPerParticipant / stintLaps)) : 0;

    for (const auto &p : participants)
    {
        // Availability
        for (int s = 0; s < m_totalStints; ++s)
        {
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(stintWithPitSeconds));
            auto stintEndCheck = stintEnd - std::chrono::seconds(1);

            std::string startKey = TimeHelpers::timePointToKey(stintStart);
            std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);

            bool isAvailable = true;
            if (m_ctx.raceData.availability.contains(p.name))
            {
                auto p_avail = m_ctx.raceData.availability[p.name];
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
        for (int s = 1; s < m_totalStints; ++s)
        {
            CoinPackedVector row;
            row.insert(p_model.switchVars.at({p.name, s}), 1.0);
            row.insert(p_model.workVars.at({p.name, s}), -1.0);
            row.insert(p_model.workVars.at({p.name, s - 1}), 1.0);
            model.solver()->addRow(row, 0.0, COIN_DBL_MAX);
        }

        // Max/Min Stint Count
        CoinPackedVector totalStintsRow;
        for (int s = 0; s < m_totalStints; ++s)
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
        for (int s = 0; s < m_totalStints - maxConsecutive; ++s)
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
            
            if (minRestStints > 0 && minRestStints <= m_totalStints)
            {
                std::vector<int> restAchievedVars;
                CoinPackedVector oneRestRow;
                
                int possibleRestStarts = m_totalStints - minRestStints + 1;
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
