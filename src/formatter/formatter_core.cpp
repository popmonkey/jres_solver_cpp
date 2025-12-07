/**
 * @author popmonkey+jres@gmail.com
 * @file src/formatter/formatter_core.cpp
 * @brief Core logic for the JRES Schedule Formatter.
 */

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

std::map<std::string, MemberItinerary> jres::generate_member_itineraries(
    const std::vector<json>& schedule, 
    const json& data, 
    bool has_spotters
) {
    std::map<std::string, std::vector<DutyBlock>> raw_duties;
    std::map<std::string, int> tz_map;

    if (data.contains("teamMembers")) {
        for (const auto& m : data["teamMembers"]) {
            std::string name = m.value("name", "Unknown");
            raw_duties[name] = {};
            tz_map[name] = m.value("tzOffset", 0);
        }
    }

    DateTime race_start_utc;
    DateTime race_end_utc;
    bool first_stint = true;

    // Populate duties using the solved schedule and determine race boundaries
    for (const auto& entry : schedule) {
        DateTime start = DateTime::parse(entry.value("startTime", ""));
        DateTime end = DateTime::parse(entry.value("endTime", ""));
        int stint_num = entry.value("id", 0);

        if (first_stint) {
            race_start_utc = start;
            race_end_utc = end;
            first_stint = false;
        } else {
            if (start < race_start_utc) race_start_utc = start;
            if (end > race_end_utc) race_end_utc = end;
        }

        std::string driver = entry.value("driver", "N/A");
        if (driver != "N/A") {
            if (raw_duties.find(driver) == raw_duties.end()) {
                raw_duties[driver] = {};
                tz_map[driver] = 0; // Default tz
            }
            raw_duties[driver].push_back({start, end, "Driving", {stint_num}});
        }

        if (has_spotters && entry.contains("spotter")) {
            std::string spotter = entry.value("spotter", "N/A");
            if (spotter != "N/A") {
                if (raw_duties.find(spotter) == raw_duties.end()) {
                    raw_duties[spotter] = {};
                    tz_map[spotter] = 0; // Default tz
                }
                raw_duties[spotter].push_back({start, end, "Spotting", {stint_num}});
            }
        }
    }
    
    if (schedule.empty()) { // Handle case with no schedule
        return {};
    }

    std::map<std::string, MemberItinerary> final_itineraries;

    for (auto& [name, duties] : raw_duties) {
        final_itineraries[name] = {{}, tz_map[name]};
        
        std::sort(duties.begin(), duties.end(), [](const DutyBlock& a, const DutyBlock& b) {
            return a.start_utc < b.start_utc;
        });

        std::vector<DutyBlock> consolidated;
        if (!duties.empty()) {
            DutyBlock current = duties[0];
            for (size_t i = 1; i < duties.size(); ++i) {
                const DutyBlock& next = duties[i];
                double gap = next.start_utc.diff_seconds(current.end_utc);

                bool is_contiguous = (next.activity_type == current.activity_type) && (std::abs(gap) < 2.0); 

                if (is_contiguous) {
                    current.end_utc = next.end_utc;
                    current.stints.insert(current.stints.end(), next.stints.begin(), next.stints.end());
                } else {
                    consolidated.push_back(current);
                    current = next;
                }
            }
            consolidated.push_back(current);
        }

        int tz_offset = tz_map[name];
        DateTime last_duty_end_local = race_start_utc.add_hours(tz_offset);

        for (auto& duty : consolidated) {
            std::sort(duty.stints.begin(), duty.stints.end());

            DateTime start_local = duty.start_utc.add_hours(tz_offset);
            DateTime end_local = duty.end_utc.add_hours(tz_offset);

            double gap_seconds = start_local.diff_seconds(last_duty_end_local);
            if (gap_seconds > 1.0) {
                final_itineraries[name].items.push_back({last_duty_end_local, start_local, "Resting"});
            }

            std::string activity_str;
            if (duty.stints.size() == 1) {
                activity_str = duty.activity_type + " Stint #" + std::to_string(duty.stints[0]);
            } else {
                activity_str = duty.activity_type + " Stints #" + std::to_string(duty.stints.front()) + "-" + std::to_string(duty.stints.back());
            }
            
            final_itineraries[name].items.push_back({start_local, end_local, activity_str});
            last_duty_end_local = end_local;
        }
        
        DateTime race_end_local = race_end_utc.add_hours(tz_offset);
        
        if (race_end_local.diff_seconds(last_duty_end_local) > 1.0) {
            final_itineraries[name].items.push_back({last_duty_end_local, race_end_local, "Resting"});
        }
    }

    return final_itineraries;
}

