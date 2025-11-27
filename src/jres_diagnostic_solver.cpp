/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_diagnostic_solver.cpp
 * @brief Diagnostic solver implementation for JRES endurance race scheduling.
 */

#include "jres_diagnostic_solver.hpp"
#include "Highs.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <set>
#include <map>
#include <numeric>

// --- Helper for formatting stint ranges (e.g. "1, 2, 3" -> "1-3") ---
static std::string formatStintList(std::vector<int>& stints) {
    if (stints.empty()) return "";
    std::sort(stints.begin(), stints.end());
    stints.erase(std::unique(stints.begin(), stints.end()), stints.end());
    
    std::stringstream ss;
    int rangeStart = stints[0];
    int prev = stints[0];
    
    for (size_t i = 1; i < stints.size(); ++i) {
        if (stints[i] != prev + 1) {
            if (rangeStart == prev) ss << rangeStart << ", ";
            else ss << rangeStart << "-" << prev << ", ";
            rangeStart = stints[i];
        }
        prev = stints[i];
    }
    if (rangeStart == prev) ss << rangeStart;
    else ss << rangeStart << "-" << prev;
    
    return ss.str();
}

JresDiagnosticSolver::JresDiagnosticSolver(const SolverContext& ctx)
    : JresSolverBase(ctx)
{
}

JresDiagnosticSolver::~JresDiagnosticSolver() = default;

