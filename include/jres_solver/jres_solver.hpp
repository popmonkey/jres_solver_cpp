/**
 * @author popmonkey+jres@gmail.com
 * @file jres_solver.hpp
 * @brief Public C-API for the JRES Solver Library.
 *
 * This file defines the C-style interface for the solver,
 * allowing it to be called from other languages (C, Go, Python, etc.).
 *
 * The interface consists of functions to solve, diagnose, and manage memory.
 */

#ifndef JRES_SOLVER_HPP
#define JRES_SOLVER_HPP

#if defined(_WIN32)
    #if defined(JRES_STATIC)
        #define JRES_SOLVER_API
    #elif defined(JRES_SOLVER_EXPORTS)
        #define JRES_SOLVER_API __declspec(dllexport)
    #else
        #define JRES_SOLVER_API __declspec(dllimport)
    #endif
#else
    #define JRES_SOLVER_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Public C-API Enums and Structs ---

/**
 * @brief Enum for member availability.
 */
enum JresAvailability {
    JRES_AVAILABILITY_UNAVAILABLE,
    JRES_AVAILABILITY_AVAILABLE,
    JRES_AVAILABILITY_PREFERRED
};

/**
 * @brief Enum for spotter scheduling mode.
 */
enum JresSpotterMode {
    JRES_SPOTTER_MODE_NONE = 0,
    JRES_SPOTTER_MODE_INTEGRATED = 1,
    JRES_SPOTTER_MODE_SEQUENTIAL = 2
};

/**
 * @brief Options for the solver.
 */
struct JresSolverOptions {
    /** @brief Maximum time in seconds to let the solver run. */
    int timeLimit;
    /** @brief Type of spotter scheduling to use. */
    JresSpotterMode spotterMode;
    /** @brief Allow stints to have no spotter assigned. */
    bool allowNoSpotter;
    /** @brief The solver will terminate when the gap between the primal and dual objective bound is less than this value. */
    double optimalityGap;
    /** @brief Weight for coupling driver and spotter roles (integrated mode only). */
    double roleCouplingWeight;
    /** @brief Weight for adhering to a rotation beat or fairness metric (default: 0.0). */
    double rotationBeatWeight;
};

/**
 * @brief Represents a single team member.
 */
struct JresTeamMember {
    /** @brief Unique name of the team member. */
    const char* name;
    /** @brief 1 if the member can drive, 0 otherwise. */
    int isDriver;
    /** @brief 1 if the member can spot, 0 otherwise. */
    int isSpotter;
    /** @brief Timezone offset, in hours from UTC. */
    double tzOffset;
};

/**
 * @brief Represents a single race stint.
 */
struct JresStint {
    /** @brief Unique identifier for the stint. */
    int id;
    /** @brief ISO 8601 timestamp for the start of the stint. */
    const char* startTime;
    /** @brief ISO 8601 timestamp for the end of the stint. */
    const char* endTime;
};

/**
 * @brief Represents a single availability entry for a team member.
 */
struct JresAvailabilityEntry {
    /** @brief An ISO 8601 timestamp for the hour slot. */
    const char* time;
    /** @brief The availability for that hour. */
    JresAvailability availability;
};

/**
 * @brief Represents the availability for a single team member.
 */
struct JresMemberAvailability {
    /** @brief The name of the team member. */
    const char* name;
    /** @brief A pointer to an array of availability entries. */
    JresAvailabilityEntry* availability;
    /** @brief The number of availability entries for this member. */
    int availability_len;
};

/**
 * @brief The main input struct for the solver.
 */
struct JresSolverInput {
    /** @brief Required consecutive stints (drivers must do this many stints in a row, except potentially the last one). */
    int consecutiveStints;
    /** @brief Minimum rest time in hours required after a shift. */
    int minimumRestHours;
    /** @brief Maximum busy time in hours (driving or spotting) before a rest is required. */
    int maxBusyHours;
    /** @brief The name of the team member who must drive the first stint. */
    const char* firstStintDriver;
    /** @brief A pointer to an array of team members. */
    JresTeamMember* teamMembers;
    /** @brief The number of team members. */
    int teamMembers_len;
    /** @brief A pointer to an array of availability information. */
    JresMemberAvailability* availability;
    /** @brief The number of members with availability information. */
    int availability_len;
    /** @brief A pointer to an array of stints. */
    JresStint* stints;
    /** @brief The number of stints. */
    int stints_len;
};

/**
 * @brief Represents a single entry in the solved schedule.
 */
struct JresScheduleEntry {
    /** @brief The ID of the stint. */
    int id;
    /** @brief ISO 8601 timestamp for the start of the stint. */
    const char* startTime;
    /** @brief ISO 8601 timestamp for the end of the stint. */
    const char* endTime;
    /** @brief Name of the assigned driver. */
    const char* driver;
    /** @brief Name of the assigned spotter. */
    const char* spotter;
};

/**
 * @brief Solver performance and complexity metrics.
 */
struct JresSolverStats {
    /** @brief The number of columns in the solver model. */
    int modelColumns;
    /** @brief The number of rows in the solver model. */
    int modelRows;
    /** @brief The number of nodes explored by the solver. */
    int searchNodes;
    /** @brief The final optimality gap of the solution. */
    double finalGap;
    /** @brief The time taken to set up the model in milliseconds. */
    double setupDurationMs;
    /** @brief The time taken to solve for the drivers in milliseconds. */
    double driverSolveDurationMs;
    /** @brief The time taken to solve for the spotters in milliseconds (sequential mode only). */
    double spotterSolveDurationMs;
};

/**
 * @brief The main output struct from the solver.
 */
struct JresSolverOutput {
    /** @brief A pointer to an array of schedule entries. */
    JresScheduleEntry* schedule;
    /** @brief The number of schedule entries. */
    int schedule_len;
    /** @brief An array of strings with diagnostic information. Empty on success. */
    const char** diagnosis;
    /** @brief The number of diagnosis strings. */
    int diagnosis_len;
    /** @brief Solver performance and complexity metrics. */
    JresSolverStats* stats;
    /** @brief The options used to generate this solution. */
    JresSolverOptions* options;
    /** @brief A pointer to an array of team members, including their tzOffset. */
    JresTeamMember* teamMembers;
    /** @brief The number of team members. */
    int teamMembers_len;
};

// --- Public C-API Functions ---

JRES_SOLVER_API JresSolverOutput* solve_race_schedule(const JresSolverInput* input, const JresSolverOptions* options);
JRES_SOLVER_API JresSolverOutput* diagnose_race_schedule(const JresSolverInput* input, const JresSolverOptions* options);

JRES_SOLVER_API JresSolverInput* jres_input_from_json(const char* jsonData);
JRES_SOLVER_API char* jres_output_to_json(const JresSolverOutput* output);

JRES_SOLVER_API void free_jres_solver_input(JresSolverInput* input);
JRES_SOLVER_API void free_jres_solver_output(JresSolverOutput* output);
JRES_SOLVER_API const char* jres_get_last_error();
JRES_SOLVER_API void free_json_string(char* json_string);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // JRES_SOLVER_HPP