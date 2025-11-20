#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "utils/date_utils.hpp" // Needed for DateTime in structs

namespace jres {

    // --- Exposed Structures (for Testing) ---
    struct ItineraryItem {
        DateTime start_local;
        DateTime end_local;
        std::string activity;
    };

    // --- Core File Generation Function ---
    void write_output(const nlohmann::json& solved_data, const std::string& output_file, const std::string& format);

    // --- Exposed Helpers (for Testing) ---

    std::string generate_schedule_csv_string(
        const std::vector<nlohmann::json>& schedule, 
        bool has_spotters
    );

    std::string generate_summary_csv_string(
        const std::map<std::string, std::pair<int, int>>& driver_stats,
        const std::map<std::string, int>& spotter_stats,
        bool has_spotters
    );
    
    // Expose this complex logic for unit testing
    std::map<std::string, std::vector<ItineraryItem>> generate_member_itineraries(
        const std::vector<nlohmann::json>& schedule, 
        const nlohmann::json& data, 
        int pit_time_seconds, 
        bool has_spotters
    );

}
