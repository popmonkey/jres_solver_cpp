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

enum JresAvailability {
    JRES_AVAILABILITY_UNAVAILABLE,
    JRES_AVAILABILITY_AVAILABLE,
    JRES_AVAILABILITY_PREFERRED
};

enum JresSpotterMode {
    JRES_SPOTTER_MODE_NONE = 0,
    JRES_SPOTTER_MODE_INTEGRATED = 1,
    JRES_SPOTTER_MODE_SEQUENTIAL = 2
};

struct JresSolverOptions {
    int timeLimit;
    JresSpotterMode spotterMode;
    bool allowNoSpotter;
    double optimalityGap;
};

struct JresTeamMember {
    const char* name;
    int isDriver;
    int isSpotter;
    int maxStints;
    int minimumRestHours;
};

struct JresStint {
    int id;
    const char* startTime;
    const char* endTime;
};

struct JresAvailabilityEntry {
    const char* time;
    JresAvailability availability;
};

struct JresMemberAvailability {
    const char* name;
    JresAvailabilityEntry* availability;
    int availability_len;
};

struct JresSolverInput {
    JresTeamMember* teamMembers;
    int teamMembers_len;
    JresMemberAvailability* availability;
    int availability_len;
    JresStint* stints;
    int stints_len;
};

struct JresScheduleEntry {
    int stintId;
    const char* driver;
    const char* spotter;
};

struct JresSolverStats {
    int modelColumns;
    int modelRows;
    int searchNodes;
    double finalGap;
    double setupDurationMs;
    double driverSolveDurationMs;
    double spotterSolveDurationMs;
};

struct JresSolverOutput {
    JresScheduleEntry* schedule;
    int schedule_len;
    const char** diagnosis;
    int diagnosis_len;
    JresSolverStats* stats;
};

// --- Public C-API Functions ---

JRES_SOLVER_API JresSolverOutput* solve_race_schedule(const JresSolverInput* input, const JresSolverOptions* options);
JRES_SOLVER_API JresSolverOutput* diagnose_race_schedule(const JresSolverInput* input, const JresSolverOptions* options);

JRES_SOLVER_API JresSolverInput* jres_input_from_json(const char* jsonData);
JRES_SOLVER_API char* jres_output_to_json(const JresSolverOutput* output);

JRES_SOLVER_API void free_jres_solver_input(JresSolverInput* input);
JRES_SOLVER_API void free_jres_solver_output(JresSolverOutput* output);
JRES_SOLVER_API void free_json_string(char* json_string);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // JRES_SOLVER_HPP
