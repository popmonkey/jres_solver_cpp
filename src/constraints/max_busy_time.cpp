/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/max_busy_time.cpp
 * @brief Implementation of maximum busy time constraints.
 */
#include "max_busy_time.hpp"
#include "Highs.h"
#include <chrono>
#include <map>

namespace jres::constraints {

void apply_max_busy_time_constraints(
    Highs &highs,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember> &participants,
    const std::map<std::pair<std::string, int>, int>& driverVars,
    const std::map<std::pair<std::string, int>, int>& spotterVars,
    bool enforceCombined,
    std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::vector<jres::internal::ScheduleEntry>* fixedSchedule
)
{
    using namespace jres::internal;

    if (input.maximumBusyHours <= 0) return;
    
    // Calculate durations
    std::vector<double> stintDurations;
    stintDurations.reserve(input.stints.size());
    for (const auto& stint : input.stints) {
        auto s = TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = TimeHelpers::stringToTimePoint(stint.endTime);
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
        stintDurations.push_back(static_cast<double>(ms) / 3600000.0);
    }

    for (const auto &p : participants)
    {
        for (size_t s = 0; s < input.stints.size(); ++s) {
            double currentDuration = 0.0;
            for (size_t e = s; e < input.stints.size(); ++e) {
                currentDuration += stintDurations[e];
                
                if (currentDuration > input.maximumBusyHours) {
                    // Violation if assigned to ALL stints in [s, e]
                    // Constraint: Sum(coeff * x[k]) <= (e - s)
                    
                    std::map<int, double> coefficients;
                    int fixedAssignments = 0;

                    for (size_t k = s; k <= e; ++k) {
                        // Driver
                        if (fixedSchedule) {
                            // Sequential Mode: Check fixed schedule
                            if (k < fixedSchedule->size() && (*fixedSchedule)[k].driver == p.name) {
                                fixedAssignments++;
                            }
                        } else {
                            // Integrated Mode: Add driver var to constraint
                            if (driverVars.count({p.name, (int)k})) {
                                coefficients[driverVars.at({p.name, (int)k})] += 1.0;
                            }
                        }
                        
                        // Spotter
                        if (spotterVars.count({p.name, (int)k})) {
                             if (fixedSchedule || enforceCombined) {
                                 coefficients[spotterVars.at({p.name, (int)k})] += 1.0;
                             }
                        }
                    }
                    
                    std::vector<int> idx;
                    std::vector<double> val;
                    idx.reserve(coefficients.size());
                    val.reserve(coefficients.size());

                    for(const auto& [col, coeff] : coefficients) {
                        idx.push_back(col);
                        val.push_back(coeff);
                    }
                    
                    double maxAssignments = static_cast<double>(e - s);
                    
                    if (fixedSchedule) {
                        // Adjust RHS
                        maxAssignments -= fixedAssignments;
                    }
                    
                    if (!idx.empty() || maxAssignments < 0) {
                        highs.addRow(-kHighsInf, maxAssignments, (int)idx.size(), idx.data(), val.data());
                    }
                    
                    break; 
                }
            }
        }
    }
}

} // namespace jres::constraints