/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.cpp
 * @brief Standard solver implementation for JRES endurance race scheduling.
 */

#include "jres_standard_solver.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

// HiGHS is accessed via a single header
#include "Highs.h"

JresStandardSolver::JresStandardSolver(const SolverContext& ctx)
    : JresSolverBase(ctx)
{
    m_highs = std::make_unique<Highs>();
    
    // Set HiGHS Options
    m_highs->setOptionValue("output_flag", false); // Equivalent to setLogLevel(0)
    m_highs->setOptionValue("presolve", "on");
    
    // Time Limit
    if (m_ctx.timeLimit > 0) {
        m_highs->setOptionValue("time_limit", static_cast<double>(m_ctx.timeLimit));
    }

    // GAP (mip_rel_gap)
    m_highs->setOptionValue("mip_rel_gap", m_ctx.optimalityGap);
}

JresStandardSolver::~JresStandardSolver() = default;

json JresStandardSolver::solve()
{
    using namespace std::chrono;
    auto startTotal = high_resolution_clock::now();
    ComplexityMetrics metrics;

    double setupDurationMs = 0.0;
    double driverSolveDurationMs = 0.0;
    double spotterSolveDurationMs = 0.0;

    // --- Build Driver Model ---
    ParticipantModel driverModel = add_participant_model(
        *m_highs, m_driverPool, "Drive", m_stintWithPitSeconds, m_stintLaps
    );

    // --- Add Coverage Constraints (One driver per stint) ---
    for (int s = 0; s < m_totalStints; ++s)
    {
        std::vector<int> indices;
        std::vector<double> values;
        for (const auto &p : m_driverPool)
        {
            indices.push_back(driverModel.workVars.at({p.name, s}));
            values.push_back(1.0);
        }
        // sum(drivers) == 1.0
        m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
    }

    // --- 3. Add First Stint Driver Constraint ---
    if (!m_ctx.raceData.firstStintDriver.empty())
    {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), 
                             [&](const TeamMember& m){ return m.name == firstName; });
        if (it != m_driverPool.end())
        {
            int varIdx = driverModel.workVars.at({firstName, 0});
            // Fix variable bounds to 1.0
            m_highs->changeColBounds(varIdx, 1.0, 1.0);
        }
    }

    // --- 4. Add Spotter Model (Integrated Mode) ---
    ParticipantModel spotterModel("Spot");

    if (m_ctx.spotterMode == SpotterMode::Integrated)
    {
        if (m_spotterPool.empty() && !m_ctx.allowNoSpotter) {
            throw std::runtime_error("Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
        }
        spotterModel = add_participant_model(
            *m_highs, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );

        // Spotter Coverage
        for (int s = 0; s < m_totalStints; ++s) {
            std::vector<int> indices;
            std::vector<double> values;
            for (const auto& p : m_spotterPool) {
                indices.push_back(spotterModel.workVars.at({p.name, s}));
                values.push_back(1.0);
            }
            if (m_ctx.allowNoSpotter) {
                m_highs->addRow(0.0, 1.0, (int)indices.size(), indices.data(), values.data());
            } else {
                m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
            }
        }

        // Driver cannot spot for themselves simultaneously
        for (const auto& p : m_ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < m_totalStints; ++s) {
                    std::vector<int> idx = {
                        driverModel.workVars.at({p.name, s}),
                        spotterModel.workVars.at({p.name, s})
                    };
                    std::vector<double> val = {1.0, 1.0};
                    // driver + spotter <= 1
                    m_highs->addRow(0.0, 1.0, 2, idx.data(), val.data());
                }
            }
        }
    }

    // --- Metric Calculation (Static) ---
    
    metrics.modelRows = m_highs->getNumRows();
    metrics.modelColumns = m_highs->getNumCol();

    auto endSetup = high_resolution_clock::now();
    setupDurationMs = duration<double, std::milli>(endSetup - startTotal).count();

    // --- 5. Solve (Driver/Integrated) ---
    auto solveStart = high_resolution_clock::now();
    
    // HiGHS run() handles the full solve process
    m_highs->run();
    
    auto solveEnd = high_resolution_clock::now();
    driverSolveDurationMs = duration<double, std::milli>(solveEnd - solveStart).count();

    // --- Metric Calculation (Dynamic) ---
    const HighsInfo& info = m_highs->getInfo();
    metrics.searchNodes = (int)info.mip_node_count; // int64_t to int
    metrics.finalGap = info.mip_gap; // Relative gap

    HighsModelStatus status = m_highs->getModelStatus();
    if (status != HighsModelStatus::kOptimal && status != HighsModelStatus::kTimeLimit) {
        // Strict check: if it's infeasible or unbounded
        if (status == HighsModelStatus::kInfeasible) {
            throw std::runtime_error("Model is infeasible. No solution exists.");
        }
        // Note: HiGHS might return kTimeLimit but still have a valid feasible solution
    }

    // --- 6. Process Results ---
    json outputJson;
    json metaJson;
    to_json(metaJson, m_ctx);
    outputJson["metadata"] = metaJson;
    
    json complexityJson;
    to_json(complexityJson, metrics);
    outputJson["complexity"] = complexityJson;
    outputJson["raceData"] = m_ctx.raceData;

    std::vector<ScheduleEntry> schedule;
    
    // Get solution vector
    const auto& solution = m_highs->getSolution();
    const std::vector<double>& colValues = solution.col_value;

    auto currentTimePoint = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);
    double stintDurationSeconds = m_stintLaps * m_ctx.raceData.avgLapTimeInSeconds;
    double pitSeconds = m_ctx.raceData.pitTimeInSeconds;

    for (int s = 0; s < m_totalStints; ++s) {
        ScheduleEntry entry;
        entry.stint = s + 1;
        entry.driver = "N/A";
        entry.spotter = "N/A";
        entry.laps = m_stintLaps;
        
        entry.startTimeUTC = TimeHelpers::timePointToString(currentTimePoint);
        auto endTimePoint = currentTimePoint + std::chrono::seconds(static_cast<long>(stintDurationSeconds));
        entry.endTimeUTC = TimeHelpers::timePointToString(endTimePoint);
        currentTimePoint = endTimePoint + std::chrono::seconds(static_cast<long>(pitSeconds));

        for (const auto& p : m_driverPool) {
            if (colValues[driverModel.workVars.at({p.name, s})] > 0.5) {
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
                if (colValues[spotterModel.workVars.at({p.name, s})] > 0.5) {
                    schedule[s].spotter = p.name;
                    break;
                }
            }
        }
    } 
    else if (m_ctx.spotterMode == SpotterMode::Sequential && !m_spotterPool.empty())
    {
        auto spotterStart = high_resolution_clock::now();

        // Create a separate instance for the sequential spotter solve
        Highs spotterSolver;
        spotterSolver.setOptionValue("output_flag", false);
        spotterSolver.setOptionValue("mip_rel_gap", m_ctx.optimalityGap);
        if (m_ctx.timeLimit > 0) spotterSolver.setOptionValue("time_limit", static_cast<double>(m_ctx.timeLimit));

        ParticipantModel seqSpotterModel = add_participant_model(
            spotterSolver, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );

        // Coverage Constraints for sequential
        for (int s = 0; s < m_totalStints; ++s) {
            std::vector<int> idx;
            std::vector<double> val;
            for (const auto& p : m_spotterPool) {
                idx.push_back(seqSpotterModel.workVars.at({p.name, s}));
                val.push_back(1.0);
            }
            double lb = m_ctx.allowNoSpotter ? 0.0 : 1.0;
            spotterSolver.addRow(lb, 1.0, (int)idx.size(), idx.data(), val.data());
        }

        // Lock out the driver who is driving from spotting
        for (int s = 0; s < m_totalStints; ++s) {
            const std::string& driverName = schedule[s].driver;
            if (driverName == "N/A") continue;
            auto it = std::find_if(m_spotterPool.begin(), m_spotterPool.end(), 
                                 [&](const TeamMember& m){ return m.name == driverName; });
            if (it != m_spotterPool.end()) {
                int varIdx = seqSpotterModel.workVars.at({driverName, s});
                spotterSolver.changeColBounds(varIdx, 0.0, 0.0);
            }
        }

        spotterSolver.run();

        const auto& spotterSol = spotterSolver.getSolution();
        if (spotterSolver.getModelStatus() != HighsModelStatus::kInfeasible) {
             for (int s = 0; s < m_totalStints; ++s) {
                for (const auto& p : m_spotterPool) {
                    if (spotterSol.col_value[seqSpotterModel.workVars.at({p.name, s})] > 0.5) {
                        schedule[s].spotter = p.name;
                        break;
                    }
                }
            }
        }

        auto spotterEnd = high_resolution_clock::now();
        spotterSolveDurationMs = duration<double, std::milli>(spotterEnd - spotterStart).count();
    }
    
    auto endTotal = high_resolution_clock::now();
    double totalDurationSeconds = duration<double>(endTotal - startTotal).count();

    // Timing JSON
    json timingJson;
    timingJson["setupMs"] = setupDurationMs;
    timingJson["driverSolveMs"] = driverSolveDurationMs;
    if (m_ctx.spotterMode == SpotterMode::Sequential) {
        timingJson["spotterSolveMs"] = spotterSolveDurationMs;
    }
    timingJson["totalSeconds"] = totalDurationSeconds;

    outputJson["timing"] = timingJson;
    json scheduleJson = json::array();
    bool hasSpotters = (m_ctx.spotterMode != SpotterMode::None);
    for(const auto& entry : schedule) {
        json entryJson;
        entryJson["stint"] = entry.stint;
        entryJson["startTimeUTC"] = entry.startTimeUTC;
        entryJson["endTimeUTC"] = entry.endTimeUTC;
        entryJson["laps"] = entry.laps;
        entryJson["driver"] = entry.driver;
        if (hasSpotters) entryJson["spotter"] = entry.spotter;
        scheduleJson.push_back(entryJson);
    }
    outputJson["schedule"] = scheduleJson;
    outputJson["success"] = true;
    return outputJson;
}