std::string jres::generate_schedule_csv_string(const std::vector<json>& schedule, bool has_spotters) {
    std::ostringstream oss;
    oss << "Stint,Start Time (UTC),End Time (UTC),Assigned Driver";
    if (has_spotters) oss << ",Assigned Spotter";
    oss << "\n";

    for (const auto& entry : schedule) {
        oss << entry.value("id", 0) << "," 
            << entry.value("startTime", "") << "," 
            << entry.value("endTime", "") << "," 
            << entry.value("driver", "N/A");
        if (has_spotters) {
            oss << "," << (entry.contains("spotter") ? entry.value("spotter", "N/A") : "N/A");
        }
        oss << "\n";
    }
    return oss.str();
}

std::string jres::generate_schedule_ascii_table(const std::vector<json>& schedule, bool has_spotters) {
    if (schedule.empty()) return "No schedule data.\n";

    size_t w_stint = 5;  // "Stint"
    size_t w_start = 13; // "Start (UTC)"
    size_t w_end = 11;   // "End (UTC)"
    size_t w_driver = 6; // "Driver"
    size_t w_spot = 7;   // "Spotter"

    for (const auto& entry : schedule) {
        w_stint = std::max(w_stint, std::to_string(entry.value("id", 0)).length());
        w_driver = std::max(w_driver, entry.value("driver", "N/A").length());
        w_start = std::max(w_start, entry.value("startTime", "").length());
        w_end = std::max(w_end, entry.value("endTime", "").length());
        if (has_spotters) {
             w_spot = std::max(w_spot, entry.value("spotter", "N/A").length());
        }
    }

    size_t pad = 2;
    w_stint += pad; w_start += pad; w_end += pad; w_driver += pad; w_spot += pad;

    std::ostringstream oss;
    
    oss << std::left 
        << std::setw(w_stint) << "Stint"
        << std::setw(w_start) << "Start (UTC)"
        << std::setw(w_end)   << "End (UTC)"
        << std::setw(w_driver) << "Driver";
    if (has_spotters) oss << std::setw(w_spot) << "Spotter";
    oss << "\n";

    size_t total_width = w_stint + w_start + w_end + w_driver + (has_spotters ? w_spot : 0);
    oss << std::string(total_width, '-') << "\n";

    for (const auto& entry : schedule) {
        oss << std::left 
            << std::setw(w_stint) << entry.value("id", 0)
            << std::setw(w_start) << entry.value("startTime", "")
            << std::setw(w_end)   << entry.value("endTime", "")
            << std::setw(w_driver) << entry.value("driver", "N/A");
        
        if (has_spotters) {
             oss << std::setw(w_spot) << (entry.contains("spotter") ? entry.value("spotter", "N/A") : "N/A");
        }
        oss << "\n";
    }

    return oss.str();
}

std::string jres::generate_summary_csv_string(
    const std::map<std::string, int>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    bool has_spotters
) {
    std::ostringstream oss;
    oss << "Role,Name,Total Stints\n";
    
    for (const auto& [name, stint_count] : driver_stats) {
        oss << "Driver," << name << "," << stint_count << "\n";
    }
    
    if (has_spotters) {
        for (const auto& [name, count] : spotter_stats) {
            if (count > 0) {
                oss << "Spotter," << name << "," << count << "\n";
            }
        }
    }
    return oss.str();
}

std::string generate_itinerary_csv_string(const MemberItinerary& itinerary) {
    std::ostringstream oss;
    std::string tz_string = std::string("UTC") + (itinerary.tz_offset >= 0 ? "+" : "") + std::to_string(itinerary.tz_offset);
    oss << "Start Time (" << tz_string << "),End Time (" << tz_string << "),Duration,Activity\n";

    for (const auto& item : itinerary.items) {
         double dur = item.end_local.diff_seconds(item.start_local);
         oss << item.start_local.to_string() << "," 
             << item.end_local.to_string() << ","
             << DateTime::format_duration((long long)dur) << ","
             << item.activity << "\n";
    }
    return oss.str();
}

std::string generate_full_text_report(
    const std::vector<json>& schedule, 
    const std::map<std::string, int>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    const std::map<std::string, MemberItinerary>& itineraries,
    bool has_spotters
) {
    std::ostringstream f;
    f << "--- DRIVER SUMMARY ---\n";
    for (const auto& [name, stint_count] : driver_stats) {
        f << name << ": " << stint_count << " stints\n";
    }
    
    if (has_spotters) {
        f << "\n--- SPOTTER SUMMARY ---\n";
        for (const auto& [name, count] : spotter_stats) {
            if (count > 0) f << name << ": " << count << " stints\n";
        }
    }

    f << "\n--- SCHEDULE ---\n";
    f << jres::generate_schedule_ascii_table(schedule, has_spotters);
    
    f << "\n--- ITINERARIES ---\n";
    for (const auto& [name, itinerary] : itineraries) {
        if (itinerary.items.empty()) continue;
        
        std::string tz_string = std::string("UTC") + (itinerary.tz_offset >= 0 ? "+" : "") + std::to_string(itinerary.tz_offset);
        f << "\nSchedule for " << name << " (" << tz_string << "):\n";

        for (const auto& item : itinerary.items) {
             double dur = item.end_local.diff_seconds(item.start_local);
             f << "  " << item.start_local.to_string() << " to " << item.end_local.time_string() 
               << " (" << DateTime::format_duration((long long)dur) << "): " << item.activity << "\n";
        }
    }
    return f.str();
}

