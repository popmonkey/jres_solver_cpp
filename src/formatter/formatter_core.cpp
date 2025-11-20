#include "formatter/formatter_core.hpp" 

#include "utils/date_utils.hpp"
#include "utils/zip_writer.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

using namespace jres;
using json = nlohmann::json;

// --- Internal Structures ---
struct DutyBlock {
    DateTime start_utc;
    DateTime end_utc;
    std::string activity_type; 
    std::vector<int> stints;
};

// --- Logic Implementation ---

std::map<std::string, std::vector<ItineraryItem>> jres::generate_member_itineraries(
    const std::vector<json>& schedule, 
    const json& data, 
    int pit_time_seconds, 
    bool has_spotters
) {
    std::map<std::string, std::vector<DutyBlock>> raw_duties;
    std::map<std::string, int> tz_map;

    // Initialize
    if (data.contains("teamMembers")) {
        for (const auto& m : data["teamMembers"]) {
            std::string name = m.value("name", "Unknown");
            raw_duties[name] = {};
            tz_map[name] = m.value("timezone", 0);
        }
    }

    // Collect Duties
    for (const auto& entry : schedule) {
        DateTime start = DateTime::parse(entry.value("startTimeUTC", ""));
        DateTime end = DateTime::parse(entry.value("endTimeUTC", ""));
        int stint_num = entry.value("stint", 0);
        std::string driver = entry.value("driver", "N/A");
        
        if (driver != "N/A") {
            if (raw_duties.find(driver) == raw_duties.end()) {
                raw_duties[driver] = {};
                tz_map[driver] = 0;
            }
            raw_duties[driver].push_back({start, end, "Driving", {stint_num}});
        }

        if (has_spotters && entry.contains("spotter")) {
            std::string spotter = entry.value("spotter", "N/A");
            if (spotter != "N/A") {
                if (raw_duties.find(spotter) == raw_duties.end()) {
                    raw_duties[spotter] = {};
                    tz_map[spotter] = 0;
                }
                raw_duties[spotter].push_back({start, end, "Spotting", {stint_num}});
            }
        }
    }

    DateTime race_start_utc = DateTime::parse(data.value("raceStartUTC", "1970-01-01T00:00:00Z"));
    
    std::map<std::string, std::vector<ItineraryItem>> final_itineraries;

    for (auto& [name, duties] : raw_duties) {
        final_itineraries[name] = {};
        
        std::sort(duties.begin(), duties.end(), [](const DutyBlock& a, const DutyBlock& b) {
            return a.start_utc < b.start_utc;
        });

        // Consolidate
        std::vector<DutyBlock> consolidated;
        if (!duties.empty()) {
            DutyBlock current = duties[0];
            for (size_t i = 1; i < duties.size(); ++i) {
                DutyBlock next = duties[i];
                double gap = next.start_utc.diff_seconds(current.end_utc);
                bool is_contiguous = (next.activity_type == current.activity_type) && 
                                     (std::abs(gap - pit_time_seconds) < 2.0); 

                if (is_contiguous) {
                    current.end_utc = next.end_utc;
                    current.stints.push_back(next.stints[0]);
                } else {
                    consolidated.push_back(current);
                    current = next;
                }
            }
            consolidated.push_back(current);
        }

        // Transform to Local Time
        int tz_offset = tz_map[name];
        DateTime last_duty_end_local = race_start_utc.add_hours(tz_offset);

        for (auto& duty : consolidated) {
            DateTime start_local = duty.start_utc.add_hours(tz_offset);
            DateTime end_local = duty.end_utc.add_hours(tz_offset);

            double gap_seconds = start_local.diff_seconds(last_duty_end_local);
            if (gap_seconds > 1.0 && std::abs(gap_seconds - pit_time_seconds) > 2.0) {
                final_itineraries[name].push_back({last_duty_end_local, start_local, "Resting"});
            }

            std::string activity_str;
            if (duty.stints.size() == 1) {
                activity_str = duty.activity_type + " Stint #" + std::to_string(duty.stints[0]);
            } else {
                activity_str = duty.activity_type + " Stints #" + std::to_string(duty.stints.front()) + "-" + std::to_string(duty.stints.back());
            }
            
            final_itineraries[name].push_back({start_local, end_local, activity_str});
            last_duty_end_local = end_local;
        }
        
        // We need duration for the final rest check
        double duration_h = data.value("durationHours", 24.0);
        // Use long long explicitly to avoid ambiguity with overloaded add_seconds/add_hours
        DateTime race_end_local = race_start_utc
            .add_hours(static_cast<int>(duration_h))
            .add_seconds(static_cast<long long>((duration_h - static_cast<int>(duration_h)) * 3600))
            .add_hours(tz_offset);
        
        if (race_end_local.diff_seconds(last_duty_end_local) > 1.0) {
            final_itineraries[name].push_back({last_duty_end_local, race_end_local, "Resting"});
        }
    }

    return final_itineraries;
}

