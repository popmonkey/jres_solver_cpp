/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_diagnostic_solver.cpp
 * @brief Diagnostic solver for the JRES Solver library.
 */
#include "jres_diagnostic_solver.hpp"
#include "Highs.h"
#include "jres_internal_types.hpp"
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

JresDiagnosticSolver::JresDiagnosticSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options)
    : JresSolverBase(input, options)
{
}

JresDiagnosticSolver::~JresDiagnosticSolver() = default;

jres::internal::SolverOutput JresDiagnosticSolver::diagnose()
{
    Highs solver;
    solver.setOptionValue("output_flag", false);

    // Pre-parse stint times
    std::vector<std::chrono::system_clock::time_point> startTimes;
    std::vector<std::chrono::system_clock::time_point> endTimes;
    startTimes.reserve(m_input.stints.size());
    endTimes.reserve(m_input.stints.size());
    for (const auto& stint : m_input.stints) {
        startTimes.push_back(jres::internal::TimeHelpers::stringToTimePoint(stint.startTime));
        endTimes.push_back(jres::internal::TimeHelpers::stringToTimePoint(stint.endTime));
    }
    
    std::map<std::pair<std::string, int>, int> assignVars;
    
    // --- Helper: Centralized Availability Logic ---
    auto isDriverAvailable = [&](const std::string& name, int s) -> bool {
        auto stintStartTime = jres::internal::TimeHelpers::stringToTimePoint(m_input.stints[s].startTime);
        std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(stintStartTime);
        
        auto member_availability_it = m_input.availability.find(name);
        if (member_availability_it != m_input.availability.end()) {
            auto time_availability_it = member_availability_it->second.find(availabilityKey);
            if (time_availability_it != member_availability_it->second.end()) {
                if (time_availability_it->second == jres::internal::Availability::Unavailable) {
                    return false;
                }
            }
        }
        return true;
    };

    // =========================================================
    // BUILD DIAGNOSTIC MODEL
    // =========================================================

    std::map<std::pair<std::string, int>, int> spotterAssignVars;

    // --- Driver Assignment Variables ---
    for (const auto& p : m_driverPool) {
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            if (!isDriverAvailable(p.name, s)) {
                continue;
            }
            int idx = solver.getNumCol();
            assignVars[{p.name, s}] = idx;
            
            solver.addVar(0.0, 1.0);
            solver.changeColIntegrality(idx, HighsVarType::kInteger);
            solver.changeColCost(idx, 0.0); 
        }
    }

    // --- Spotter Assignment Variables (Integrated Mode) ---
    if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
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
    for (size_t s = 0; s < m_input.stints.size(); ++s) {
        std::vector<int> idx;
        std::vector<double> val;

        for (const auto& p : m_driverPool) {
            if (assignVars.count({p.name, s})) {
                idx.push_back(assignVars.at({p.name, s}));
                val.push_back(1.0);
            }
        }

        int missingIdx = solver.getNumCol();
        solver.addVar(0.0, 1.0);
        solver.changeColIntegrality(missingIdx, HighsVarType::kInteger);
        solver.changeColCost(missingIdx, 1000000.0);
        idx.push_back(missingIdx);
        val.push_back(1.0);

        int extraIdx = solver.getNumCol();
        solver.addVar(0.0, 5.0); 
        solver.changeColIntegrality(extraIdx, HighsVarType::kInteger);
        solver.changeColCost(extraIdx, 500000.0);
        idx.push_back(extraIdx);
        val.push_back(-1.0);

        solver.addRow(1.0, 1.0, (int)idx.size(), idx.data(), val.data());
    }

    // --- Max Consecutive Stints ---
    for (const auto& p : m_driverPool) {
        int maxConsecutive = p.maxStints;
        if (maxConsecutive == 0 || m_input.stints.size() < maxConsecutive) continue; 

        for (size_t s = 0; s <= m_input.stints.size() - maxConsecutive; ++s) {
            std::vector<int> idx;
            std::vector<double> val;
            
            for (size_t i = 0; i < maxConsecutive + 1; ++i) {
                if (assignVars.count({p.name, s + i})) {
                    idx.push_back(assignVars.at({p.name, s + i}));
                    val.push_back(1.0);
                }
            }
            if (idx.empty()) continue;

            int slackIdx = solver.getNumCol();
            solver.addVar(0.0, (double)(maxConsecutive + 1));
            solver.changeColIntegrality(slackIdx, HighsVarType::kInteger);
            solver.changeColCost(slackIdx, 10000.0);

            idx.push_back(slackIdx);
            val.push_back(-1.0);
            
            solver.addRow(-kHighsInf, maxConsecutive, (int)idx.size(), idx.data(), val.data());
        }
    }

    // --- Minimum Rest Soft Constraints ---
    for (const auto& p : m_driverPool) {
        if (p.minimumRestHours <= 0) continue;
        auto minRestDuration = std::chrono::hours(p.minimumRestHours);
        
        for (size_t s1 = 0; s1 < m_input.stints.size(); ++s1) {
            if (!assignVars.count({p.name, s1})) continue;
            
            for (size_t s2 = s1 + 2; s2 < m_input.stints.size(); ++s2) {
                 if (!assignVars.count({p.name, s2})) continue;
                 
                 if (startTimes[s2] < endTimes[s1] + minRestDuration) {
                     // Violation if both selected
                     int var1 = assignVars.at({p.name, s1});
                     int var2 = assignVars.at({p.name, s2});
                     
                     int slack = solver.getNumCol();
                     solver.addVar(0.0, 1.0);
                     solver.changeColIntegrality(slack, HighsVarType::kInteger);
                     solver.changeColCost(slack, 20000.0);
                     
                     // x1 + x2 - slack <= 1
                     std::vector<int> idx = {var1, var2, slack};
                     std::vector<double> val = {1.0, 1.0, -1.0};
                     solver.addRow(-kHighsInf, 1.0, 3, idx.data(), val.data());
                 } else {
                     break; 
                 }
            }
        }
    }
    
    if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED && !m_spotterPool.empty()) {
        for (const auto& p : m_spotterPool) {
            int maxConsecutive = p.maxStints;
            if (maxConsecutive == 0 || m_input.stints.size() < maxConsecutive) continue;

            for (size_t s = 0; s <= m_input.stints.size() - maxConsecutive; ++s) {
                std::vector<int> idx;
                std::vector<double> val;
                
                for (size_t i = 0; i < maxConsecutive + 1; ++i) {
                     if (spotterAssignVars.count({p.name, s + i})) {
                        idx.push_back(spotterAssignVars.at({p.name, s + i}));
                        val.push_back(1.0);
                    }
                }
                if (idx.empty()) continue;

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

    solver.run();

    jres::internal::SolverOutput output;
    const auto& solution = solver.getSolution();
    const std::vector<double>& colValues = solution.col_value;

    std::vector<int> emptyStints;
    for (size_t s = 0; s < m_input.stints.size(); ++s) {
        bool driverFound = false;
        for (const auto& p : m_driverPool) {
            if (assignVars.count({p.name, s}) && colValues[assignVars.at({p.name, s})] > 0.5) {
                driverFound = true;
                break;
            }
        }
        if (!driverFound) {
            emptyStints.push_back(m_input.stints[s].id);
        }
    }

        if (!emptyStints.empty()) {
            output.diagnosis.push_back("CRITICAL: No drivers could be assigned to " + std::to_string(emptyStints.size()) + 
                             " stints (" + formatStintList(emptyStints) + "). This usually means the total roster size is too small or constraints are too strict during these times.");
        }
    
        if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED && !m_options.allowNoSpotter) {
            std::vector<int> emptySpotterStints;
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                bool spotterFound = false;
                for (const auto& p : m_spotterPool) {
                    if (spotterAssignVars.count({p.name, s}) && colValues[spotterAssignVars.at({p.name, s})] > 0.5) {
                        spotterFound = true;
                        break;
                    }
                }
                if (!spotterFound) {
                    emptySpotterStints.push_back(m_input.stints[s].id);
                }
            }
            if (!emptySpotterStints.empty()) {
                output.diagnosis.push_back("CRITICAL: No spotters could be assigned to " + std::to_string(emptySpotterStints.size()) + 
                                 " stints (" + formatStintList(emptySpotterStints) + "). The spotter roster may be too small or constraints are too strict.");
            }
        }
    
        std::map<std::string, std::vector<std::pair<int, int>>> driverConsecutiveDetails;    for (const auto& p : m_driverPool) {
        int consecutive = 0;
        int startStint = -1;
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            if (assignVars.count({p.name, s}) && colValues[assignVars.at({p.name, s})] > 0.5) {
                if (consecutive == 0) startStint = m_input.stints[s].id;
                consecutive++;
            } else {
                if (consecutive > p.maxStints) {
                    driverConsecutiveDetails[p.name].push_back({startStint, m_input.stints[s-1].id});
                }
                consecutive = 0;
            }
        }
        if (consecutive > p.maxStints) {
             driverConsecutiveDetails[p.name].push_back({startStint, m_input.stints.back().id});
        }
    }

    for (const auto& [name, ranges] : driverConsecutiveDetails) {
        auto it = std::find_if(m_driverPool.begin(), m_driverPool.end(), [&](const auto& d){ return d.name == name; });
        int limit = (it != m_driverPool.end()) ? it->maxStints : 0;

        for (const auto& range : ranges) {
            output.diagnosis.push_back("Driver " + name + " exceeded max consecutive stint limit (Driven Stints: " + 
                              std::to_string(range.first) + "-" + std::to_string(range.second) + 
                              ", Limit: " + std::to_string(limit) + ").");
        }
    }

    // Check for Minimum Rest Violations
    for (const auto& p : m_driverPool) {
        if (p.minimumRestHours <= 0) continue;
        
        std::vector<int> driverStints;
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            if (assignVars.count({p.name, s}) && colValues[assignVars.at({p.name, s})] > 0.5) {
                driverStints.push_back(s);
            }
        }
        
        if (driverStints.empty()) continue;
        
        int lastShiftEndStintIdx = driverStints[0];
        
        for (size_t i = 1; i < driverStints.size(); ++i) {
            int currentStintIdx = driverStints[i];
            int prevStintIdx = driverStints[i-1];
            
            if (currentStintIdx == prevStintIdx + 1) {
                // Continuation of shift
                lastShiftEndStintIdx = currentStintIdx;
            } else {
                // New shift
                // Check gap from lastShiftEndStintIdx to currentStintIdx
                auto gap = startTimes[currentStintIdx] - endTimes[lastShiftEndStintIdx];
                if (gap < std::chrono::hours(p.minimumRestHours)) {
                     auto duration = std::chrono::duration_cast<std::chrono::minutes>(gap).count();
                      output.diagnosis.push_back("Driver " + p.name + " has insufficient rest between stints " + 
                          std::to_string(m_input.stints[lastShiftEndStintIdx].id) + " and " + std::to_string(m_input.stints[currentStintIdx].id) + 
                          " (Gap: " + std::to_string(duration) + "m, Required: " + std::to_string(p.minimumRestHours * 60) + "m).");
                }
                lastShiftEndStintIdx = currentStintIdx;
            }
        }
    }

    if (output.diagnosis.empty()) {
        output.diagnosis.push_back("Diagnosis complete.");
    }
    output.teamMembers = m_input.teamMembers;
    return output;
}