// --- Writers ---

void _write_to_txt(
    const std::vector<json>& schedule, 
    const std::map<std::string, int>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    const std::map<std::string, MemberItinerary>& itineraries,
    const std::string& filename,
    bool has_spotters
) {
    std::ofstream f(filename);
    f << generate_full_text_report(schedule, driver_stats, spotter_stats, itineraries, has_spotters);
}

void _write_to_zip(
    const std::vector<json>& schedule, 
    const std::map<std::string, int>& driver_stats,
    const std::map<std::string, int>& spotter_stats,
    const std::map<std::string, MemberItinerary>& itineraries,
    const std::string& filename,
    bool has_spotters
) {
    ZipWriter zip(filename);
    zip.add_file("master_schedule.csv", jres::generate_schedule_csv_string(schedule, has_spotters));
    zip.add_file("summaries.csv", jres::generate_summary_csv_string(driver_stats, spotter_stats, has_spotters));
    zip.add_file("summary.txt", generate_full_text_report(schedule, driver_stats, spotter_stats, itineraries, has_spotters));

    for (const auto& [name, itinerary] : itineraries) {
        if (itinerary.items.empty()) continue;
        std::string safe_name = name;
        std::replace(safe_name.begin(), safe_name.end(), ' ', '_');
        std::replace(safe_name.begin(), safe_name.end(), '/', '_');
        zip.add_file("itinerary_" + safe_name + ".csv", generate_itinerary_csv_string(itinerary));
    }
    zip.close();
}

void _write_to_csv_file(const std::vector<json>& schedule, const std::string& filename, bool has_spotters) {
    std::ofstream f(filename);
    f << jres::generate_schedule_csv_string(schedule, has_spotters);
}

void jres::write_output(
    const json& solved_data, 
    const std::string& output_file, 
    const std::string& format
) {
    if (!solved_data.contains("schedule") || !solved_data.contains("teamMembers")) {
        std::cerr << "Error: JSON missing 'schedule' or 'teamMembers' keys." << std::endl;
        return;
    }

    const auto& schedule = solved_data["schedule"];
    const auto& data = solved_data;
    
    bool has_spotters = false;
    if (!schedule.empty()) {
        const auto& first_entry = schedule[0];
        // Check if spotter field is present and not null/empty-string.
        has_spotters = first_entry.contains("spotter") && 
                       (!first_entry["spotter"].is_null()) && 
                       (!first_entry["spotter"].is_string() || !first_entry["spotter"].get<std::string>().empty());
    }

    std::map<std::string, int> driver_stats; 
    std::map<std::string, int> spotter_stats;

    if (data.contains("teamMembers")) {
        for (const auto& m : data["teamMembers"]) {
            // Read as int and convert to bool
            bool is_driver = m.value("isDriver", 0) == 1;
            bool is_spotter = m.value("isSpotter", 0) == 1;

            if (is_driver) driver_stats[m.value("name", "Unknown")] = 0;
            if (is_spotter) spotter_stats[m.value("name", "Unknown")] = 0;
        }
    }

    std::vector<json> sched_vec;
    for (const auto& item : schedule) {
        sched_vec.push_back(item);
        
        std::string driver = item.value("driver", "N/A");
        if (driver != "N/A") {
             if (driver_stats.find(driver) == driver_stats.end()) driver_stats[driver] = 0;
             driver_stats[driver]++;
        }
        
        if (has_spotters && item.contains("spotter")) {
            std::string spotter = item.value("spotter", "N/A");
            if (spotter != "N/A") {
                 if (spotter_stats.find(spotter) == spotter_stats.end()) spotter_stats[spotter] = 0;
                 spotter_stats[spotter]++;
            }
        }
    }

    auto member_itineraries = generate_member_itineraries(sched_vec, data, has_spotters);

    if (format == "csv") {
        _write_to_csv_file(sched_vec, output_file, has_spotters);
    } else if (format == "txt") {
        _write_to_txt(sched_vec, driver_stats, spotter_stats, member_itineraries, output_file, has_spotters);
    } else {
        std::string zip_file = output_file;
        if (format != "zip") {
             std::cerr << "Warning: Unsupported format '" << format << "'. Defaulting to ZIP." << std::endl;
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
