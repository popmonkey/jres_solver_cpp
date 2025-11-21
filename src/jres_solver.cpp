/**
 * @file jres_solver.cpp
 * @brief JRES Solver Library C-API Wrapper
 */

#include <string>
#include <stdexcept>
#include <cstring>

#include "jres_solver/jres_solver.hpp"
#include "jres_standard_solver.hpp"
#include "jres_diagnostic_solver.hpp"
#include "jres_solver_types.hpp"

using json = nlohmann::json;

// --- C-API Implementation ---

char* create_output_string(const std::string& s) {
    char* cstr = new char[s.length() + 1];
    std::strcpy(cstr, s.c_str());
    return cstr;
}

SpotterMode translate_spotter_mode(JresSpotterMode mode) {
    switch (mode) {
        case JRES_SPOTTER_MODE_NONE: return SpotterMode::None;
        case JRES_SPOTTER_MODE_INTEGRATED: return SpotterMode::Integrated;
        case JRES_SPOTTER_MODE_SEQUENTIAL: return SpotterMode::Sequential;
        default: throw std::runtime_error("Unknown spotter mode enum provided.");
    }
}

int solve_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson)
{
    try {
        json rawJsonData = json::parse(raceDataJson);
        SolverContext ctx;
        ctx.quiet = options.quiet;
        ctx.timeLimit = options.timeLimit;
        ctx.spotterMode = translate_spotter_mode(options.spotterMode);
        ctx.allowNoSpotter = options.allowNoSpotter;
        ctx.optimalityGap = options.optimalityGap;
        ctx.raceData = rawJsonData.get<RaceData>();

        // Use Standard Solver
        JresStandardSolver solver(ctx);
        json resultJson = solver.solve(); 

        *outputJson = create_output_string(resultJson.dump(4));
        return 0; 

    } catch (const std::exception& e) {
        json errorJson;
        errorJson["success"] = false;
        errorJson["error"] = e.what();
        *outputJson = create_output_string(errorJson.dump(4));
        return -1; 
    }
}

int diagnose_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson)
{
    try {
        json rawJsonData = json::parse(raceDataJson);
        SolverContext ctx;
        ctx.quiet = options.quiet;
        ctx.timeLimit = options.timeLimit;
        ctx.spotterMode = translate_spotter_mode(options.spotterMode);
        ctx.allowNoSpotter = options.allowNoSpotter;
        ctx.optimalityGap = options.optimalityGap;
        ctx.raceData = rawJsonData.get<RaceData>();

        // Use Diagnostic Solver
        JresDiagnosticSolver solver(ctx);
        json resultJson = solver.diagnose();

        *outputJson = create_output_string(resultJson.dump(4));
        return 0; 

    } catch (const std::exception& e) {
        json errorJson;
        errorJson["success"] = false;
        errorJson["error"] = std::string("Internal Diagnosis Error: ") + e.what();
        *outputJson = create_output_string(errorJson.dump(4));
        return -1; 
    }
}

void free_solver_result(char* resultJson)
{
    if (resultJson != nullptr) {
        delete[] resultJson;
    }
}
