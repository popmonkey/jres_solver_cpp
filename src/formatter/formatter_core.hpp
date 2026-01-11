/**
 * @author popmonkey+jres@gmail.com
 * @file src/formatter/formatter_core.hpp
 * @brief Core header for the JRES Schedule Formatter.
 */

 #pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "utils/date_utils.hpp" 

namespace jres {

    // --- Exposed Structures (for Testing) ---
    struct ItineraryItem {
        DateTime start_local;
        DateTime end_local;
        std::string activity;
    };

    struct MemberItinerary {
        std::vector<ItineraryItem> items;
        int tz_offset;
    };

    // --- Core File Generation Function ---
    void write_output(const nlohmann::json& solved_data, 
                      const std::string& output_file, 
                      const std::string& format);

    /**
     * @brief Returns the version string of the library.
     */
    const char* get_version();

    // --- Exposed Helpers (for Testing) ---

    std::string generate_schedule_csv_string(
        const std::vector<nlohmann::json>& schedule, 
        bool has_spotters
    );

    // New Helper for ASCII table generation
    std::string generate_schedule_ascii_table(
        const std::vector<nlohmann::json>& schedule, 
        bool has_spotters
    );

    std::string generate_summary_csv_string(
        const std::map<std::string, int>& driver_stats,
        const std::map<std::string, int>& spotter_stats,
        bool has_spotters
    );
    
    std::map<std::string, MemberItinerary> generate_member_itineraries(
        const std::vector<nlohmann::json>& schedule, 
        const nlohmann::json& data, 
        bool has_spotters
    );

}
