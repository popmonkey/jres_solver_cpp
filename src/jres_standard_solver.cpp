/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.cpp
 * @brief Standard solver implementation for JRES endurance race scheduling.
 */

#include "jres_standard_solver.hpp"

// --- COIN-OR Includes ---
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include "CbcStrategy.hpp" 
#include "CoinPackedVector.hpp"
#include "CoinBuild.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

JresStandardSolver::JresStandardSolver(const SolverContext& ctx)
    : JresSolverBase(ctx)
{
    m_mainSolver = std::make_unique<OsiClpSolverInterface>();
    m_mainSolver->setObjSense(1.0); // Minimize
    m_mainModel = std::make_unique<CbcModel>(*m_mainSolver);
    
    m_mainModel->setLogLevel(0);
    m_mainSolver->setLogLevel(0);
}

JresStandardSolver::~JresStandardSolver() = default;

json JresStandardSolver::solve()
{
    auto totalStart = std::chrono::high_resolution_clock::now();

    // --- 1. Build Driver Model ---
    ParticipantModel driverModel = add_participant_model(
        *m_mainModel, m_driverPool, "Drive", m_stintWithPitSeconds, m_stintLaps
    );

    CoinBuild coverageRows;

    // --- 2. Add Coverage Constraints (Batch) ---
    for (int s = 0; s < m_totalStints; ++s)
    {
        CoinPackedVector oneDriverRow;
        for (const auto &p : m_driverPool)
        {
            oneDriverRow.insert(driverModel.workVars.at({p.name, s}), 1.0);
        }
        coverageRows.addRow(oneDriverRow.getNumElements(), oneDriverRow.getIndices(), oneDriverRow.getElements(), 1.0, 1.0);
    }
    m_mainModel->solver()->addRows(coverageRows);

    // --- 3. Add First Stint Driver Constraint ---
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

    // --- 4. Add Spotter Model ---
    ParticipantModel spotterModel("Spot");

    if (m_ctx.spotterMode == SpotterMode::Integrated)
    {
        if (m_spotterPool.empty() && !m_ctx.allowNoSpotter) {
            throw std::runtime_error("Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
        }
        spotterModel = add_participant_model(
            *m_mainModel, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );
        
        CoinBuild spotterRows;
        for (int s = 0; s < m_totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : m_spotterPool) {
                row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (m_ctx.allowNoSpotter) {
                spotterRows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 0.0, 1.0);
            } else {
                spotterRows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 1.0, 1.0);
            }
        }
        // Exclusion
        for (const auto& p : m_ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < m_totalStints; ++s) {
                    CoinPackedVector row;
                    row.insert(driverModel.workVars.at({p.name, s}), 1.0);
                    row.insert(spotterModel.workVars.at({p.name, s}), 1.0);
                    spotterRows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 0.0, 1.0);
                }
            }
        }
        m_mainModel->solver()->addRows(spotterRows);
    }

    // --- 5. Solve Main Model ---
    auto mainSolveStart = std::chrono::high_resolution_clock::now();

    CbcStrategyDefault strategy;
    m_mainModel->setStrategy(strategy);
    
    // --- OPTIMIZATION: Temporal Branching Priorities ---
    // We prioritize variables by stint order (Stint 1 = Priority 1, Stint 2 = Priority 2, etc.)
    // This forces the solver to make decisions chronologically, preventing deep backtracking
    // from future constraints.
    int numCols = m_mainModel->solver()->getNumCols();
    int* priorities = new int[numCols];
    std::fill(priorities, priorities + numCols, 10000); // Default low priority for non-work vars

    for (const auto& p : m_driverPool) {
        for (int s = 0; s < m_totalStints; ++s) {
            // Work variables: Priority s + 1. 
            // Lower number = Higher priority to branch on.
            if (driverModel.workVars.count({p.name, s})) {
                 priorities[driverModel.workVars.at({p.name, s})] = s + 1;
            }
            // Integrated Spotter variables
            if (m_ctx.spotterMode == SpotterMode::Integrated && spotterModel.workVars.count({p.name, s})) {
                 priorities[spotterModel.workVars.at({p.name, s})] = s + 1;
            }
        }
    }
    // Pass ownership (true) of array to model
    m_mainModel->passInPriorities(priorities, true);

    m_mainModel->setDblParam(CbcModel::CbcAllowableFractionGap, m_ctx.optimalityGap);
    m_mainModel->setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(m_ctx.timeLimit));

    m_mainModel->branchAndBound();
    auto mainSolveEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> mainSolveDuration = mainSolveEnd - mainSolveStart;

    if (!m_mainModel->isProvenOptimal() && m_mainModel->isProvenInfeasible())
    {
        throw std::runtime_error("Model is infeasible. No solution exists.");
    }

    // --- 6. Process Results ---
    json outputJson;
    outputJson["raceData"] = m_ctx.raceData;

    std::vector<ScheduleEntry> schedule;
    const double* mainSolution = m_mainModel->solver()->getColSolution();
    
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
            if (mainSolution[driverModel.workVars.at({p.name, s})] > 0.5) {
                entry.driver = p.name;
                break;
            }
        }
        schedule.push_back(entry);
    }

    double spotterSolveDurationSeconds = 0.0;

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
        spotterCbcModel.setLogLevel(m_ctx.quiet ? 0 : 1);

        ParticipantModel seqSpotterModel = add_participant_model(
            spotterCbcModel, m_spotterPool, "Spot", m_stintWithPitSeconds, m_stintLaps
        );

        CoinBuild seqRows;
        for (int s = 0; s < m_totalStints; ++s) {
            CoinPackedVector row;
            for (const auto& p : m_spotterPool) {
                row.insert(seqSpotterModel.workVars.at({p.name, s}), 1.0);
            }
            if (m_ctx.allowNoSpotter) {
                seqRows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 0.0, 1.0);
            } else {
                seqRows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 1.0, 1.0);
            }
        }
        spotterCbcModel.solver()->addRows(seqRows);

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

        auto spotterStart = std::chrono::high_resolution_clock::now();
        
        CbcStrategyDefault spotterStrategy;
        spotterCbcModel.setStrategy(spotterStrategy);
        
        // --- Priorities for Sequential Spotter ---
        int sNumCols = spotterCbcModel.solver()->getNumCols();
        int* sPriorities = new int[sNumCols];
        std::fill(sPriorities, sPriorities + sNumCols, 10000);
        for (const auto& p : m_spotterPool) {
            for (int s = 0; s < m_totalStints; ++s) {
                if (seqSpotterModel.workVars.count({p.name, s})) {
                    sPriorities[seqSpotterModel.workVars.at({p.name, s})] = s + 1;
                }
            }
        }
        spotterCbcModel.passInPriorities(sPriorities, true);

        spotterCbcModel.setDblParam(CbcModel::CbcAllowableFractionGap, m_ctx.optimalityGap);
        spotterCbcModel.setDblParam(CbcModel::CbcMaximumSeconds, static_cast<double>(m_ctx.timeLimit));
        
        spotterCbcModel.branchAndBound();
        auto spotterEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> sd = spotterEnd - spotterStart;
        spotterSolveDurationSeconds = sd.count();

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
    
    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> totalDuration = totalEnd - totalStart;

    json metadata;
    metadata["options"]["timeLimit"] = m_ctx.timeLimit;
    metadata["options"]["quiet"] = m_ctx.quiet;
    metadata["options"]["allowNoSpotter"] = m_ctx.allowNoSpotter;
    metadata["options"]["optimalityGap"] = m_ctx.optimalityGap;
    
    std::string modeStr = "None";
    if (m_ctx.spotterMode == SpotterMode::Integrated) modeStr = "Integrated";
    if (m_ctx.spotterMode == SpotterMode::Sequential) modeStr = "Sequential";
    metadata["options"]["spotterMode"] = modeStr;

    metadata["timing"]["totalSeconds"] = totalDuration.count();
    metadata["timing"]["mainSolverSeconds"] = mainSolveDuration.count();
    
    if (m_ctx.spotterMode == SpotterMode::Sequential) {
        metadata["timing"]["spotterSolverSeconds"] = spotterSolveDurationSeconds;
    }

    outputJson["metadata"] = metadata;

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

