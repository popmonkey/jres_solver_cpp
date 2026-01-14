#include "solver_diagnostics.hpp"
#include "capacity_analyzer.hpp"
#include "../utils/date_utils.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <tuple>

namespace jres::analysis {

std::string explain_assignment_failure(
    int stintIndex,
    const std::string& violationDriver,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::map<std::pair<std::string, int>, int>& driverWorkVars,
    const std::vector<double>& colValues)
{
    // Keeping this for backward compatibility if needed, but not used in new flow
    return "";
}

std::vector<std::string> formatHumanDiagnostic(
    const std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::set<int>& unavailableVars,
    const std::map<std::pair<std::string, int>, int>& driverWorkVars,
    const std::map<std::pair<std::string, int>, int>& spotterWorkVars,
    const std::vector<double>& colValues,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::vector<jres::internal::TeamMember>& spotterPool)
{
    using namespace jres::internal;
    std::vector<std::string> report;

    double raceDurationHours = 0.0;
    for (const auto& s : input.stints) {
        auto t1 = TimeHelpers::stringToTimePoint(s.startTime);
        auto t2 = TimeHelpers::stringToTimePoint(s.endTime);
        raceDurationHours += std::chrono::duration<double, std::ratio<3600>>(t2 - t1).count();
    }

    // --- Roster Health Check (Drivers) ---
    if (!driverPool.empty()) {
        auto cap = CapacityAnalyzer::calculate_max_potential_capacity(driverPool, input);
        if (cap.totalCapacity < raceDurationHours) {
            std::ostringstream ss;
            ss << "Driver Roster Understaffed: You have " << std::fixed << std::setprecision(1) 
               << cap.totalCapacity << " hours of coverage for a " 
               << raceDurationHours << " hour race.";
            report.push_back(ss.str());
        }
    }

    // --- Roster Health Check (Spotters) ---
    if (!spotterPool.empty()) {
        auto cap = CapacityAnalyzer::calculate_max_potential_capacity(spotterPool, input);
        if (cap.totalCapacity < raceDurationHours) {
            std::ostringstream ss;
            ss << "Spotter Roster Understaffed: You have " << std::fixed << std::setprecision(1) 
               << cap.totalCapacity << " hours of coverage for a " 
               << raceDurationHours << " hour race.";
            report.push_back(ss.str());
        }
    }

    // --- Collect Violations per Stint ---
    struct Violation {
        int priority; // 0: Unavailable (Critical), 1: Max Busy, 2: Min Rest, 3: Other
        std::string description;
        std::string member;
        std::string role; // "Driver" or "Spotter"
    };
    std::map<int, std::vector<Violation>> stintViolations;
    std::vector<std::string> globalViolations;

    // Build reverse map for variable index -> (Member, Stint, Role)
    std::map<int, std::tuple<std::string, int, std::string>> varToInfo;
    
    for(const auto& [key, varIdx] : driverWorkVars) {
        varToInfo[varIdx] = std::make_tuple(key.first, key.second, "Driver");
    }
    for(const auto& [key, varIdx] : spotterWorkVars) {
        varToInfo[varIdx] = std::make_tuple(key.first, key.second, "Spotter");
    }

    // Check Unavailable (Priority 0)
    for (int varIdx : unavailableVars) {
        if (varIdx < (int)colValues.size() && colValues[varIdx] > 0.5) {
            if (varToInfo.count(varIdx)) {
                auto [member, stintIdx, role] = varToInfo[varIdx];
                stintViolations[stintIdx].push_back({0, "Unavailable", member, role});
            }
        }
    }

    // Check Slack (Rest, Busy, etc)
    for (const auto& [varIdx, info] : slackInfo) {
        if (varIdx < (int)colValues.size() && colValues[varIdx] > 0.001) {
            int priority = 3;
            if (info.type.find("Busy") != std::string::npos) priority = 1;
            else if (info.type.find("Rest") != std::string::npos) priority = 2;
            else if (info.type.find("Fair Share") != std::string::npos) priority = 2;

            if (info.stintIndex >= 0) {
                 stintViolations[info.stintIndex].push_back({priority, info.type, info.memberName, "Participant"});
            } else {
                // Global violation
                bool assignedAny = false;
                for(const auto& [vIdx, tupleInfo] : varToInfo) {
                    auto [memName, stintIdx, role] = tupleInfo;
                    if (memName == info.memberName && colValues[vIdx] > 0.5) {
                        stintViolations[stintIdx].push_back({priority, info.type, memName, role});
                        assignedAny = true;
                    }
                }
                
                if (!assignedAny) {
                    std::string reason = info.type;
                    if (priority == 2) {
                         if (info.type.find("Fair Share") != std::string::npos) 
                             reason = "Fair Share Rules violated";
                         else 
                             reason = "Rest Rules violated";
                    }
                    globalViolations.push_back(reason + " (" + info.memberName + ")");
                }
            }
        }
    }

    // --- Group & Format ---
    std::vector<int> violationStints;
    for(const auto& [s, _] : stintViolations) violationStints.push_back(s);
    std::sort(violationStints.begin(), violationStints.end());

    int startStint = -1;
    int prevStint = -1;
    std::string currentBlockReason = "";
    int currentPriority = 999;
    std::string currentMember = "";

    auto finalizeBlock = [&](int endStint) {
        if (startStint == -1) return;
        
        std::ostringstream ss;
        if (startStint == endStint) ss << "Stint " << startStint;
        else ss << "Stints " << startStint << "-" << endStint;

        ss << ": " << currentBlockReason;
        report.push_back(ss.str());

        // Actionable Advice
        if (currentPriority == 0) { 
             report.push_back("   -> Advice: Mark " + currentMember + " as Available during this window.");
        } else if (currentPriority == 1) { 
             report.push_back("   -> Advice: Increase 'Maximum Busy Hours' or add more participants.");
        } else if (currentPriority == 2) { 
             report.push_back("   -> Advice: Reduce 'Minimum Rest Hours' or shuffle previous stints.");
        }
    };

    for (int s : violationStints) {
        auto& vs = stintViolations[s];
        std::sort(vs.begin(), vs.end(), [](const Violation& a, const Violation& b){
            return a.priority < b.priority;
        });
        
        const auto& best = vs[0];
        std::string reason;
        
        if (best.priority == 0) {
            reason = std::string("No one could ") + (best.role == "Spotter" ? "spot" : "drive") + 
                     " without being Unavailable (" + best.member + ")";
        }
        else if (best.priority == 1) {
            reason = std::string("No one could ") + (best.role == "Spotter" ? "spot" : "drive") + 
                     " without exceeding Max Busy Time (" + best.member + ")";
        }
        else if (best.priority == 2) {
             if (best.description.find("Fair Share") != std::string::npos) 
                 reason = std::string("No one could ") + (best.role == "Spotter" ? "spot" : "drive") + 
                          " without breaking Fair Share Rules (" + best.member + ")";
             else 
                 reason = std::string("No one could ") + (best.role == "Spotter" ? "spot" : "drive") + 
                          " without breaking Rest Rules (" + best.member + ")";
        }
        else reason = best.description + " (" + best.member + ")";

        bool isContiguous = (prevStint != -1 && s == prevStint + 1);
        bool sameReason = (reason == currentBlockReason);

        if (isContiguous && sameReason) {
            prevStint = s;
        } else {
            finalizeBlock(prevStint);
            startStint = s;
            prevStint = s;
            currentBlockReason = reason;
            currentPriority = best.priority;
            currentMember = best.member;
        }
    }
    finalizeBlock(prevStint);

    // Append Global Violations (Unattributed)
    for (const auto& g : globalViolations) {
        report.push_back("Global Violation: " + g);
    }

    return report;
}

} // namespace jres::analysis