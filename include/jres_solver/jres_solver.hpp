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
    // Windows/MSVC: Handle DLL exports, imports, AND static builds
    #if defined(JRES_STATIC)
        // Static build: No special attributes needed
        #define JRES_SOLVER_API
    #elif defined(JRES_SOLVER_EXPORTS)
        // Building the DLL: Export symbols
        #define JRES_SOLVER_API __declspec(dllexport)
    #else
        // Using the DLL: Import symbols
        #define JRES_SOLVER_API __declspec(dllimport)
    #endif
#else
    // macOS/Linux (GCC/Clang): Use visibility attribute
    // This allows the shared library (.dylib/.so) to function correctly.
    #define JRES_SOLVER_API __attribute__((visibility("default")))
#endif


// Use standard C-style linking for compatibility
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief C-API enum for specifying the spotter scheduling mode.
 */
enum JresSpotterMode {
    JRES_SPOTTER_MODE_NONE = 0,       // No Spotter schedule is solved for
    JRES_SPOTTER_MODE_INTEGRATED = 1, // Driver and Spotter schedule is solved together
    JRES_SPOTTER_MODE_SEQUENTIAL = 2  // Driver schedule is prioritized
};

/**
 * @brief Options for the race schedule solver.
 */
struct JresSolverOptions {
    int timeLimit;               // Maximum time in seconds to let the solver run
    JresSpotterMode spotterMode; // Type of spotter scheduling to use
    bool allowNoSpotter;         // Allow stints to have no spotter assigned
    double optimalityGap;        // e.g., 0.01 for 1%
    bool quiet;                  // Suppress console logging from the library
};

/**
 * @brief Solves a race schedule.
 *
 * This function is thread-safe. It takes a JSON string of race data and a set of
 * options, and returns a JSON string containing the schedule or an error.
 *
 * @param raceDataJson A UTF-8 JSON string containing the race data.
 * @param options A struct containing the solver options.
 * @param outputJson A pointer that will be set to a newly allocated string
 * containing the JSON result. The caller is responsible for
 * freeing this string using free_solver_result().
 *
 * @return 0 on success.
 * @return -1 on failure (e.g., parse error, infeasible model).
 * On failure, outputJson will still be set to a JSON string
 * containing an "error" key.
 */
JRES_SOLVER_API int solve_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson);

/**
 * @brief Runs the diagnostic solver to explain why a schedule is infeasible.
 *
 * This uses a relaxed model with penalties to find the minimum set of
 * constraints that must be violated to make the schedule work.
 *
 * @param raceDataJson A UTF-8 JSON string containing the race data.
 * @param options A struct containing the solver options.
 * @param outputJson A pointer that will be set to a newly allocated string
 * containing the JSON result. The result will contain a "diagnosis" array.
 *
 * @return 0 on success (diagnosis ran successfully).
 * @return -1 on failure (internal error running diagnosis).
 */
JRES_SOLVER_API int diagnose_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson);

/**
 * @brief Frees the memory of the JSON result string.
 *
 * Use this function to free the string allocated and returned by
 * solve_race_schedule() or diagnose_race_schedule().
 *
 * @param resultJson The string to free.
 */
JRES_SOLVER_API void free_solver_result(char* resultJson);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // JRES_SOLVER_HPP
