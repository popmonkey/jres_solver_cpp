#include "jres_solver/jres_json_converter.hpp"
#include "nlohmann/json.hpp"
#include <iostream>

using json = nlohmann::json;

// Helper function to convert string to JresAvailability
JresAvailability to_jres_availability(const std::string& s) {
    if (s == "Available") {
        return JRES_AVAILABILITY_AVAILABLE;
    } else if (s == "Unavailable") {
        return JRES_AVAILABILITY_UNAVAILABLE;
    } else if (s == "Preferred") {
        return JRES_AVAILABILITY_PREFERRED;
    }
    // Default to unavailable
    return JRES_AVAILABILITY_UNAVAILABLE;
}

// Helper to allocate and copy a string
char* allocate_and_copy(const std::string& s) {
    char* cstr = new char[s.length() + 1];
    std::strcpy(cstr, s.c_str());
    return cstr;
}

JresSolverInput* jres_input_from_json(const char* jsonData) {
    try {
        json j = json::parse(jsonData);
        JresSolverInput* input = new JresSolverInput();

        if (j.find("teamMembers") == j.end()) {
            throw std::runtime_error("Missing 'teamMembers' key in input JSON.");
        }
        if (j.find("stints") == j.end()) {
            throw std::runtime_error("Missing 'stints' key in input JSON.");
        }
        if (j.find("availability") == j.end()) {
            throw std::runtime_error("Missing 'availability' key in input JSON.");
        }

        // Team Members
        input->teamMembers_len = j["teamMembers"].size();
        input->teamMembers = new JresTeamMember[input->teamMembers_len];
        for (size_t i = 0; i < input->teamMembers_len; ++i) {
            auto& member_json = j["teamMembers"][i];
            input->teamMembers[i].name = allocate_and_copy(member_json["name"]);
            input->teamMembers[i].isDriver = member_json.value("isDriver", true);
            input->teamMembers[i].isSpotter = member_json.value("isSpotter", false);
            input->teamMembers[i].maxStints = member_json.value("maxStints", 1);
            input->teamMembers[i].minimumRestHours = member_json.value("minimumRestHours", 0);
        }

        // Stints
        input->stints_len = j["stints"].size();
        input->stints = new JresStint[input->stints_len];
        for (size_t i = 0; i < input->stints_len; ++i) {
            auto& stint_json = j["stints"][i];
            input->stints[i].id = stint_json["id"];
            input->stints[i].startTime = allocate_and_copy(stint_json["startTime"]);
            input->stints[i].endTime = allocate_and_copy(stint_json["endTime"]);
        }

        // Availability
        input->availability_len = j["availability"].size();
        input->availability = new JresMemberAvailability[input->availability_len];
        int member_idx = 0;
        for (auto& [name, avail_map] : j["availability"].items()) {
            input->availability[member_idx].name = allocate_and_copy(name);
            input->availability[member_idx].availability_len = avail_map.size();
            input->availability[member_idx].availability = new JresAvailabilityEntry[avail_map.size()];
            int avail_idx = 0;
            for (auto& [time, availability] : avail_map.items()) {
                input->availability[member_idx].availability[avail_idx].time = allocate_and_copy(time);
                input->availability[member_idx].availability[avail_idx].availability = to_jres_availability(availability);
                avail_idx++;
            }
            member_idx++;
        }

        return input;

    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return nullptr;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return nullptr;
    }
}


char* jres_output_to_json(const JresSolverOutput* output) {
    if (!output) {
        return nullptr;
    }

    try {
        json j;
        j["schedule"] = json::array();
        for (int i = 0; i < output->schedule_len; ++i) {
            json entry;
            entry["stintId"] = output->schedule[i].stintId;
            entry["driver"] = output->schedule[i].driver;
            entry["spotter"] = output->schedule[i].spotter;
            j["schedule"].push_back(entry);
        }

        j["diagnosis"] = json::array();
        for (int i = 0; i < output->diagnosis_len; ++i) {
            j["diagnosis"].push_back(output->diagnosis[i]);
        }
        
        if (output->stats) {
            json stats_json;
            stats_json["modelColumns"] = output->stats->modelColumns;
            stats_json["modelRows"] = output->stats->modelRows;
            stats_json["searchNodes"] = output->stats->searchNodes;
            stats_json["finalGap"] = output->stats->finalGap;
            stats_json["setupDurationMs"] = output->stats->setupDurationMs;
            stats_json["driverSolveDurationMs"] = output->stats->driverSolveDurationMs;
            stats_json["spotterSolveDurationMs"] = output->stats->spotterSolveDurationMs;
            j["stats"] = stats_json;
        }

        std::string json_str = j.dump();
        return allocate_and_copy(json_str);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return nullptr;
    }
}

void free_jres_solver_output(JresSolverOutput* output) {
    if (!output) {
        return;
    }

    for (int i = 0; i < output->schedule_len; ++i) {
        delete[] output->schedule[i].driver;
        delete[] output->schedule[i].spotter;
    }
    delete[] output->schedule;

    for (int i = 0; i < output->diagnosis_len; ++i) {
        delete[] output->diagnosis[i];
    }
    delete[] output->diagnosis;

    delete output->stats;
    delete output;
}

void free_jres_solver_input(JresSolverInput* input) {
    if (!input) {
        return;
    }

    for (int i = 0; i < input->teamMembers_len; ++i) {
        delete[] input->teamMembers[i].name;
    }
    delete[] input->teamMembers;

    for (int i = 0; i < input->stints_len; ++i) {
        delete[] input->stints[i].startTime;
        delete[] input->stints[i].endTime;
    }
    delete[] input->stints;

    for (int i = 0; i < input->availability_len; ++i) {
        for (int j = 0; j < input->availability[i].availability_len; ++j) {
            delete[] input->availability[i].availability[j].time;
        }
        delete[] input->availability[i].availability;
        delete[] input->availability[i].name;
    }
    delete[] input->availability;

    delete input;
}
