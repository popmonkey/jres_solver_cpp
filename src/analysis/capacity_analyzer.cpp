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
    // Parse stint times once
    std::vector<std::chrono::system_clock::time_point> startTimes;
    std::vector<std::chrono::system_clock::time_point> endTimes;
    startTimes.reserve(input.stints.size());
    endTimes.reserve(input.stints.size());

    std::chrono::system_clock::time_point raceStart;
    std::chrono::system_clock::time_point raceEnd;
    bool raceTimesInit = false;

    for (const auto& stint : input.stints) {
        auto s = TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = TimeHelpers::stringToTimePoint(stint.endTime);
        startTimes.push_back(s);
        endTimes.push_back(e);

        if(!raceTimesInit) {
            raceStart = s;
            raceEnd = e;
            raceTimesInit = true;
        } else {
            if(s < raceStart) raceStart = s;
            if(e > raceEnd) raceEnd = e;
        }
    }

    CapacityAnalysis analysis;
    analysis.totalCapacity = 0;
    std::ostringstream ss;

    for (const auto& p : participants) {
        // Build Availability
        std::vector<bool> is_available(input.stints.size(), true);
        auto member_availability_it = input.availability.find(p.name);
        if (member_availability_it != input.availability.end()) {
            for (size_t s = 0; s < input.stints.size(); ++s) {
                std::string key = TimeHelpers::timePointToKey(startTimes[s]);
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
                
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTimes[s] - startTimes[s]).count();
                driver_total_hours += static_cast<double>(duration_ms) / 3600000.0;
            }
        }
        
        // Adjust for Global Minimum Rest (One Instance)
        int final_capacity = base_capacity;
        if (input.minimumRestHours > 0) {
             auto minRestDuration = std::chrono::hours(input.minimumRestHours);
             int min_loss = base_capacity; 
             bool found_valid_window = false;

             std::vector<std::chrono::system_clock::time_point> candidateStarts;
             candidateStarts.push_back(raceStart);
             for(const auto& t : endTimes) candidateStarts.push_back(t);

             for(const auto& tStart : candidateStarts) {
                 auto tEnd = tStart + minRestDuration;
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
        
        ss << "\n- " << p.name << ": " << final_capacity 
           << " stints (approx " << std::fixed << std::setprecision(1) << driver_total_hours 
           << "h, MinRest=" << input.minimumRestHours << "h)";
    }
    analysis.details = ss.str();
    return analysis;
}

} // namespace jres::internal