json JresDiagnosticSolver::diagnose()
{
    Highs solver;
    solver.setOptionValue("output_flag", false);
    
    // We strictly minimize penalties
    // HiGHS minimizes by default, but we can be explicit if needed.
    // (No setObjSense needed, default is minimize)

    std::map<std::pair<std::string, int>, int> assignVars;
    auto raceStartUTC = TimeHelpers::stringToTimePoint(m_ctx.raceData.raceStartUTC);

    // --- Helper: Centralized Availability Logic ---
    auto isDriverAvailable = [&](const std::string& name, int s) -> bool {
        auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * m_stintWithPitSeconds));
        auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(m_stintWithPitSeconds));
        auto stintEndCheck = stintEnd - std::chrono::seconds(1);
        
        std::string startKey = TimeHelpers::timePointToKey(stintStart);
        std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);
        
        if (m_ctx.raceData.availability.contains(name)) {
            if (m_ctx.raceData.availability[name].value(startKey, "Unavailable") == "Unavailable") return false;
            if (m_ctx.raceData.availability[name].value(endKey, "Unavailable") == "Unavailable") return false;
            return true;
        }
        return false;
    };

    // =========================================================
    // BUILD DIAGNOSTIC MODEL
    // =========================================================

    std::map<std::pair<std::string, int>, int> spotterAssignVars;

    // --- Driver Assignment Variables ---
    for (const auto& p : m_driverPool) {
        for (int s = 0; s < m_totalStints; ++s) {
            int idx = solver.getNumCol();
            assignVars[{p.name, s}] = idx;
            
            // Add binary variable [0, 1]
            solver.addVar(0.0, 1.0);
            solver.changeColIntegrality(idx, HighsVarType::kInteger);
            
            // If unavailable, add penalty to objective immediately
            if (!isDriverAvailable(p.name, s)) {
                solver.changeColCost(idx, 100000.0); // High cost penalty
            } else {
                solver.changeColCost(idx, 0.0); 
            }
        }
    }

    // --- Spotter Assignment Variables (Integrated Mode) ---
    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            for (int s = 0; s < m_totalStints; ++s) {
                int idx = solver.getNumCol();
                spotterAssignVars[{p.name, s}] = idx;
                
                solver.addVar(0.0, 1.0);
                solver.changeColIntegrality(idx, HighsVarType::kInteger);
                
                if (!isDriverAvailable(p.name, s)) {
                    solver.changeColCost(idx, 100000.0);
                } else {
                    solver.changeColCost(idx, 0.0);
                }
            }
        }
    }

    // --- Coverage Constraints (Exactly 1 Driver) ---
    for (int s = 0; s < m_totalStints; ++s) {
        std::vector<int> idx;
        std::vector<double> val;

        for (const auto& p : m_driverPool) {
            idx.push_back(assignVars.at({p.name, s}));
            val.push_back(1.0);
        }

        // Slack: Missing Driver (Allows sum to be 0 at high cost)
        // equation: sum(drivers) + missing - extra = 1
        // missing: cost 1M
        int missingIdx = solver.getNumCol();
        solver.addVar(0.0, 1.0);
        solver.changeColIntegrality(missingIdx, HighsVarType::kInteger);
        solver.changeColCost(missingIdx, 1000000.0);
        idx.push_back(missingIdx);
        val.push_back(1.0);

        // Slack: Extra Driver (Allows sum > 1 at high cost)
        // extra: cost 500k
        int extraIdx = solver.getNumCol();
        solver.addVar(0.0, 5.0); // Allow up to 5 extra drivers overlap?
        solver.changeColIntegrality(extraIdx, HighsVarType::kInteger);
        solver.changeColCost(extraIdx, 500000.0);
        idx.push_back(extraIdx);
        val.push_back(-1.0);

        // Row == 1.0
        solver.addRow(1.0, 1.0, (int)idx.size(), idx.data(), val.data());
    }

    // --- Spotter Coverage Constraints (Integrated Mode) ---
    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (int s = 0; s < m_totalStints; ++s) {
            std::vector<int> idx;
            std::vector<double> val;

            for (const auto& p : m_spotterPool) {
                idx.push_back(spotterAssignVars.at({p.name, s}));
                val.push_back(1.0);
            }

            // Slack for missing spotter
            int missingSpotterIdx = solver.getNumCol();
            solver.addVar(0.0, 1.0);
            solver.changeColIntegrality(missingSpotterIdx, HighsVarType::kInteger);
            double missingCost = m_ctx.allowNoSpotter ? 50000.0 : 1000000.0;
            solver.changeColCost(missingSpotterIdx, missingCost);
            idx.push_back(missingSpotterIdx);
            val.push_back(1.0);

            // Slack for extra spotter
            int extraSpotterIdx = solver.getNumCol();
            solver.addVar(0.0, 5.0);
            solver.changeColIntegrality(extraSpotterIdx, HighsVarType::kInteger);
            solver.changeColCost(extraSpotterIdx, 500000.0);
            idx.push_back(extraSpotterIdx);
            val.push_back(-1.0);

            double lb = m_ctx.allowNoSpotter ? 0.0 : 1.0;
            solver.addRow(lb, 1.0, (int)idx.size(), idx.data(), val.data());
        }

        // Driver cannot spot for themselves simultaneously
        for (const auto& p : m_ctx.raceData.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (int s = 0; s < m_totalStints; ++s) {
                    int slackIdx = solver.getNumCol();
                    solver.addVar(0.0, 1.0);
                    solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                    solver.changeColCost(slackIdx, 500000.0);

                    std::vector<int> idx = {
                        assignVars.at({p.name, s}),
                        spotterAssignVars.at({p.name, s}),
                        slackIdx
                    };
                    std::vector<double> val = {1.0, 1.0, -1.0};
                    // driver + spotter - slack <= 1
                    solver.addRow(-kHighsInf, 1.0, 3, idx.data(), val.data());
                }
            }
        }
    }

    // --- Max Consecutive Stints ---
    for (const auto& p : m_driverPool) {
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
            std::vector<int> idx;
            std::vector<double> val;
            
            for (int i = 0; i <= maxConsecutive; ++i) {
                idx.push_back(assignVars.at({p.name, s + i}));
                val.push_back(1.0);
            }

            // Slack Variable
            int slackIdx = solver.getNumCol();
            solver.addVar(0.0, (double)(maxConsecutive + 1));
            solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
            solver.changeColCost(slackIdx, 10000.0);

            // Constraint: Sum(window) - Slack <= Max
            idx.push_back(slackIdx);
            val.push_back(-1.0);
            
            solver.addRow(-kHighsInf, maxConsecutive, (int)idx.size(), idx.data(), val.data());
        }
    }
    
    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            int maxConsecutive = p.preferredStints;
            for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
                std::vector<int> idx;
                std::vector<double> val;
                
                for (int i = 0; i <= maxConsecutive; ++i) {
                    idx.push_back(spotterAssignVars.at({p.name, s + i}));
                    val.push_back(1.0);
                }

                int slackIdx = solver.getNumCol();
                solver.addVar(0.0, (double)(maxConsecutive + 1));
                solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                solver.changeColCost(slackIdx, 10000.0);

                idx.push_back(slackIdx);
                val.push_back(-1.0);
                
                solver.addRow(-kHighsInf, maxConsecutive, (int)idx.size(), idx.data(), val.data());
            }
        }
    }

    // --- Fair Share ---
    double totalLaps = m_totalStints * m_stintLaps;
    double equalShareLaps = totalLaps / m_driverPool.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    double minStintsFloat = (m_stintLaps > 0) ? (double)minLapsPerParticipant / m_stintLaps : 0.0;
    int minStintsPerParticipant = static_cast<int>(std::ceil(minStintsFloat));
    
    if (minStintsPerParticipant * m_driverPool.size() > m_totalStints) minStintsPerParticipant = 0;

    if (minStintsPerParticipant > 0) {
        for (const auto &p : m_driverPool) {
            std::vector<int> idx;
            std::vector<double> val;
            
            for (int s = 0; s < m_totalStints; ++s) {
                idx.push_back(assignVars.at({p.name, s}));
                val.push_back(1.0);
            }

            int slackIdx = solver.getNumCol();
            solver.addVar(0.0, (double)m_totalStints);
            solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
            solver.changeColCost(slackIdx, 50000.0);

            // Constraint: Sum(TotalStints) + Slack >= Min
            idx.push_back(slackIdx);
            val.push_back(1.0);
            
            solver.addRow(minStintsPerParticipant, kHighsInf, (int)idx.size(), idx.data(), val.data());
        }
    }

    // --- Minimum Rest ---
    for (const auto &p : m_driverPool) {
        int minRestHours = p.minimumRestHours;
        if (minRestHours > 0 && m_stintWithPitSeconds > 0) {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / m_stintWithPitSeconds));
            if (minRestStints > 0) {
                int possibleRestStarts = m_totalStints - minRestStints + 1;
                for (int s = 0; s < possibleRestStarts; ++s) {
                    for (int k = 1; k <= minRestStints; ++k) {
                        if (s + k < m_totalStints) {
                            std::vector<int> idx;
                            std::vector<double> val;
                            
                            idx.push_back(assignVars.at({p.name, s}));
                            val.push_back(1.0);
                            
                            idx.push_back(assignVars.at({p.name, s + k}));
                            val.push_back(1.0);

                            int slackIdx = solver.getNumCol();
                            solver.addVar(0.0, 1.0);
                            solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                            solver.changeColCost(slackIdx, 50000.0);

                            // Constraint: Stint[s] + Stint[s+k] - Slack <= 1
                            idx.push_back(slackIdx);
                            val.push_back(-1.0);
                            
                            solver.addRow(-kHighsInf, 1.0, (int)idx.size(), idx.data(), val.data());
                        }
                    }
                }
            }
        }
    }
    
    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (const auto &p : m_spotterPool) {
            int minRestHours = p.minimumRestHours;
            if (minRestHours > 0 && m_stintWithPitSeconds > 0) {
                int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / m_stintWithPitSeconds));
                if (minRestStints > 0) {
                    int possibleRestStarts = m_totalStints - minRestStints + 1;
                    for (int s = 0; s < possibleRestStarts; ++s) {
                        for (int k = 1; k <= minRestStints; ++k) {
                            if (s + k < m_totalStints) {
                                std::vector<int> idx;
                                std::vector<double> val;
                                
                                idx.push_back(spotterAssignVars.at({p.name, s}));
                                val.push_back(1.0);
                                
                                idx.push_back(spotterAssignVars.at({p.name, s + k}));
                                val.push_back(1.0);

                                int slackIdx = solver.getNumCol();
                                solver.addVar(0.0, 1.0);
                                solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                                solver.changeColCost(slackIdx, 50000.0);

                                idx.push_back(slackIdx);
                                val.push_back(-1.0);
                                
                                solver.addRow(-kHighsInf, 1.0, (int)idx.size(), idx.data(), val.data());
                            }
                        }
                    }
                }
            }
        }
    }
    
    // --- First Stint Driver ---
    if (!m_ctx.raceData.firstStintDriver.empty()) {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), 
                                [&](const TeamMember& m){ return m.name == firstName; });
        if (it != m_driverPool.end()) {
            int workVarIdx = assignVars.at({firstName, 0});
            int slackIdx = solver.getNumCol();
            
            solver.addVar(0.0, 1.0);
            solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
            solver.changeColCost(slackIdx, 80000.0);

            // Constraint: WorkVar + Slack >= 1
            std::vector<int> idx = {workVarIdx, slackIdx};
            std::vector<double> val = {1.0, 1.0};
            solver.addRow(1.0, kHighsInf, 2, idx.data(), val.data());
        }
    }

    // =========================================================
    // SOLVE
    // =========================================================
    
    // --- Pre-declare violation tracking variables for use in both solve and sequential sections ---
    std::vector<int> emptyStints;
    std::vector<int> unavailableStints; 
    std::vector<int> emptySpotterStints;
    std::vector<int> unavailableSpotterStints;
    
    std::map<std::string, int> driverRestViolations;
    std::map<std::string, std::vector<std::pair<int, int>>> driverConsecutiveDetails;
    std::map<std::string, int> driverUnavailableCounts;
    std::map<std::string, int> fairShareViolations;
    
    std::map<std::string, int> spotterRestViolations;
    std::map<std::string, std::vector<std::pair<int, int>>> spotterConsecutiveDetails;
    std::map<std::string, int> spotterUnavailableCounts;
    
    double diagnosticTimeLimit = std::max((double)m_ctx.timeLimit, 60.0);
    if (m_ctx.timeLimit > 0) {
        solver.setOptionValue("time_limit", diagnosticTimeLimit);
    }
    solver.setOptionValue("mip_rel_gap", m_ctx.optimalityGap);
    
    solver.run();

    // =========================================================
    // SEQUENTIAL SPOTTER MODE (If Needed)
    // =========================================================
    
    if (m_ctx.spotterMode == SpotterMode::Sequential && !m_spotterPool.empty()) {
        // Extract driver assignments from the first solve
        std::vector<std::string> driverAssignments(m_totalStints, "N/A");
        const auto& solution = solver.getSolution();
        const std::vector<double>& colValues = solution.col_value;
        
        for (int s = 0; s < m_totalStints; ++s) {
            for (const auto& p : m_driverPool) {
                if (colValues[assignVars.at({p.name, s})] > 0.5) {
                    driverAssignments[s] = p.name;
                    break;
                }
            }
        }
        
        // Create a separate solver for spotters
        Highs spotterSolver;
        spotterSolver.setOptionValue("output_flag", false);
        if (m_ctx.timeLimit > 0) {
            spotterSolver.setOptionValue("time_limit", diagnosticTimeLimit);
        }
        spotterSolver.setOptionValue("mip_rel_gap", m_ctx.optimalityGap);
        
        // Add spotter variables
        for (const auto& p : m_spotterPool) {
            for (int s = 0; s < m_totalStints; ++s) {
                int idx = spotterSolver.getNumCol();
                spotterAssignVars[{p.name, s}] = idx;
                
                spotterSolver.addVar(0.0, 1.0);
                spotterSolver.changeColIntegrality(idx, HighsVarType::kInteger);
                
                if (!isDriverAvailable(p.name, s)) {
                    spotterSolver.changeColCost(idx, 100000.0);
                } else {
                    spotterSolver.changeColCost(idx, 0.0);
                }
            }
        }
        
        // Spotter coverage constraints
        for (int s = 0; s < m_totalStints; ++s) {
            std::vector<int> idx;
            std::vector<double> val;

            for (const auto& p : m_spotterPool) {
                idx.push_back(spotterAssignVars.at({p.name, s}));
                val.push_back(1.0);
            }

            int missingSpotterIdx = spotterSolver.getNumCol();
            spotterSolver.addVar(0.0, 1.0);
            spotterSolver.changeColIntegrality(missingSpotterIdx, HighsVarType::kInteger);
            double missingCost = m_ctx.allowNoSpotter ? 50000.0 : 1000000.0;
            spotterSolver.changeColCost(missingSpotterIdx, missingCost);
            idx.push_back(missingSpotterIdx);
            val.push_back(1.0);

            int extraSpotterIdx = spotterSolver.getNumCol();
            spotterSolver.addVar(0.0, 5.0);
            spotterSolver.changeColIntegrality(extraSpotterIdx, HighsVarType::kInteger);
            spotterSolver.changeColCost(extraSpotterIdx, 500000.0);
            idx.push_back(extraSpotterIdx);
            val.push_back(-1.0);

            double lb = m_ctx.allowNoSpotter ? 0.0 : 1.0;
            spotterSolver.addRow(lb, 1.0, (int)idx.size(), idx.data(), val.data());
        }
        
        // Prevent drivers from spotting themselves
        for (int s = 0; s < m_totalStints; ++s) {
            const std::string& driverName = driverAssignments[s];
            if (driverName == "N/A") continue;
            
            auto it = std::find_if(m_spotterPool.begin(), m_spotterPool.end(), 
                                 [&](const TeamMember& m){ return m.name == driverName; });
            if (it != m_spotterPool.end()) {
                int varIdx = spotterAssignVars.at({driverName, s});
                spotterSolver.changeColBounds(varIdx, 0.0, 0.0);
            }
        }
        
        // Add spotter max consecutive constraints
        for (const auto& p : m_spotterPool) {
            int maxConsecutive = p.preferredStints;
            for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
                std::vector<int> idx;
                std::vector<double> val;
                
                for (int i = 0; i <= maxConsecutive; ++i) {
                    idx.push_back(spotterAssignVars.at({p.name, s + i}));
                    val.push_back(1.0);
                }

                int slackIdx = spotterSolver.getNumCol();
                spotterSolver.addVar(0.0, (double)(maxConsecutive + 1));
                spotterSolver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                spotterSolver.changeColCost(slackIdx, 10000.0);

                idx.push_back(slackIdx);
                val.push_back(-1.0);
                
                spotterSolver.addRow(-kHighsInf, maxConsecutive, (int)idx.size(), idx.data(), val.data());
            }
        }
        
        // Add spotter minimum rest constraints
        for (const auto &p : m_spotterPool) {
            int minRestHours = p.minimumRestHours;
            if (minRestHours > 0 && m_stintWithPitSeconds > 0) {
                int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / m_stintWithPitSeconds));
                if (minRestStints > 0) {
                    int possibleRestStarts = m_totalStints - minRestStints + 1;
                    for (int s = 0; s < possibleRestStarts; ++s) {
                        for (int k = 1; k <= minRestStints; ++k) {
                            if (s + k < m_totalStints) {
                                std::vector<int> idx;
                                std::vector<double> val;
                                
                                idx.push_back(spotterAssignVars.at({p.name, s}));
                                val.push_back(1.0);
                                
                                idx.push_back(spotterAssignVars.at({p.name, s + k}));
                                val.push_back(1.0);

                                int slackIdx = spotterSolver.getNumCol();
                                spotterSolver.addVar(0.0, 1.0);
                                spotterSolver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
                                spotterSolver.changeColCost(slackIdx, 50000.0);

                                idx.push_back(slackIdx);
                                val.push_back(-1.0);
                                
                                spotterSolver.addRow(-kHighsInf, 1.0, (int)idx.size(), idx.data(), val.data());
                            }
                        }
                    }
                }
            }
        }
        
        // Solve spotter schedule
        spotterSolver.run();
        
        // Update spotter-related violation data with sequential results
        const auto& spotterSol = spotterSolver.getSolution();
        const std::vector<double>& spotterColValues = spotterSol.col_value;
        
        spotterRestViolations.clear();
        spotterConsecutiveDetails.clear();
        spotterUnavailableCounts.clear();
        emptySpotterStints.clear();
        
        for (const auto& p : m_spotterPool) {
            // Availability
            for (int s = 0; s < m_totalStints; ++s) {
                if (spotterColValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    if (!isDriverAvailable(p.name, s)) {
                        spotterUnavailableCounts[p.name]++;
                        unavailableSpotterStints.push_back(s + 1);
                    }
                }
            }

            // Max Consecutive
            int consecutive = 0;
            int startStint = -1;
            for (int s = 0; s < m_totalStints; ++s) {
                if (spotterColValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    if (consecutive == 0) startStint = s + 1;
                    consecutive++;
                } else {
                    if (consecutive > p.preferredStints) {
                        spotterConsecutiveDetails[p.name].push_back({startStint, s});
                    }
                    consecutive = 0;
                }
            }
            if (consecutive > p.preferredStints) {
                 spotterConsecutiveDetails[p.name].push_back({startStint, m_totalStints});
            }

            // Min Rest
            if (p.minimumRestHours > 0) {
                int minRestStints = static_cast<int>(std::floor((p.minimumRestHours * 3600) / m_stintWithPitSeconds));
                int lastSpottedStint = -999;
                for (int s = 0; s < m_totalStints; ++s) {
                    if (spotterColValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                        if (lastSpottedStint != -999) {
                            int stintsSinceLast = s - lastSpottedStint - 1;
                            if (stintsSinceLast >= 0 && stintsSinceLast < minRestStints) {
                                 spotterRestViolations[p.name]++;
                            }
                        }
                        lastSpottedStint = s;
                    }
                }
            }
        }
        
        // Check for empty spotter stints
        for (int s = 0; s < m_totalStints; ++s) {
            bool hasSpotter = false;
            for (const auto& p : m_spotterPool) {
                if (spotterColValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    hasSpotter = true;
                    break;
                }
            }
            if (!hasSpotter && !m_ctx.allowNoSpotter) {
                emptySpotterStints.push_back(s + 1);
            }
        }
    }

    // =========================================================
    // INTELLIGENT REPORT GENERATION
    // =========================================================
    
    json issues = json::array();
    const auto& solution = solver.getSolution();
    const std::vector<double>& colValues = solution.col_value;

    // --- Raw Data Collection (for Integrated mode and drivers) ---
    // Coverage Check
    struct DiagEntry { std::vector<std::string> drivers; std::vector<std::string> spotters; };
    std::vector<DiagEntry> schedule(m_totalStints);

    for (const auto& p : m_driverPool) {
        for (int s = 0; s < m_totalStints; ++s) {
            if (colValues[assignVars.at({p.name, s})] > 0.5) {
                schedule[s].drivers.push_back(p.name);
            }
        }
    }

    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            for (int s = 0; s < m_totalStints; ++s) {
                if (colValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    schedule[s].spotters.push_back(p.name);
                }
            }
        }
    }

    for (int s = 0; s < m_totalStints; ++s) {
        if (schedule[s].drivers.empty()) emptyStints.push_back(s + 1);
        if (m_ctx.spotterMode == SpotterMode::Integrated && !m_ctx.allowNoSpotter && schedule[s].spotters.empty()) {
            emptySpotterStints.push_back(s + 1);
        }
    }

    // Rule Checks (Drivers)
    for (const auto& p : m_driverPool) {
        
        // Availability
        for (int s = 0; s < m_totalStints; ++s) {
            if (colValues[assignVars.at({p.name, s})] > 0.5) {
                if (!isDriverAvailable(p.name, s)) {
                    driverUnavailableCounts[p.name]++;
                    unavailableStints.push_back(s + 1);
                }
            }
        }

        // Max Consecutive
        int consecutive = 0;
        int startStint = -1;
        for (int s = 0; s < m_totalStints; ++s) {
            if (colValues[assignVars.at({p.name, s})] > 0.5) {
                if (consecutive == 0) startStint = s + 1;
                consecutive++;
            } else {
                if (consecutive > p.preferredStints) {
                    driverConsecutiveDetails[p.name].push_back({startStint, s});
                }
                consecutive = 0;
            }
        }
        if (consecutive > p.preferredStints) {
             driverConsecutiveDetails[p.name].push_back({startStint, m_totalStints});
        }

        // Min Rest
        if (p.minimumRestHours > 0) {
            int minRestStints = static_cast<int>(std::floor((p.minimumRestHours * 3600) / m_stintWithPitSeconds));
            int lastDrivenStint = -999;
            for (int s = 0; s < m_totalStints; ++s) {
                if (colValues[assignVars.at({p.name, s})] > 0.5) {
                    if (lastDrivenStint != -999) {
                        int stintsSinceLast = s - lastDrivenStint - 1;
                        if (stintsSinceLast >= 0 && stintsSinceLast < minRestStints) {
                             driverRestViolations[p.name]++;
                        }
                    }
                    lastDrivenStint = s;
                }
            }
        }
    }
    
    // Rule Checks (Spotters - Integrated Mode Only, Sequential handled separately)
    if (m_ctx.spotterMode == SpotterMode::Integrated && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            
            // Availability
            for (int s = 0; s < m_totalStints; ++s) {
                if (colValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    if (!isDriverAvailable(p.name, s)) {
                        spotterUnavailableCounts[p.name]++;
                        unavailableSpotterStints.push_back(s + 1);
                    }
                }
            }

            // Max Consecutive
            int consecutive = 0;
            int startStint = -1;
            for (int s = 0; s < m_totalStints; ++s) {
                if (colValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                    if (consecutive == 0) startStint = s + 1;
                    consecutive++;
                } else {
                    if (consecutive > p.preferredStints) {
                        spotterConsecutiveDetails[p.name].push_back({startStint, s});
                    }
                    consecutive = 0;
                }
            }
            if (consecutive > p.preferredStints) {
                 spotterConsecutiveDetails[p.name].push_back({startStint, m_totalStints});
            }

            // Min Rest
            if (p.minimumRestHours > 0) {
                int minRestStints = static_cast<int>(std::floor((p.minimumRestHours * 3600) / m_stintWithPitSeconds));
                int lastSpottedStint = -999;
                for (int s = 0; s < m_totalStints; ++s) {
                    if (colValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                        if (lastSpottedStint != -999) {
                            int stintsSinceLast = s - lastSpottedStint - 1;
                            if (stintsSinceLast >= 0 && stintsSinceLast < minRestStints) {
                                 spotterRestViolations[p.name]++;
                            }
                        }
                        lastSpottedStint = s;
                    }
                }
            }
        }
    }
    
    // Fair Share (Drivers only)
    if (minStintsPerParticipant > 0) {
        for (const auto& p : m_driverPool) {
            int totalDriven = 0;
            for (int s = 0; s < m_totalStints; ++s) {
                if (colValues[assignVars.at({p.name, s})] > 0.5) totalDriven++;
            }
            if (totalDriven < minStintsPerParticipant) {
                fairShareViolations[p.name] = minStintsPerParticipant - totalDriven;
            }
        }
    }

    // --- SYNTHESIZE REPORT ---

    // Critical Gaps (No Driver)
    if (!emptyStints.empty()) {
        issues.push_back("CRITICAL: No drivers could be assigned to " + std::to_string(emptyStints.size()) + 
                         " stints (" + formatStintList(emptyStints) + "). This usually means the total roster size is too small or constraints are too strict during these times.");
    }

    // Critical Gaps (No Spotter)
    if (!emptySpotterStints.empty() && !m_ctx.allowNoSpotter) {
        issues.push_back("CRITICAL: No spotters could be assigned to " + std::to_string(emptySpotterStints.size()) + 
                         " stints (" + formatStintList(emptySpotterStints) + "). The spotter roster may be too small or constraints are too strict.");
    }

    // Systemic Rest Violations (Drivers)
    int totalRestViolations = 0;
    for(auto const& [name, count] : driverRestViolations) totalRestViolations += count;
    
    if (totalRestViolations > 3) {
        int minRest = m_driverPool.empty() ? 0 : m_driverPool[0].minimumRestHours;
        issues.push_back("SYSTEMIC FAILURE: The 'Minimum Rest' setting (" + std::to_string(minRest) + 
                         "h) is causing widespread conflicts for drivers. The diagnostic solver had to violate rest rules " + 
                         std::to_string(totalRestViolations) + " times to fill the schedule. Suggestion: Reduce minimum rest or add more drivers.");
    } else {
        for(auto const& [name, count] : driverRestViolations) {
            if (count > 0) issues.push_back("Driver " + name + " violated rest rules " + std::to_string(count) + " times.");
        }
    }

    // Systemic Rest Violations (Spotters)
    int totalSpotterRestViolations = 0;
    for(auto const& [name, count] : spotterRestViolations) totalSpotterRestViolations += count;
    
    if (totalSpotterRestViolations > 3) {
        issues.push_back("SYSTEMIC FAILURE: Spotter 'Minimum Rest' constraints are causing widespread conflicts. " +
                         std::to_string(totalSpotterRestViolations) + " violations occurred. Suggestion: Reduce spotter rest requirements or add more spotters.");
    } else {
        for(auto const& [name, count] : spotterRestViolations) {
            if (count > 0) issues.push_back("Spotter " + name + " violated rest rules " + std::to_string(count) + " times.");
        }
    }

    // Availability Hotspots (Drivers)
    if (!unavailableStints.empty()) {
        std::string range = formatStintList(unavailableStints);
        issues.push_back("AVAILABILITY GAP: Drivers were forced to drive during their unavailable blocks in stints: " + range + 
                         ". Please verify driver availability during these times.");
    }

    // Availability Hotspots (Spotters)
    if (!unavailableSpotterStints.empty()) {
        std::string range = formatStintList(unavailableSpotterStints);
        issues.push_back("AVAILABILITY GAP: Spotters were forced to spot during their unavailable blocks in stints: " + range + 
                         ". Please verify spotter availability during these times.");
    }

    // Consecutive Warnings (Drivers with Context Check)
    for (const auto& [name, ranges] : driverConsecutiveDetails) {
        for (const auto& range : ranges) {
            int start = range.first; // 1-based
            int end = range.second;  // 1-based inclusive
            
            // Context Check: Was anyone else available?
            bool alternativeExists = false;
            for (int s = start - 1; s < end; ++s) { // Convert to 0-based for array access
                for (const auto& other : m_driverPool) {
                    if (other.name == name) continue;
                    if (isDriverAvailable(other.name, s)) {
                        alternativeExists = true; 
                        break;
                    }
                }
                if (alternativeExists) break;
            }

            std::string msg = "Driver " + name + " exceeded max consecutive stint limit (Driven Stints: " + 
                              std::to_string(start) + "-" + std::to_string(end) + 
                              ", Limit: " + std::to_string(m_driverPool[0].preferredStints) + ").";
            
            if (!alternativeExists) {
                msg += " Note: No other drivers were available during this period.";
            }
            issues.push_back(msg);
        }
    }

    // Consecutive Warnings (Spotters)
    for (const auto& [name, ranges] : spotterConsecutiveDetails) {
        for (const auto& range : ranges) {
            int start = range.first;
            int end = range.second;
            
            bool alternativeExists = false;
            for (int s = start - 1; s < end; ++s) {
                for (const auto& other : m_spotterPool) {
                    if (other.name == name) continue;
                    if (isDriverAvailable(other.name, s)) {
                        alternativeExists = true; 
                        break;
                    }
                }
                if (alternativeExists) break;
            }

            std::string msg = "Spotter " + name + " exceeded max consecutive stint limit (Spotted Stints: " + 
                              std::to_string(start) + "-" + std::to_string(end) + ").";
            
            if (!alternativeExists) {
                msg += " Note: No other spotters were available during this period.";
            }
            issues.push_back(msg);
        }
    }

    // Fair Share
    for (const auto& [name, missing] : fairShareViolations) {
        issues.push_back("Driver " + name + " is under-utilized by " + std::to_string(missing) + " stints (Fair share requires more driving).");
    }

    // First Stint
    if (!m_ctx.raceData.firstStintDriver.empty()) {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        bool droveFirst = false;
        if (!schedule.empty()) {
            for (const auto& name : schedule[0].drivers) if (name == firstName) droveFirst = true;
        }
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), 
                                [&](const TeamMember& m){ return m.name == firstName; });
        if (it != m_driverPool.end() && !droveFirst) {
             issues.push_back("Requested First Stint Driver '" + firstName + "' could not be assigned to Stint 1.");
        }
    }

    // Fallback
    if (issues.empty()) {
        issues.push_back("Unknown infeasibility. The diagnostic solver found a valid relaxed schedule, but strict verification failed.");
    }

    json result;
    result["success"] = false;
    result["error"] = "Schedule Infeasible";
    result["diagnosis"] = issues;
    result["raceData"] = m_ctx.raceData; 
    
    return result;
}
