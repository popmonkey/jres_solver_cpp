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
    // Returns true if the driver is strictly available for the ENTIRE duration of stint 's'
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
        return false; // Default to unavailable if missing from map
    };

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

        solver.addRow(row, 1.0, 1.0);
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
            solver.addRow(row, -COIN_DBL_MAX, maxConsecutive);
        }
    }

    // --- D. Fair Share ---
    double totalLaps = m_totalStints * m_stintLaps;
    double equalShareLaps = totalLaps / m_driverPool.size();
    int minLapsPerParticipant = static_cast<int>(std::ceil(0.25 * equalShareLaps));
    double minStintsFloat = (m_stintLaps > 0) ? (double)minLapsPerParticipant / m_stintLaps : 0.0;
    int minStintsPerParticipant = static_cast<int>(std::ceil(minStintsFloat));
    
    if (minStintsPerParticipant * m_driverPool.size() > m_totalStints) minStintsPerParticipant = 0;

    if (minStintsPerParticipant > 0) {
        for (const auto &p : m_driverPool) {
            CoinPackedVector row;
            for (int s = 0; s < m_totalStints; ++s) {
                row.insert(assignVars.at({p.name, s}), 1.0);
            }

            int slackIdx = solver.getNumCols();
            solver.addCol(CoinPackedVector(), 0.0, (double)m_totalStints, 50000.0); 
            solver.setInteger(slackIdx);

            row.insert(slackIdx, 1.0);
            solver.addRow(row, minStintsPerParticipant, COIN_DBL_MAX);
        }
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
                            solver.addRow(row, -COIN_DBL_MAX, 1.0);
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
            solver.addRow(row, 1.0, COIN_DBL_MAX);
        }
    }

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
    std::map<std::string, int> fairShareViolations;
    
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
        
        // Fair Share
        if (minStintsPerParticipant > 0) {
            int totalDriven = 0;
            for (int s = 0; s < m_totalStints; ++s) {
                if (solution[assignVars.at({p.name, s})] > 0.5) totalDriven++;
            }
            if (totalDriven < minStintsPerParticipant) {
                fairShareViolations[p.name] = minStintsPerParticipant - totalDriven;
            }
        }
    }

    // --- SYNTHESIZE REPORT ---

    // A. Critical Gaps (No Driver)
    if (!emptyStints.empty()) {
        issues.push_back("CRITICAL: No drivers could be assigned to " + std::to_string(emptyStints.size()) + 
                         " stints (" + formatStintList(emptyStints) + "). This usually means the total roster size is too small or constraints are too strict during these times.");
    }

    // B. Systemic Rest Violations
    int totalRestViolations = 0;
    for(auto const& [name, count] : driverRestViolations) totalRestViolations += count;
    
    if (totalRestViolations > 3) {
        int minRest = m_driverPool.empty() ? 0 : m_driverPool[0].minimumRestHours;
        issues.push_back("SYSTEMIC FAILURE: The 'Minimum Rest' setting (" + std::to_string(minRest) + 
                         "h) is causing widespread conflicts. The diagnostic solver had to violate rest rules " + 
                         std::to_string(totalRestViolations) + " times to fill the schedule. Suggestion: Reduce minimum rest or add more drivers.");
    } else {
        for(auto const& [name, count] : driverRestViolations) {
            if (count > 0) issues.push_back("Driver " + name + " violated rest rules " + std::to_string(count) + " times.");
        }
    }

    // C. Availability Hotspots
    if (!unavailableStints.empty()) {
        std::string range = formatStintList(unavailableStints);
        issues.push_back("AVAILABILITY GAP: Drivers were forced to drive during their unavailable blocks in stints: " + range + 
                         ". Please verify driver availability during these times.");
    }

    // D. Consecutive Warnings (With Context Check)
    for (const auto& [name, ranges] : driverConsecutiveDetails) {
        for (const auto& range : ranges) {
            int start = range.first; // 1-based
            int end = range.second;  // 1-based inclusize
            
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

    // E. Fair Share
    for (const auto& [name, missing] : fairShareViolations) {
        issues.push_back("Driver " + name + " is under-utilized by " + std::to_string(missing) + " stints (Fair share requires more driving).");
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

    // G. Fallback
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