ParticipantModel JresStandardSolver::add_participant_model(
    CbcModel &model,
    const std::vector<TeamMember> &participants,
    const std::string &prefix,
    double stintWithPitSeconds,
    int stintLaps)
{
    ParticipantModel p_model(prefix);
    if (participants.empty()) return p_model;

    CoinBuild rows;
    auto raceStartUTC = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);

    // --- 1. FULLY BATCHED COLUMN CREATION ---
    int nParticipants = static_cast<int>(participants.size());
    int nStints = m_totalStints;
    
    int totalNewCols = 0;
    
    struct ParticipantVars {
        int workStartIdx;
        int switchStartIdx;
        int restStartIdx;
        int minRestStints;
        int possibleRestStarts;
    };
    std::vector<ParticipantVars> pVars(nParticipants);

    for (int i = 0; i < nParticipants; ++i) {
        const auto& p = participants[i];
        pVars[i].workStartIdx = totalNewCols;
        totalNewCols += nStints; 
        
        pVars[i].switchStartIdx = totalNewCols;
        totalNewCols += (nStints - 1); 

        // Calculate Rest Vars needed
        int minRestHours = p.minimumRestHours;
        pVars[i].minRestStints = 0;
        pVars[i].possibleRestStarts = 0;
        pVars[i].restStartIdx = -1;

        if (minRestHours > 0 && stintWithPitSeconds > 0) {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / stintWithPitSeconds));
            if (minRestStints > 0 && minRestStints <= nStints) {
                pVars[i].minRestStints = minRestStints;
                pVars[i].possibleRestStarts = nStints - minRestStints + 1;
                pVars[i].restStartIdx = totalNewCols;
                totalNewCols += pVars[i].possibleRestStarts;
            }
        }
    }

    std::vector<double> colLb(totalNewCols, 0.0);
    std::vector<double> colUb(totalNewCols, 1.0);
    std::vector<double> colObj(totalNewCols, 0.0);
    std::vector<int> colStarts(totalNewCols + 1, 0); 

    int baseColIdx = model.solver()->getNumCols();

    // Fill Vectors & Map indices
    for (int i = 0; i < nParticipants; ++i) {
        const auto& p = participants[i];
        
        // --- OPTIMIZATION: Symmetry Breaking ---
        // Add tiny perturbation based on driver index 'i'.
        // This ensures Driver[0] is mathematically distinct from Driver[1] 
        // even if their data is identical, reducing swap-search overhead.
        double symmetryNoise = i * 1.0e-5; 

        // Map Work Vars
        for (int s = 0; s < nStints; ++s) {
            int localIdx = pVars[i].workStartIdx + s;
            p_model.workVars[{p.name, s}] = baseColIdx + localIdx;
            
            // Preference Cost + Symmetry Breaking
            double cost = 0.0;
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * stintWithPitSeconds));
            std::string availabilityKey = TimeHelpers::timePointToKey(stintStart);
            if (m_ctx.raceData.availability.contains(p.name) &&
                m_ctx.raceData.availability[p.name].contains(availabilityKey) &&
                m_ctx.raceData.availability[p.name][availabilityKey] == "Preferred")
            {
                cost = -1.0;
            }
            colObj[localIdx] = cost + symmetryNoise;
        }

        // Map Switch Vars
        for (int s = 1; s < nStints; ++s) {
            int localIdx = pVars[i].switchStartIdx + (s - 1);
            p_model.switchVars[{p.name, s}] = baseColIdx + localIdx;
            colObj[localIdx] = 100.0; 
        }
    }

    model.solver()->addCols(totalNewCols, colStarts.data(), nullptr, nullptr, colLb.data(), colUb.data(), colObj.data());

    for(int i = 0; i < totalNewCols; ++i) {
        model.solver()->setInteger(baseColIdx + i);
    }

    // --- 2. ADD CONSTRAINTS (Rows) ---

    // Calculate fairness bounds
    double totalLaps = m_totalStints * stintLaps;
    double equalShareLaps = totalLaps / participants.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    double minStintsFloat = (stintLaps > 0) ? (double)minLapsPerParticipant / stintLaps : 0.0;
    int minStintsPerParticipant = static_cast<int>(std::ceil(minStintsFloat));
    if (minStintsPerParticipant * participants.size() > m_totalStints) minStintsPerParticipant = 0;
    double equalShareStints = (double)m_totalStints / participants.size();
    int maxStintsPerParticipant = static_cast<int>(std::ceil(equalShareStints)) + 3;

    for (int i = 0; i < nParticipants; ++i)
    {
        const auto& p = participants[i];

        // Availability
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
                model.solver()->setColBounds(p_model.workVars.at({p.name, s}), 0.0, 0.0);
            }
        }

        // Switch Constraints
        for (int s = 1; s < m_totalStints; ++s) {
            CoinPackedVector row;
            row.insert(p_model.switchVars.at({p.name, s}), 1.0);
            row.insert(p_model.workVars.at({p.name, s}), -1.0);
            row.insert(p_model.workVars.at({p.name, s - 1}), 1.0);
            rows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 0.0, COIN_DBL_MAX);
        }

        // Fairness Constraints
        CoinPackedVector totalStintsRow;
        for (int s = 0; s < m_totalStints; ++s) totalStintsRow.insert(p_model.workVars.at({p.name, s}), 1.0);
        
        double lowerBound = (prefix == "Drive") ? (double)minStintsPerParticipant : 0.0;
        double upperBound = (double)maxStintsPerParticipant;
        if (upperBound < lowerBound) upperBound = lowerBound;
        
        rows.addRow(totalStintsRow.getNumElements(), totalStintsRow.getIndices(), totalStintsRow.getElements(), lowerBound, upperBound);

        // Max Consecutive
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
            CoinPackedVector consecutiveRow;
            for (int i = 0; i <= maxConsecutive; ++i) {
                consecutiveRow.insert(p_model.workVars.at({p.name, s + i}), 1.0);
            }
            rows.addRow(consecutiveRow.getNumElements(), consecutiveRow.getIndices(), consecutiveRow.getElements(), -COIN_DBL_MAX, maxConsecutive);
        }

        if (pVars[i].possibleRestStarts > 0) {
            CoinPackedVector oneRestRow;
            
            for (int s = 0; s < pVars[i].possibleRestStarts; ++s) {
                // Determine absolute index of the pre-created rest variable
                int restVarIdx = baseColIdx + pVars[i].restStartIdx + s;
                
                // Add to "Must rest at least once" row
                oneRestRow.insert(restVarIdx, 1.0);

                // Add "If Rest, No Work" constraint (Big M)
                CoinPackedVector enforceRestRow;
                for (int k = 0; k < pVars[i].minRestStints; ++k) {
                    enforceRestRow.insert(p_model.workVars.at({p.name, s + k}), 1.0);
                }
                double M = pVars[i].minRestStints + 1;
                enforceRestRow.insert(restVarIdx, M);
                rows.addRow(enforceRestRow.getNumElements(), enforceRestRow.getIndices(), enforceRestRow.getElements(), -COIN_DBL_MAX, M);
            }

            // Ensure at least one rest period is taken
            if (oneRestRow.getNumElements() > 0) {
                rows.addRow(oneRestRow.getNumElements(), oneRestRow.getIndices(), oneRestRow.getElements(), 1.0, COIN_DBL_MAX);
            }
        }
    } 
    model.solver()->addRows(rows);
    return p_model;
}
