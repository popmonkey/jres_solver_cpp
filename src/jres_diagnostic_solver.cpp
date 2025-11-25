/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_diagnostic_solver.cpp
 * @brief Diagnostic solver implementation for JRES endurance race scheduling.
 */

 #include "jres_diagnostic_solver.hpp"

// --- COIN-OR Includes ---
#include "OsiClpSolverInterface.hpp"
#include "CbcModel.hpp"
#include "CoinPackedVector.hpp"
#include "CoinBuild.hpp"

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
    OsiClpSolverInterface solver;
    solver.setObjSense(1.0); // Minimize Cost
    solver.setLogLevel(0);

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

    CoinBuild rows;

    // =========================================================
    // 1. BUILD DIAGNOSTIC MODEL
    // =========================================================

    // --- A. Driver Assignment Variables ---
    for (const auto& p : m_driverPool) {
        for (int s = 0; s < m_totalStints; ++s) {
            int idx = solver.getNumCols();
            assignVars[{p.name, s}] = idx;
            solver.addCol(CoinPackedVector(), 0.0, 1.0, 0.0); 
            solver.setInteger(idx);
            
            if (!isDriverAvailable(p.name, s)) {
                solver.setObjCoeff(idx, 100000.0); // High cost penalty
            } else {
                solver.setObjCoeff(idx, 0.0); 
            }
        }
    }

    // --- B. Coverage Constraints (Exactly 1 Driver) ---
    for (int s = 0; s < m_totalStints; ++s) {
        CoinPackedVector row;
        for (const auto& p : m_driverPool) {
            row.insert(assignVars.at({p.name, s}), 1.0);
        }

        // Slack: Missing Driver
        int missingIdx = solver.getNumCols();
        solver.addCol(CoinPackedVector(), 0.0, 1.0, 1000000.0);
        solver.setInteger(missingIdx);
        row.insert(missingIdx, 1.0); 

        // Slack: Extra Driver
        int extraIdx = solver.getNumCols();
        solver.addCol(CoinPackedVector(), 0.0, 5.0, 500000.0);
        solver.setInteger(extraIdx);
        row.insert(extraIdx, -1.0); 

        rows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 1.0, 1.0);
    }

    // --- C. Max Consecutive Stints ---
    for (const auto& p : m_driverPool) {
        int maxConsecutive = p.preferredStints;
        for (int s = 0; s < m_totalStints - maxConsecutive; ++s) {
            CoinPackedVector row;
            for (int i = 0; i <= maxConsecutive; ++i) {
                row.insert(assignVars.at({p.name, s + i}), 1.0);
            }

            int slackIdx = solver.getNumCols();
            solver.addCol(CoinPackedVector(), 0.0, (double)(maxConsecutive + 1), 10000.0);
            solver.setInteger(slackIdx);

            row.insert(slackIdx, -1.0); 
            rows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), -COIN_DBL_MAX, maxConsecutive);
        }
    }

    // --- D. Fair Share (Updated to match Standard Solver) ---
    double totalLaps = m_totalStints * m_stintLaps;
    double equalShareLaps = totalLaps / m_driverPool.size();
    double equalShareStints = (double)m_totalStints / m_driverPool.size();
    
    // Min Stints
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    double minStintsFloat = (m_stintLaps > 0) ? (double)minLapsPerParticipant / m_stintLaps : 0.0;
    int minStintsPerParticipant = static_cast<int>(std::ceil(minStintsFloat));
    if (minStintsPerParticipant * m_driverPool.size() > m_totalStints) minStintsPerParticipant = 0;

    // Max Stints (Average + 3)
    int maxStintsPerParticipant = static_cast<int>(std::ceil(equalShareStints)) + 3;

    for (const auto &p : m_driverPool) {
        CoinPackedVector row;
        for (int s = 0; s < m_totalStints; ++s) {
            row.insert(assignVars.at({p.name, s}), 1.0);
        }

        // Min Slack
        if (minStintsPerParticipant > 0) {
            int slackIdx = solver.getNumCols();
            solver.addCol(CoinPackedVector(), 0.0, (double)m_totalStints, 50000.0); 
            solver.setInteger(slackIdx);
            CoinPackedVector minRow = row;
            minRow.insert(slackIdx, 1.0);
            rows.addRow(minRow.getNumElements(), minRow.getIndices(), minRow.getElements(), minStintsPerParticipant, COIN_DBL_MAX);
        }

        // Max Slack
        int maxSlackIdx = solver.getNumCols();
        solver.addCol(CoinPackedVector(), 0.0, (double)m_totalStints, 50000.0);
        solver.setInteger(maxSlackIdx);
        CoinPackedVector maxRow = row;
        maxRow.insert(maxSlackIdx, -1.0);
        rows.addRow(maxRow.getNumElements(), maxRow.getIndices(), maxRow.getElements(), -COIN_DBL_MAX, maxStintsPerParticipant);
    }

    // --- E. Minimum Rest ---
    for (const auto &p : m_driverPool) {
        int minRestHours = p.minimumRestHours;
        if (minRestHours > 0 && m_stintWithPitSeconds > 0) {
            int minRestStints = static_cast<int>(std::floor((minRestHours * 3600) / m_stintWithPitSeconds));
            if (minRestStints > 0) {
                int possibleRestStarts = m_totalStints - minRestStints + 1;
                for (int s = 0; s < possibleRestStarts; ++s) {
                    for (int k = 1; k <= minRestStints; ++k) {
                        if (s + k < m_totalStints) {
                            CoinPackedVector row;
                            row.insert(assignVars.at({p.name, s}), 1.0);
                            row.insert(assignVars.at({p.name, s + k}), 1.0);

                            int slackIdx = solver.getNumCols();
                            solver.addCol(CoinPackedVector(), 0.0, 1.0, 50000.0);
                            solver.setInteger(slackIdx);

                            row.insert(slackIdx, -1.0);
                            rows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), -COIN_DBL_MAX, 1.0);
                        }
                    }
                }
            }
        }
    }
    
    // --- F. First Stint Driver ---
    if (!m_ctx.raceData.firstStintDriver.empty()) {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), 
                                [&](const TeamMember& m){ return m.name == firstName; });
        if (it != m_driverPool.end()) {
            int workVarIdx = assignVars.at({firstName, 0});
            int slackIdx = solver.getNumCols();
            solver.addCol(CoinPackedVector(), 0.0, 1.0, 80000.0);
            solver.setInteger(slackIdx);

            CoinPackedVector row;
            row.insert(workVarIdx, 1.0);
            row.insert(slackIdx, 1.0);
            rows.addRow(row.getNumElements(), row.getIndices(), row.getElements(), 1.0, COIN_DBL_MAX);
        }
    }

    // Correctly invoke addRows via the base class to avoid hiding issues
    static_cast<OsiSolverInterface*>(&solver)->addRows(rows);

    // =========================================================
    // 2. SOLVE
    // =========================================================
    
    CbcModel model(solver);
    double diagnosticTimeLimit = std::max((double)m_ctx.timeLimit, 60.0);
    model.setDblParam(CbcModel::CbcMaximumSeconds, diagnosticTimeLimit);
    model.setDblParam(CbcModel::CbcAllowableFractionGap, m_ctx.optimalityGap);
    model.setLogLevel(0);
    
    model.branchAndBound();

    // =========================================================
    // 3. INTELLIGENT REPORT GENERATION
    // =========================================================
    
    json issues = json::array();
    const double* solution = model.solver()->getColSolution();

    // --- Raw Data Collection ---
    std::vector<int> emptyStints;
    std::vector<int> unavailableStints; 
    
    std::map<std::string, int> driverRestViolations;
    std::map<std::string, std::vector<std::pair<int, int>>> driverConsecutiveDetails;
    std::map<std::string, int> driverUnavailableCounts;
    std::map<std::string, int> fairShareMinViolations;
    std::map<std::string, int> fairShareMaxViolations;
    
    // 1. Coverage Check
    struct DiagEntry { std::vector<std::string> drivers; };
    std::vector<DiagEntry> schedule(m_totalStints);

    for (const auto& p : m_driverPool) {
        for (int s = 0; s < m_totalStints; ++s) {
            if (solution[assignVars.at({p.name, s})] > 0.5) {
                schedule[s].drivers.push_back(p.name);
            }
        }
    }

    for (int s = 0; s < m_totalStints; ++s) {
        if (schedule[s].drivers.empty()) emptyStints.push_back(s + 1);
    }

    // 2. Rule Checks
    for (const auto& p : m_driverPool) {
        
        // Availability
        for (int s = 0; s < m_totalStints; ++s) {
            if (solution[assignVars.at({p.name, s})] > 0.5) {
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
            if (solution[assignVars.at({p.name, s})] > 0.5) {
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
                if (solution[assignVars.at({p.name, s})] > 0.5) {
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
        
        // Fair Share Check
        int totalDriven = 0;
        for (int s = 0; s < m_totalStints; ++s) {
            if (solution[assignVars.at({p.name, s})] > 0.5) totalDriven++;
        }
        
        if (minStintsPerParticipant > 0 && totalDriven < minStintsPerParticipant) {
            fairShareMinViolations[p.name] = minStintsPerParticipant - totalDriven;
        }
        if (totalDriven > maxStintsPerParticipant) {
            fairShareMaxViolations[p.name] = totalDriven - maxStintsPerParticipant;
        }
    }

    // --- SYNTHESIZE REPORT ---

    // A. Critical Gaps
    if (!emptyStints.empty()) {
        issues.push_back("CRITICAL: No drivers could be assigned to " + std::to_string(emptyStints.size()) + 
                         " stints (" + formatStintList(emptyStints) + ").");
    }

    // B. Systemic Rest Violations
    int totalRestViolations = 0;
    for(auto const& [name, count] : driverRestViolations) totalRestViolations += count;
    
    if (totalRestViolations > 3) {
        int minRest = m_driverPool.empty() ? 0 : m_driverPool[0].minimumRestHours;
        issues.push_back("SYSTEMIC FAILURE: 'Minimum Rest' (" + std::to_string(minRest) + 
                         "h) caused " + std::to_string(totalRestViolations) + " conflicts.");
    } else {
        for(auto const& [name, count] : driverRestViolations) {
            if (count > 0) issues.push_back("Driver " + name + " violated rest rules " + std::to_string(count) + " times.");
        }
    }

    // C. Availability
    if (!unavailableStints.empty()) {
        issues.push_back("AVAILABILITY: Drivers forced to drive during unavailable times in stints: " + formatStintList(unavailableStints));
    }

    // D. Consecutive
    for (const auto& [name, ranges] : driverConsecutiveDetails) {
        for (const auto& range : ranges) {
            issues.push_back("Driver " + name + " exceeded max consecutive limit (Stints " + 
                              std::to_string(range.first) + "-" + std::to_string(range.second) + ").");
        }
    }

    // E. Fair Share
    for (const auto& [name, missing] : fairShareMinViolations) {
        issues.push_back("Driver " + name + " is under-utilized by " + std::to_string(missing) + " stints.");
    }
    for (const auto& [name, over] : fairShareMaxViolations) {
        issues.push_back("Driver " + name + " is over-utilized by " + std::to_string(over) + " stints (Exceeds fair share max).");
    }

    // F. First Stint
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
