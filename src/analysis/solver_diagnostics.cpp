/**
 * @author popmonkey+jres@gmail.com
 * @file src/analysis/solver_diagnostics.cpp
 * @brief Implementation of solver diagnostics.
 */
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
    jres::internal::ID violationDriverId,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverWorkVars,
    const std::vector<double>& colValues)
{
    using namespace jres::internal;
    std::ostringstream ss;

    // Time points for the target stint
    auto tStart = input.stints[stintIndex].startTime;
    auto tEnd = input.stints[stintIndex].endTime;

    // Identify which drivers are assigned to which stints in the CURRENT solution
    std::map<ID, std::set<int>> driverAssignments;
    for (size_t s = 0; s < input.stints.size(); ++s) {
        for (const auto& p : driverPool) {
            if (driverWorkVars.count({p.nameId, (int)s})) {
                int idx = driverWorkVars.at({p.nameId, (int)s});
                if (idx < (int)colValues.size() && colValues[idx] > 0.5) {
                    driverAssignments[p.nameId].insert((int)s);
                }
            }
        }
    }

    // Helper: Get stint duration in hours
    auto getDuration = [&](int sIdx) {
        return std::difftime(input.stints[sIdx].endTime, input.stints[sIdx].startTime) / 3600.0;
    };

    ss << "      Alternatives Analysis:";
    
    for (const auto& candidate : driverPool) {
        if (candidate.nameId == violationDriverId) continue;

        std::string candidateName = input.strings.get_string(candidate.nameId);

        ss << "\n        - " << candidateName << ": ";
        std::vector<std::string> reasons;

        // Check Availability
        bool isUnavailable = false;
        auto it = input.availability.find(candidate.nameId);
        if (it != input.availability.end()) {
            auto cursor = tStart;
            // Align cursor to hour if not already (assuming availability is hourly)
            cursor = TimeHelpers::roundToHour(cursor);
            
            while (cursor < tEnd) {
                if (it->second.count(cursor) && it->second.at(cursor) == Availability::Unavailable) {
                    isUnavailable = true;
                    break;
                }
                cursor += 3600;
            }
        }
        if (isUnavailable) {
            reasons.push_back("Also Unavailable");
        }

        // Check Consecutive Stints
        int consecutiveCount = 1;
        for (int k = stintIndex - 1; k >= 0; --k) {
            if (driverAssignments[candidate.nameId].count(k)) consecutiveCount++;
            else break;
        }
        for (size_t k = stintIndex + 1; k < input.stints.size(); ++k) {
            if (driverAssignments[candidate.nameId].count((int)k)) consecutiveCount++;
            else break;
        }

        if (consecutiveCount > input.consecutiveStints) {
            reasons.push_back("Max Consecutive Limit (" + std::to_string(consecutiveCount) + "/" + std::to_string(input.consecutiveStints) + ")");
        }

        // Check Minimum Rest
        if (input.minimumRestHours > 0) {
            double minRestSec = input.minimumRestHours * 3600.0;
            for (int assignedS : driverAssignments[candidate.nameId]) {
                auto s1_start = input.stints[stintIndex].startTime;
                auto s1_end = input.stints[stintIndex].endTime;
                auto s2_start = input.stints[assignedS].startTime;
                auto s2_end = input.stints[assignedS].endTime;

                double gap = 0.0;
                if (s1_end <= s2_start) gap = std::difftime(s2_start, s1_end);
                else if (s2_end <= s1_start) gap = std::difftime(s1_start, s2_end);
                else gap = -1.0; 

                if (gap < minRestSec - 1.0) {
                    double needed = (minRestSec - gap) / 3600.0;
                    std::ostringstream rss;
                    rss << std::fixed << std::setprecision(1) << " Needs " << needed << "h more rest";
                    reasons.push_back(rss.str());
                    break;
                }
            }
        }

        // Check Max Busy Time
        if (input.maximumBusyHours > 0) {
            double busyDuration = getDuration(stintIndex);
            for (int k = stintIndex - 1; k >= 0; --k) {
                if (driverAssignments[candidate.nameId].count(k)) busyDuration += getDuration(k);
                else break;
            }
            for (size_t k = stintIndex + 1; k < input.stints.size(); ++k) {
                if (driverAssignments[candidate.nameId].count((int)k)) busyDuration += getDuration(k);
                else break;
            }
            if (busyDuration > input.maximumBusyHours) {
                 reasons.push_back("Max Busy Time Exceeded (" + std::to_string(busyDuration) + "h > " + std::to_string(input.maximumBusyHours) + "h)");
            }
        }

        if (reasons.empty()) {
             ss << "Available (Unknown constraint or softer optimization preference)";
        } else {
             for (size_t i=0; i<reasons.size(); ++i) {
                 if (i > 0) ss << ", ";
                 ss << reasons[i];
             }
        }
    }
    return ss.str();
}

/**
 * @brief Helper to format a list of stint indices into a human-readable string using IDs.
 */
std::string formatStintList(const std::vector<int>& indices, const jres::internal::SolverInput& input) {
    if (indices.empty()) return "";
    
    std::vector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    
    std::ostringstream ss;
    for (size_t i = 0; i < sorted.size(); ++i) {
        int startIdx = sorted[i];
        int endIdx = startIdx;
        while (i + 1 < sorted.size() && sorted[i + 1] == endIdx + 1) {
            endIdx = sorted[++i];
        }
        
        if (ss.tellp() > 0) ss << ", ";
        if (startIdx == endIdx) ss << input.stints[startIdx].id;
        else ss << input.stints[startIdx].id << "-" << input.stints[endIdx].id;
    }
    return ss.str();
}

