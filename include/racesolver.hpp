/**
 * @file racesolver.hpp
 * @brief Public C-API for the JRES Solver Library.
 *
 * This file defines the C-style interface for the solver,
 * allowing it to be called from other languages (C, Go, Python, etc.).
 *
 * The interface consists of two functions:
 * . solve_race_schedule: The main function to run the solver.
 * . free_solver_result: A function to free the memory allocated by the solver.
 */

#ifndef RACESOLVER_HPP
#define RACESOLVER_HPP

// Use standard C-style linking for compatibility
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Options for the race schedule solver.
 */
struct RaceSolverOptions {
    int timeLimit;           // Maximum time in seconds to let the solver run
    const char* spotterMode; // "none", "integrated", or "sequential"
    bool allowNoSpotter;     // Allow stints to have no spotter assigned
    double optimalityGap;    // e.g., 0.01 for 1%
    bool quiet;              // Suppress console logging from the library
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
int solve_race_schedule(const char* raceDataJson,
                        const RaceSolverOptions& options,
                        char** outputJson);

/**
 * @brief Frees the memory of the JSON result string.
 *
 * Use this function to free the string allocated and returned by
 * solve_race_schedule().
 *
 * @param resultJson The string to free.
 */
void free_solver_result(char* resultJson);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RACESOLVER_HPP
