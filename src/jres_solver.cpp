/**
 * @file jres_solver.cpp
 * @brief JRES Solver Library C-API Wrapper
 * * This file is a "dumb" wrapper that translates C-API calls
 * into C++ class calls. All logic is in JresSolverImpl.
 */

// --- C++ Standard Libs ---
#include <string>
#include <stdexcept>
#include <cstring>   // For strlen, strcpy

// --- Our Public API Header ---
#include "jres_solver/jres_solver.hpp" // Note: This is a C-compatible header

// --- Our Private C++ Implementation ---
#include "jres_solver_impl.hpp"
#include "jres_solver_types.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- C-API Implementation ---

/**
 * @brief Allocates a C-string from a std::string and returns it.
 * The caller is responsible for freeing this with free_solver_result.
 */
char* create_output_string(const std::string& s) {
    char* cstr = new char[s.length() + 1];
    std::strcpy(cstr, s.c_str());
    return cstr;
}

/**
 * @brief The main C-API function.
 */
int solve_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson)
{
    try {
        // Parse Inputs
        json rawJsonData = json::parse(raceDataJson);

        // Build Context
        SolverContext ctx;
        ctx.quiet = options.quiet;
        ctx.timeLimit = options.timeLimit;
        ctx.spotterMode = options.spotterMode;
        ctx.allowNoSpotter = options.allowNoSpotter;
        ctx.optimalityGap = options.optimalityGap;
        ctx.raceData = rawJsonData.get<RaceData>();

        // Create C++ Solver and... SOLVE!
        //    All the complexity is hidden in this one line.
        JresSolverImpl solver(ctx);
        json resultJson = solver.solve(); // <-- All the magic happens here

        // Allocate and return output string
        *outputJson = create_output_string(resultJson.dump(4));
        return 0; // Success

    } catch (const std::exception& e) {
        // Handle errors
        json errorJson;
        errorJson["success"] = false;
        errorJson["error"] = e.what();
        *outputJson = create_output_string(errorJson.dump(4));
        return -1; // Failure
    }
}

/**
 * @brief The C-API function to free memory.
 */
void free_solver_result(char* resultJson)
{
    if (resultJson != nullptr) {
        delete[] resultJson;
    }
}