std::vector<std::string> formatHumanDiagnostic(
    const std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::set<int>& unavailableVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverWorkVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& spotterWorkVars,
    const std::vector<double>& colValues,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::vector<jres::internal::TeamMember>& spotterPool)
{
    using namespace jres::internal;
    std::vector<std::string> report;

    // --- Roster Health Check (Drivers) ---
    if (!driverPool.empty()) {
        auto cap = CapacityAnalyzer::calculate_max_potential_capacity(driverPool, input);
        if (cap.totalCapacity < (int)input.stints.size()) {
            std::ostringstream ss;
            ss << "Driver Roster Understaffed: You have " << cap.totalCapacity 
               << " stints of coverage for a " << input.stints.size() << " stint race.";
            report.push_back(ss.str());
        }
    }

    // --- Collect Violations per Stint ---
    struct ViolationKey {
        int priority;
        std::string reason;
        std::string member;
        
        bool operator<(const ViolationKey& other) const {
            return std::tie(priority, reason, member) < std::tie(other.priority, other.reason, other.member);
        }
    };

    std::map<ViolationKey, std::vector<int>> groupedViolations;
    std::vector<std::string> globalViolations;

    // Build reverse map for variable index -> (MemberName, Stint, Role)
    std::map<int, std::tuple<std::string, int, std::string>> varToInfo;
    
    for(const auto& [key, varIdx] : driverWorkVars) {
        varToInfo[varIdx] = std::make_tuple(input.strings.get_string(key.first), key.second, "Driver");
    }
    for(const auto& [key, varIdx] : spotterWorkVars) {
        varToInfo[varIdx] = std::make_tuple(input.strings.get_string(key.first), key.second, "Spotter");
    }

    // Check Unavailable (Priority 0)
    for (int varIdx : unavailableVars) {
        if (varIdx < (int)colValues.size() && colValues[varIdx] > 0.5) {
            if (varToInfo.count(varIdx)) {
                auto [member, stintIdx, role] = varToInfo[varIdx];
                std::string reason = std::string("No one could ") + (role == "Spotter" ? "spot" : "drive") + 
                                     " without being Unavailable (" + member + ")";
                groupedViolations[{0, reason, member}].push_back(stintIdx);
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
            
            std::string infoMemberName = input.strings.get_string(info.memberNameId);

            if (info.stintIndex >= 0) {
                 std::string reason;
                 if (priority == 1) reason = "Max Busy Time exceeded (" + infoMemberName + ")";
                 else if (priority == 2) {
                     if (info.type.find("Fair Share") != std::string::npos) reason = "Fair Share Rules violated (" + infoMemberName + ")";
                     else reason = "Rest Rules violated (" + infoMemberName + ")";
                 }
                 else reason = info.type + " (" + infoMemberName + ")";

                 groupedViolations[{priority, reason, infoMemberName}].push_back(info.stintIndex);
            } else {
                // Try to attribute global violation to stints
                bool assignedAny = false;
                for(const auto& [vIdx, tupleInfo] : varToInfo) {
                    auto [memName, stintIdx, role] = tupleInfo;
                    if (memName == infoMemberName && colValues[vIdx] > 0.5) {
                        std::string reason;
                        if (priority == 1) reason = "Max Busy Time exceeded (" + memName + ")";
                        else if (priority == 2) {
                            if (info.type.find("Fair Share") != std::string::npos) reason = "Fair Share Rules violated (" + memName + ")";
                            else reason = "Rest Rules violated (" + memName + ")";
                        }
                        else reason = info.type + " (" + memName + ")";

                        groupedViolations[{priority, reason, memName}].push_back(stintIdx);
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
                    globalViolations.push_back(reason + " (" + infoMemberName + ")");
                }
            }
        }
    }

    // --- Sort & Format Grouped Violations ---
    std::vector<ViolationKey> sortedKeys;
    for(const auto& [key, _] : groupedViolations) sortedKeys.push_back(key);
    std::sort(sortedKeys.begin(), sortedKeys.end());

    for (const auto& key : sortedKeys) {
        const auto& stints = groupedViolations[key];
        std::string stintStr = formatStintList(stints, input);
        
        std::ostringstream ss;
        if (stints.size() == 1) ss << "Stint " << stintStr << ": " << key.reason;
        else ss << "Stints " << stintStr << ": " << key.reason;
        report.push_back(ss.str());

        // Actionable Advice
        if (key.priority == 0) { 
             report.push_back("   -> Advice: Mark " + key.member + " as Available during this window.");
        } else if (key.priority == 1) { 
             report.push_back("   -> Advice: Increase 'Maximum Busy Hours' (currently " + std::to_string(input.maximumBusyHours) + "h) or add more participants.");
        } else if (key.priority == 2) { 
             if (key.reason.find("Fair Share") != std::string::npos)
                 report.push_back("   -> Advice: Ensure " + key.member + " is Available for more stints to meet the minimum driving time.");
             else
                 report.push_back("   -> Advice: Reduce 'Minimum Rest Hours' (currently " + std::to_string(input.minimumRestHours) + "h) or increase availability of other participants to cover the gap.");
        }
    }

    // Append Global Violations (Unattributed)
    for (const auto& g : globalViolations) {
        report.push_back("Global Violation: " + g);
    }

    return report;
}

} // namespace jres::analysis
