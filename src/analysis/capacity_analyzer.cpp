/**
 * @author popmonkey+jres@gmail.com
 * @file src/analysis/capacity_analyzer.cpp
 * @brief Implementation of capacity analysis.
 */
#include "capacity_analyzer.hpp"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace jres::internal {

CapacityAnalysis CapacityAnalyzer::calculate_max_potential_capacity(
    const std::vector<TeamMember>& participants,
    const SolverInput& input)
{
    std::vector<std::time_t> startTimes;
    std::vector<std::time_t> endTimes;
    startTimes.reserve(input.stints.size());
    endTimes.reserve(input.stints.size());

    std::time_t raceStart;
    std::time_t raceEnd;
    bool raceTimesInit = false;

    for (const auto& stint : input.stints) {
        startTimes.push_back(stint.startTime);
        endTimes.push_back(stint.endTime);

        if(!raceTimesInit) {
            raceStart = stint.startTime;
            raceEnd = stint.endTime;
            raceTimesInit = true;
        } else {
            if(stint.startTime < raceStart) raceStart = stint.startTime;
            if(stint.endTime > raceEnd) raceEnd = stint.endTime;
        }
    }

    CapacityAnalysis analysis;
    analysis.totalCapacity = 0;
    std::ostringstream ss;

    for (const auto& p : participants) {
        // Build Availability
        std::vector<bool> is_available(input.stints.size(), true);
        auto member_availability_it = input.availability.find(p.nameId);
        if (member_availability_it != input.availability.end()) {
            for (size_t s = 0; s < input.stints.size(); ++s) {
                // Check if this stint time is unavailable
                std::time_t key = TimeHelpers::roundToHour(startTimes[s]);
                auto time_it = member_availability_it->second.find(key);
                if (time_it != member_availability_it->second.end() && 
                    time_it->second == Availability::Unavailable) {
                    is_available[s] = false;
                }
            }
        }

        std::vector<bool> planned_drive(input.stints.size(), false);
        int base_capacity = 0;
        double driver_total_hours = 0.0;

        for(size_t s=0; s<input.stints.size(); ++s) {
            if (is_available[s]) {
                planned_drive[s] = true;
                base_capacity++;
                
                double h = std::difftime(endTimes[s], startTimes[s]) / 3600.0;
                driver_total_hours += h;
            }
        }
        
        // Adjust for Global Minimum Rest (One Instance)
        int final_capacity = base_capacity;
        if (input.minimumRestHours > 0) {
             long long minRestSeconds = (long long)input.minimumRestHours * 3600;
             int min_loss = base_capacity; 
             bool found_valid_window = false;

             std::vector<std::time_t> candidateStarts;
             candidateStarts.push_back(raceStart);
             for(const auto& t : endTimes) candidateStarts.push_back(t);

             for(const auto& tStart : candidateStarts) {
                 auto tEnd = tStart + minRestSeconds;
                 if (tEnd > raceEnd) continue;
                 found_valid_window = true;

                 int current_loss = 0;
                 for(size_t s=0; s<input.stints.size(); ++s) {
                     if (planned_drive[s]) {
                         if (startTimes[s] < tEnd && endTimes[s] > tStart) {
                             current_loss++;
                         }
                     }
                 }
                 if (current_loss < min_loss) min_loss = current_loss;
             }
             
             if (!found_valid_window) {
                 final_capacity = 0; // Impossible to satisfy rest
             } else {
                 final_capacity -= min_loss;
             }
        }
        
        analysis.totalCapacity += final_capacity;
        
        std::string name = input.strings.get_string(p.nameId);
        ss << "\n- " << name << ": " << final_capacity 
           << " stints (approx " << std::fixed << std::setprecision(1) << driver_total_hours 
           << "h, MinRest=" << input.minimumRestHours << "h)";
    }
    analysis.details = ss.str();
    return analysis;
}

} // namespace jres::internal