std::string jres::generate_schedule_csv_string(const std::vector<json>& schedule, bool has_spotters) {
    std::ostringstream oss;
    oss << "Stint,Start Time (UTC),End Time (UTC),Assigned Driver";
    if (has_spotters) oss << ",Assigned Spotter";
    oss << ",Laps\n";

    for (const auto& entry : schedule) {
        oss << entry.value("stint", 0) << "," 
            << entry.value("startTimeUTC", "") << "," 
            << entry.value("endTimeUTC", "") << "," 
            << entry.value("driver", "N/A");
        if (has_spotters) {
            oss << "," << (entry.contains("spotter") ? entry.value("spotter", "N/A") : "N/A");
        }
        oss << "," << entry.value("laps", 0) << "\n";
    }
    return oss.str();
}

std::string jres::generate_summary_csv_string(
    const std::map<std::string, std::pair<int, int>>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    bool has_spotters
) {
    std::ostringstream oss;
    oss << "Role,Name,Total Stints,Total Laps\n";
    
    for (const auto& [name, stats] : driver_stats) {
        oss << "Driver," << name << "," << stats.first << "," << stats.second << "\n";
    }
    
    if (has_spotters) {
        for (const auto& [name, count] : spotter_stats) {
            if (count > 0) {
                oss << "Spotter," << name << "," << count << ",-\n";
            }
        }
    }
    return oss.str();
}

std::string generate_itinerary_csv_string(const std::vector<ItineraryItem>& itinerary) {
    std::ostringstream oss;
    oss << "Start Local,End Local,Duration,Activity\n";
    for (const auto& item : itinerary) {
         double dur = item.end_local.diff_seconds(item.start_local);
         oss << item.start_local.to_string() << "," 
             << item.end_local.to_string() << ","
             << DateTime::format_duration((long long)dur) << ","
             << item.activity << "\n";
    }
    return oss.str();
}

// --- Writers ---

void _write_to_csv_file(const std::vector<json>& schedule, const std::string& filename, bool has_spotters) {
    std::ofstream f(filename);
    f << jres::generate_schedule_csv_string(schedule, has_spotters);
}

void _write_to_txt(
    const std::vector<json>& schedule, 
    const std::map<std::string, std::pair<int, int>>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    const std::map<std::string, std::vector<ItineraryItem>>& itineraries,
    const std::string& filename,
    bool has_spotters
) {
    std::ofstream f(filename);
    f << "--- DRIVER SUMMARY ---\n";
    for (const auto& [name, stats] : driver_stats) {
        f << name << ": " << stats.first << " stints, " << stats.second << " laps\n";
    }
    
    if (has_spotters) {
        f << "\n--- SPOTTER SUMMARY ---\n";
        for (const auto& [name, count] : spotter_stats) {
            if (count > 0) f << name << ": " << count << " stints\n";
        }
    }

    f << "\n--- SCHEDULE ---\n";
    f << jres::generate_schedule_csv_string(schedule, has_spotters);
    
    f << "\n--- ITINERARIES ---\n";
    for (const auto& [name, items] : itineraries) {
        if (items.empty()) continue;
        f << "\nSchedule for " << name << ":\n";
        for (const auto& item : items) {
             double dur = item.end_local.diff_seconds(item.start_local);
             f << "  " << item.start_local.to_string() << " to " << item.end_local.time_string() 
               << " (" << DateTime::format_duration((long long)dur) << "): " << item.activity << "\n";
        }
    }
}

