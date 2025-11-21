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
    // Remove duplicates
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
            
            // Availability Logic
            auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * m_stintWithPitSeconds));
            auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(m_stintWithPitSeconds));
            auto stintEndCheck = stintEnd - std::chrono::seconds(1);
            
            std::string startKey = TimeHelpers::timePointToKey(stintStart);
            std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);
            
            bool isUnavailable = false;
            if (m_ctx.raceData.availability.contains(p.name)) {
                if (m_ctx.raceData.availability[p.name].value(startKey, "Unavailable") == "Unavailable") isUnavailable = true;
                if (m_ctx.raceData.availability[p.name].value(endKey, "Unavailable") == "Unavailable") isUnavailable = true;
            } else { isUnavailable = true; }

            if (isUnavailable) solver.setObjCoeff(idx, 100000.0); 
            else solver.setObjCoeff(idx, 0.0); 
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
    std::vector<int> unavailableStints; // Which stints triggered an unavailability violation
    std::vector<int> restViolationStints; // Which stints were driven in violation
    
    std::map<std::string, int> driverRestViolations;
    std::map<std::string, int> driverConsecutiveViolations;
    std::map<std::string, int> driverUnavailableCounts;
    
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
                auto stintStart = raceStartUTC + std::chrono::seconds(static_cast<long>(s * m_stintWithPitSeconds));
                auto stintEnd = stintStart + std::chrono::seconds(static_cast<long>(m_stintWithPitSeconds));
                auto stintEndCheck = stintEnd - std::chrono::seconds(1);
                std::string startKey = TimeHelpers::timePointToKey(stintStart);
                std::string endKey = TimeHelpers::timePointToKey(stintEndCheck);
                
                bool isUnavailable = false;
                if (m_ctx.raceData.availability.contains(p.name)) {
                    if (m_ctx.raceData.availability[p.name].value(startKey, "Unavailable") == "Unavailable") isUnavailable = true;
                    if (m_ctx.raceData.availability[p.name].value(endKey, "Unavailable") == "Unavailable") isUnavailable = true;
                } else { isUnavailable = true; }

                if (isUnavailable) {
                    driverUnavailableCounts[p.name]++;
                    unavailableStints.push_back(s + 1);
                }
            }
        }

        // Max Consecutive
        int consecutive = 0;
        for (int s = 0; s < m_totalStints; ++s) {
            if (solution[assignVars.at({p.name, s})] > 0.5) consecutive++;
            else {
                if (consecutive > p.preferredStints) driverConsecutiveViolations[p.name]++;
                consecutive = 0;
            }
        }
        if (consecutive > p.preferredStints) driverConsecutiveViolations[p.name]++;

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
                             restViolationStints.push_back(s + 1);
                        }
                    }
                    lastDrivenStint = s;
                }
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
        // If multiple drivers are violating rest, it's a systemic issue, not an individual one.
        int minRest = m_driverPool.empty() ? 0 : m_driverPool[0].minimumRestHours;
        issues.push_back("SYSTEMIC FAILURE: The 'Minimum Rest' setting (" + std::to_string(minRest) + 
                         "h) is causing widespread conflicts. The diagnostic solver had to violate rest rules " + 
                         std::to_string(totalRestViolations) + " times to fill the schedule. Suggestion: Reduce minimum rest or add more drivers.");
    } else {
        // Report individual violators if count is low
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

    // D. Consecutive Warnings
    int totalConsec = 0;
    for(auto const& [name, count] : driverConsecutiveViolations) totalConsec += count;
    if (totalConsec > 0) {
        issues.push_back("WARNING: Max consecutive stint limits were exceeded " + std::to_string(totalConsec) + " times to maintain coverage.");
    }

    // E. First Stint
    if (!m_ctx.raceData.firstStintDriver.empty()) {
        std::string firstName = m_ctx.raceData.firstStintDriver;
        bool droveFirst = false;
        if (!schedule.empty()) {
            for (const auto& name : schedule[0].drivers) if (name == firstName) droveFirst = true;
        }
        if (!droveFirst) {
             issues.push_back("Constraint 'First Stint Driver: " + firstName + "' could not be met.");
        }
    }

    // F. Fallback
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
