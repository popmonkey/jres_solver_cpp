#include "solver_diagnostics.hpp"
#include "../utils/date_utils.hpp"
#include <sstream>
#include <iomanip>
#include <set>
#include <chrono>

namespace jres::analysis {

std::string explain_assignment_failure(
    int stintIndex,
    const std::string& violationDriver,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::map<std::pair<std::string, int>, int>& driverWorkVars,
    const std::vector<double>& colValues)
{
    using namespace jres::internal;
    std::ostringstream ss;

    // Time points for the target stint
    auto tStart = TimeHelpers::stringToTimePoint(input.stints[stintIndex].startTime);
    auto tEnd = TimeHelpers::stringToTimePoint(input.stints[stintIndex].endTime);

    // Identify which drivers are assigned to which stints in the CURRENT solution
    std::map<std::string, std::set<int>> driverAssignments;
    for (size_t s = 0; s < input.stints.size(); ++s) {
        for (const auto& p : driverPool) {
            if (driverWorkVars.count({p.name, (int)s})) {
                int idx = driverWorkVars.at({p.name, (int)s});
                if (idx < (int)colValues.size() && colValues[idx] > 0.5) {
                    driverAssignments[p.name].insert((int)s);
                }
            }
        }
    }

    // Helper: Get stint duration in hours
    auto getDuration = [&](int sIdx) {
        auto s = TimeHelpers::stringToTimePoint(input.stints[sIdx].startTime);
        auto e = TimeHelpers::stringToTimePoint(input.stints[sIdx].endTime);
        return std::chrono::duration<double, std::ratio<3600>>(e - s).count();
    };

    ss << "      Alternatives Analysis:";
    
    for (const auto& candidate : driverPool) {
        if (candidate.name == violationDriver) continue;

        ss << "\n        - " << candidate.name << ": ";
        std::vector<std::string> reasons;

        // Check Availability
        bool isUnavailable = false;
        auto it = input.availability.find(candidate.name);
        if (it != input.availability.end()) {
            auto cursor = tStart;
            while (cursor < tEnd) {
                std::string key = TimeHelpers::timePointToKey(cursor);
                if (it->second.count(key) && it->second.at(key) == Availability::Unavailable) {
                    isUnavailable = true;
                    break;
                }
                cursor += std::chrono::hours(1);
            }
        }
        if (isUnavailable) {
            reasons.push_back("Also Unavailable");
        }

        // Check Consecutive Stints
        int consecutiveCount = 1;
        for (int k = stintIndex - 1; k >= 0; --k) {
            if (driverAssignments[candidate.name].count(k)) consecutiveCount++;
            else break;
        }
        for (size_t k = stintIndex + 1; k < input.stints.size(); ++k) {
            if (driverAssignments[candidate.name].count((int)k)) consecutiveCount++;
            else break;
        }

        if (consecutiveCount > input.consecutiveStints) {
            reasons.push_back("Max Consecutive Limit (" + std::to_string(consecutiveCount) + "/" + std::to_string(input.consecutiveStints) + ")");
        }

        // Check Minimum Rest
        if (input.minimumRestHours > 0) {
            double minRestSec = input.minimumRestHours * 3600.0;
            for (int assignedS : driverAssignments[candidate.name]) {
                auto s1_start = TimeHelpers::stringToTimePoint(input.stints[stintIndex].startTime);
                auto s1_end = TimeHelpers::stringToTimePoint(input.stints[stintIndex].endTime);
                auto s2_start = TimeHelpers::stringToTimePoint(input.stints[assignedS].startTime);
                auto s2_end = TimeHelpers::stringToTimePoint(input.stints[assignedS].endTime);

                double gap = 0.0;
                if (s1_end <= s2_start) gap = std::chrono::duration<double>(s2_start - s1_end).count();
                else if (s2_end <= s1_start) gap = std::chrono::duration<double>(s1_start - s2_end).count();
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
                if (driverAssignments[candidate.name].count(k)) busyDuration += getDuration(k);
                else break;
            }
            for (size_t k = stintIndex + 1; k < input.stints.size(); ++k) {
                if (driverAssignments[candidate.name].count((int)k)) busyDuration += getDuration(k);
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

} // namespace jres::analysis