void _write_to_zip(
    const std::vector<json>& schedule, 
    const std::map<std::string, std::pair<int, int>>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    const std::map<std::string, std::vector<ItineraryItem>>& itineraries,
    const std::string& filename,
    bool has_spotters
) {
    ZipWriter zip(filename);
    zip.add_file("master_schedule.csv", jres::generate_schedule_csv_string(schedule, has_spotters));
    zip.add_file("summaries.csv", jres::generate_summary_csv_string(driver_stats, spotter_stats, has_spotters));

    for (const auto& [name, itinerary] : itineraries) {
        if (itinerary.empty()) continue;
        std::string safe_name = name;
        std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
        std::replace(safe_name.begin(), safe_name.end(), '/', '_');
        zip.add_file("itinerary_" + safe_name + ".csv", generate_itinerary_csv_string(itinerary));
    }
    zip.close();
}

void jres::write_output(const json& solved_data, const std::string& output_file, const std::string& format) {
    // SAFETY: Check keys before accessing
    if (!solved_data.contains("schedule") || !solved_data.contains("raceData")) {
        std::cerr << "Error: JSON missing 'schedule' or 'raceData' keys." << std::endl;
        return;
    }

    // NOTE: The schedule now comes Pre-Enriched from the Solver!
    const auto& schedule = solved_data["schedule"];
    const auto& data = solved_data["raceData"];
    
    bool has_spotters = false;
    if (!schedule.empty()) has_spotters = schedule[0].contains("spotter");

    std::map<std::string, std::pair<int, int>> driver_stats; 
    std::map<std::string, int> spotter_stats;

    if (data.contains("teamMembers")) {
        for (const auto& m : data["teamMembers"]) {
            if (m.value("isDriver", false)) driver_stats[m.value("name", "Unknown")] = {0, 0};
            if (m.value("isSpotter", false)) spotter_stats[m.value("name", "Unknown")] = 0;
        }
    }

    std::vector<json> sched_vec;
    for (const auto& item : schedule) {
        sched_vec.push_back(item);
        
        std::string driver = item.value("driver", "N/A");
        int laps = item.value("laps", 0); // Read directly!
        
        if (driver != "N/A") {
             if (driver_stats.find(driver) == driver_stats.end()) driver_stats[driver] = {0, 0};
             driver_stats[driver].first++;
             driver_stats[driver].second += laps;
        }
        
        if (has_spotters && item.contains("spotter")) {
            std::string spotter = item.value("spotter", "N/A");
            if (spotter != "N/A") {
                 if (spotter_stats.find(spotter) == spotter_stats.end()) spotter_stats[spotter] = 0;
                 spotter_stats[spotter]++;
            }
        }
    }

    int pit_time = data.value("pitTimeInSeconds", 0);
    auto member_itineraries = generate_member_itineraries(sched_vec, data, pit_time, has_spotters);

    if (format == "csv") {
        _write_to_csv_file(sched_vec, output_file, has_spotters);
    } else if (format == "txt") {
        _write_to_txt(sched_vec, driver_stats, spotter_stats, member_itineraries, output_file, has_spotters);
    } else {
        // Default to ZIP
        std::string zip_file = output_file;
        if (format == "xlsx") {
             std::cerr << "Warning: XLSX support has been removed. Generating ZIP instead." << std::endl;
             size_t lastindex = output_file.find_last_of("."); 
             if (lastindex != std::string::npos) { 
                 zip_file = output_file.substr(0, lastindex) + ".zip"; 
             } else {
                 zip_file += ".zip";
             }
        }
        _write_to_zip(sched_vec, driver_stats, spotter_stats, member_itineraries, zip_file, has_spotters);
    }
}
