/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_internal_types.hpp
 * @brief Internal data structures for the JRES Solver library.
 */
#pragma once

#include "jres_solver/jres_solver.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace jres::internal {

// --- Time Helpers ---
namespace TimeHelpers {
    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string);
    std::string timePointToString(std::chrono::system_clock::time_point tp);
    std::string timePointToKey(std::chrono::system_clock::time_point tp);
}


// --- Data Structures ---

enum class Availability {
    Unavailable,
    Available,
    Preferred
};

struct TeamMember
{
    std::string name;
    bool isDriver = true;
    bool isSpotter = false;
    double tzOffset = 0.0;
};

struct Stint {
    int id;
    std::string startTime;
    std::string endTime;
};

struct SolverInput
{
    int consecutiveStints = 1;
    int minimumRestHours = 0;
    int maximumBusyHours = 8;
    std::string firstStintDriver;
    std::vector<TeamMember> teamMembers;
    std::map<std::string, std::map<std::string, Availability>> availability;
    std::vector<Stint> stints;
};

struct ScheduleEntry {
    int id;
    std::string startTime;
    std::string endTime;
    std::string driver;
    std::string spotter;
};

struct SlackInfo {
    std::string type;
    std::string memberName;
    int stintIndex;
    double limit = 0.0;
    double actual = 0.0;
};

struct SolverStats {
    int modelColumns = 0;
    int modelRows = 0;
    int searchNodes = 0;
    double finalGap = 0.0;
    double setupDurationMs = 0.0;
    double driverSolveDurationMs = 0.0;
    double spotterSolveDurationMs = 0.0;
};

struct InputConfig {
    int consecutiveStints = 1;
    int minimumRestHours = 0;
    int maximumBusyHours = 8;
    std::string firstStintDriver;
};

struct SolverOutput
{
    std::vector<ScheduleEntry> schedule;
    std::vector<std::string> diagnosis;
    SolverStats stats;
    std::vector<TeamMember> teamMembers;
    InputConfig config;
    // Add any other output fields here, like diagnosis or metrics
};

// --- Conversion Functions ---

Availability to_internal_availability(JresAvailability availability);
SolverInput from_c_input(const JresSolverInput* c_input);
JresSolverOutput* to_c_output(const SolverOutput& output, const JresSolverOptions& options);
char* allocate_and_copy(const std::string& s);

} // namespace jres::internal
