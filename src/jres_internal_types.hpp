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
    std::time_t roundToHour(std::time_t t);
}


// --- Data Structures ---

using ID = int;

struct StringTable {
    std::vector<std::string> id_to_string;
    std::map<std::string, ID> string_to_id;

    ID get_id(const std::string& s) {
        auto it = string_to_id.find(s);
        if (it != string_to_id.end()) return it->second;
        ID id = (ID)id_to_string.size();
        id_to_string.push_back(s);
        string_to_id[s] = id;
        return id;
    }
    
    std::string get_string(ID id) const {
        if (id >= 0 && id < (ID)id_to_string.size()) return id_to_string[id];
        return "";
    }
};

enum class Availability {
    Unavailable,
    Available,
    Preferred
};

struct TeamMember
{
    ID nameId = -1;
    bool isDriver = true;
    bool isSpotter = false;
    double tzOffset = 0.0;
};

struct Stint {
    int id;
    std::time_t startTime;
    std::time_t endTime;
};

struct SolverInput
{
    int consecutiveStints = 1;
    int minimumRestHours = 0;
    int maximumBusyHours = 8;
    ID firstStintDriver = -1;
    std::vector<TeamMember> teamMembers;
    // Map: MemberID -> Time -> Availability
    std::map<ID, std::map<std::time_t, Availability>> availability;
    std::vector<Stint> stints;
    StringTable strings;
};

struct ScheduleEntry {
    int id;
    std::time_t startTime;
    std::time_t endTime;
    ID driverId = -1;
    ID spotterId = -1;
};

struct SlackInfo {
    std::string type;
    ID memberNameId = -1;
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
    ID firstStintDriver = -1;
};

struct SolverOutput
{
    std::vector<ScheduleEntry> schedule;
    std::vector<std::string> diagnosis;
    SolverStats stats;
    std::vector<TeamMember> teamMembers;
    InputConfig config;
    StringTable strings;
};

// --- Conversion Functions ---

Availability to_internal_availability(JresAvailability availability);
SolverInput from_c_input(const JresSolverInput* c_input);
JresSolverOutput* to_c_output(const SolverOutput& output, const JresSolverOptions& options);
char* allocate_and_copy(const std::string& s);

} // namespace jres::internal
