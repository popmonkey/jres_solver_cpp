/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_internal_types.cpp
 * @brief Internal data structures and conversion functions for the JRES Solver library.
 */
#include "jres_internal_types.hpp"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace jres::internal {

namespace TimeHelpers {
    // Portable timegm replacement
    std::time_t timegm_portable(std::tm *tm)
    {
#if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(tm);
#else
        return timegm(tm);
#endif
    }

    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string)
    {
        std::tm tm = {};
        std::stringstream ss(utc_string);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return std::chrono::system_clock::from_time_t(timegm_portable(&tm));
    }

    std::string timePointToString(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string timePointToKey(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:00:00.000Z");
        return ss.str();
    }
}


Availability to_internal_availability(JresAvailability availability) {
    switch (availability) {
        case JRES_AVAILABILITY_AVAILABLE:
            return Availability::Available;
        case JRES_AVAILABILITY_UNAVAILABLE:
            return Availability::Unavailable;
        case JRES_AVAILABILITY_PREFERRED:
            return Availability::Preferred;
        default:
            return Availability::Unavailable;
    }
}

SolverInput from_c_input(const JresSolverInput* c_input) {
    SolverInput input;

    for (int i = 0; i < c_input->teamMembers_len; ++i) {
        TeamMember member;
        member.name = c_input->teamMembers[i].name;
        member.isDriver = c_input->teamMembers[i].isDriver;
        member.isSpotter = c_input->teamMembers[i].isSpotter;
        member.maxStints = c_input->teamMembers[i].maxStints;
        member.minimumRestHours = c_input->teamMembers[i].minimumRestHours;
        input.teamMembers.push_back(member);
    }

    for (int i = 0; i < c_input->stints_len; ++i) {
        Stint stint;
        stint.id = c_input->stints[i].id;
        stint.startTime = c_input->stints[i].startTime;
        stint.endTime = c_input->stints[i].endTime;
        input.stints.push_back(stint);
    }

    for (int i = 0; i < c_input->availability_len; ++i) {
        std::string name = c_input->availability[i].name;
        for (int j = 0; j < c_input->availability[i].availability_len; ++j) {
            std::string time = c_input->availability[i].availability[j].time;
            JresAvailability availability = c_input->availability[i].availability[j].availability;
            input.availability[name][time] = to_internal_availability(availability);
        }
    }

    return input;
}

// Helper to allocate and copy a string
char* allocate_and_copy(const std::string& s) {
    char* cstr = new char[s.length() + 1];
    std::strcpy(cstr, s.c_str());
    return cstr;
}

JresSolverOutput* to_c_output(const SolverOutput& output) {
    JresSolverOutput* c_output = new JresSolverOutput();
    c_output->schedule_len = output.schedule.size();
    c_output->schedule = new JresScheduleEntry[c_output->schedule_len];

    for (size_t i = 0; i < output.schedule.size(); ++i) {
        c_output->schedule[i].id = output.schedule[i].id;
        c_output->schedule[i].startTime = allocate_and_copy(output.schedule[i].startTime);
        c_output->schedule[i].endTime = allocate_and_copy(output.schedule[i].endTime);
        c_output->schedule[i].driver = allocate_and_copy(output.schedule[i].driver);
        c_output->schedule[i].spotter = allocate_and_copy(output.schedule[i].spotter);
    }

    c_output->diagnosis_len = output.diagnosis.size();
    c_output->diagnosis = new const char*[c_output->diagnosis_len];
    for (size_t i = 0; i < output.diagnosis.size(); ++i) {
        c_output->diagnosis[i] = allocate_and_copy(output.diagnosis[i]);
    }

    c_output->stats = new JresSolverStats();
    c_output->stats->modelColumns = output.stats.modelColumns;
    c_output->stats->modelRows = output.stats.modelRows;
    c_output->stats->searchNodes = output.stats.searchNodes;
    c_output->stats->finalGap = output.stats.finalGap;
    c_output->stats->setupDurationMs = output.stats.setupDurationMs;
    c_output->stats->driverSolveDurationMs = output.stats.driverSolveDurationMs;
    c_output->stats->spotterSolveDurationMs = output.stats.spotterSolveDurationMs;

    c_output->teamMembers_len = output.teamMembers.size();
    c_output->teamMembers = new JresTeamMember[c_output->teamMembers_len];
    for (size_t i = 0; i < output.teamMembers.size(); ++i) {
        c_output->teamMembers[i].name = allocate_and_copy(output.teamMembers[i].name);
        c_output->teamMembers[i].isDriver = output.teamMembers[i].isDriver;
        c_output->teamMembers[i].isSpotter = output.teamMembers[i].isSpotter;
        c_output->teamMembers[i].maxStints = output.teamMembers[i].maxStints;
        c_output->teamMembers[i].minimumRestHours = output.teamMembers[i].minimumRestHours;
    }

    return c_output;
}

} // namespace jres::internal