ParticipantModel JresStandardSolver::add_participant_model(
    Highs &highs,
    const std::vector<TeamMember> &participants,
    const std::string &prefix,
    double stintWithPitSeconds,
    int stintLaps)
{
    ParticipantModel p_model(prefix);
    if (participants.empty()) return p_model;

    auto raceStartUTC = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);

    // Max/Min Work Stint Variables
    // Highs: addVar(lb, ub) -> returns status, but index is implicit (current num cols)
    
    p_model.maxWorkStintsVar = highs.getNumCol();
    highs.addVar(0.0, kHighsInf);
    highs.changeColCost(p_model.maxWorkStintsVar, 1000.0);
    highs.changeColIntegrality(p_model.maxWorkStintsVar, HighsVarType::kInteger);

    p_model.minWorkStintsVar = highs.getNumCol();
    highs.addVar(0.0, kHighsInf);
    highs.changeColCost(p_model.minWorkStintsVar, -1000.0);
    highs.changeColIntegrality(p_model.minWorkStintsVar, HighsVarType::kInteger);

    for (const auto &p : participants)
    {
        for (int s = 0; s < m_totalStints; ++s)
        {
            int workVarIdx = highs.getNumCol();
            p_model.workVars[{p.name, s}] = workVarIdx;
            
            // Calculate Preference Cost
            double cost = 0.0;
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            std::string availabilityKey = TimeHelpers::timePointToKey(stintStart);
            if (m_ctx.raceData.availability.contains(p.name) &&
                m_ctx.raceData.availability[p.name].contains(availabilityKey) &&
                m_ctx.raceData.availability[p.name][availabilityKey] == "Preferred")
            {
                cost = -1.0;
            }

            highs.addVar(0.0, 1.0); // Binary
            highs.changeColCost(workVarIdx, cost);
            highs.changeColIntegrality(workVarIdx, HighsVarType::kInteger);

            if (s > 0)
            {
                int switchVarIdx = highs.getNumCol();
                p_model.switchVars[{p.name, s}] = switchVarIdx;
                highs.addVar(0.0, 1.0);
                highs.changeColCost(switchVarIdx, 100.0);
                highs.changeColIntegrality(switchVarIdx, HighsVarType::kInteger);
            }
        }
    }

    // Constraints logic
    double totalLaps = m_totalStints * stintLaps;
    double equalShareLaps = totalLaps / participants.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    double minStintsFloat = (stintLaps > 0) ? (double)minLapsPerParticipant / stintLaps : 0.0;
    int minStintsPerParticipant = static_cast<int>(std::ceil(minStintsFloat));
    if (minStintsPerParticipant * participants.size() > m_totalStints) minStintsPerParticipant = 0;

    for (const auto &p : participants)
    {
        // Availability Hard Constraints
        for (int s = 0; s < m_totalStints; ++s) {
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(stintWithPitSeconds));
            auto stintEndCheck = stintEnd - std::chrono::seconds(1);
            std::string startKey = TimeHelpers::timePointToKey(stintStart);
            std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);

            bool isAvailable = true;
            if (m_ctx.raceData.availability.contains(p.name)) {
                auto p_avail = m_ctx.raceData.availability[p.name];
                if (p_avail.value(startKey, "Unavailable") == "Unavailable" ||
                    p_avail.value(endKey, "Unavailable") == "Unavailable") isAvailable = false;
            } else { isAvailable = false; }

            if (!isAvailable) {
                highs.changeColBounds(p_model.workVars.at({p.name, s}), 0.0, 0.0);
            }
        }

        // Switch Constraints: Switch[s] >= Work[s] - Work[s-1]
        // Rearranged: Switch[s] - Work[s] + Work[s-1] >= 0
        for (int s = 1; s < m_totalStints; ++s) {
            std::vector<int> idx = {
                p_model.switchVars.at({p.name, s}),
                p_model.workVars.at({p.name, s}),
                p_model.workVars.at({p.name, s - 1})
            };
            std::vector<double> val = {1.0, -1.0, 1.0};
            highs.addRow(0.0, kHighsInf, 3, idx.data(), val.data());
        }

        // Max/Min Stints Linking
        // Sum(Work) - Max <= 0
        // Sum(Work) - Min >= 0
        std::vector<int> totalStintsIdx;
        std::vector<double> totalStintsVal;
        for (int s = 0; s < m_totalStints; ++s) {
            totalStintsIdx.push_back(p_model.workVars.at({p.name, s}));
            totalStintsVal.push_back(1.0);
        }
        
        // Max Row
        {
            auto idx = totalStintsIdx; 
            idx.push_back(p_model.maxWorkStintsVar);
            auto val = totalStintsVal; 
            val.push_back(-1.0);
            highs.addRow(-kHighsInf, 0.0, (int)idx.size(), idx.data(), val.data());
        }

        // Min Row
        {
            auto idx = totalStintsIdx; 
            idx.push_back(p_model.minWorkStintsVar);
            auto val = totalStintsVal; 
            val.push_back(-1.0);
            highs.addRow(0.0, kHighsInf, (int)idx.size(), idx.data(), val.data());
        }

        // Absolute Minimum Floor
        if (prefix == "Drive" && minStintsPerParticipant > 0) {
            highs.addRow(minStintsPerParticipant, kHighsInf, (int)totalStintsIdx.size(), totalStintsIdx.data(), totalStintsVal.data());
        }

        // Max Consecutive
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
            std::vector<int> consIdx;
            std::vector<double> consVal;
            for (int i = 0; i <= maxConsecutive; ++i) {
                consIdx.push_back(p_model.workVars.at({p.name, s + i}));
                consVal.push_back(1.0);
            }
            // Sum of window must be <= maxConsecutive
            highs.addRow(-kHighsInf, maxConsecutive, (int)consIdx.size(), consIdx.data(), consVal.data());
        }

        // Minimum Rest (Big-M)
        int minRestHours = p.minimumRestHours;
        if (minRestHours > 0 && stintWithPitSeconds > 0) {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / stintWithPitSeconds));
            if (minRestStints > 0 && minRestStints <= m_totalStints) {
                std::vector<int> restAchievedVars;
                std::vector<double> restAchievedVals;
                
                int possibleRestStarts = m_totalStints - minRestStints + 1;
                for (int s = 0; s < possibleRestStarts; ++s) {
                    int restVarIdx = highs.getNumCol();
                    restAchievedVars.push_back(restVarIdx);
                    restAchievedVals.push_back(1.0);

                    highs.addVar(0.0, 1.0); // Binary indicator
                    highs.changeColIntegrality(restVarIdx, HighsVarType::kInteger);

                    // Constraint: Sum(Work in window) + M * RestVar <= M
                    std::vector<int> enforceIdx;
                    std::vector<double> enforceVal;
                    for (int i = 0; i < minRestStints; ++i) {
                        enforceIdx.push_back(p_model.workVars.at({p.name, s + i}));
                        enforceVal.push_back(1.0);
                    }
                    double M = minRestStints + 1;
                    enforceIdx.push_back(restVarIdx);
                    enforceVal.push_back(M);
                    
                    highs.addRow(-kHighsInf, M, (int)enforceIdx.size(), enforceIdx.data(), enforceVal.data());
                }
                
                // Must have at least one rest period
                if (!restAchievedVars.empty()) {
                    highs.addRow(1.0, kHighsInf, (int)restAchievedVars.size(), restAchievedVars.data(), restAchievedVals.data());
                }
            }
        }
    } 
    return p_model;
}
