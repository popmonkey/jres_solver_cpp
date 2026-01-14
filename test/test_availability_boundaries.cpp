/**
 * @file test/test_availability_boundaries.cpp
 * @brief Tests for precise availability boundary conditions.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(AvailabilityBoundaryTest, StintEndsExactlyAtUnavailableStart) {
    // Stint: 13:00 - 14:00. Unavailable at 14:00. Should be VALID.
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 1;
    j["minimumRestHours"] = 0;
    j["teamMembers"] = {{{"name", "D"}, {"isDriver", true}, {"isSpotter", false}}};
    j["stints"] = {{{"id", 1}, {"startTime", "2026-01-17T13:00:00.000Z"}, {"endTime", "2026-01-17T14:00:00.000Z"}}};
    j["availability"] = {{"D", {{"2026-01-17T13:00:00.000Z", "Available"}, {"2026-01-17T14:00:00.000Z", "Unavailable"}}}};
    j["firstStintDriver"] = nullptr;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    JresSolverOptions options = {};
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    bool hasViolation = false;
    for (int i = 0; i < output->diagnosis_len; ++i) {
        if (std::string(output->diagnosis[i]).find("No one could drive without being Unavailable") != std::string::npos) hasViolation = true;
    }

    EXPECT_FALSE(hasViolation);
    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

TEST(AvailabilityBoundaryTest, StintOverlapsUnavailableByOneMinute) {
    // Stint: 13:00 - 14:01. Unavailable at 14:00. Should be INVALID.
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 1;
    j["minimumRestHours"] = 0;
    j["teamMembers"] = {{{"name", "D"}, {"isDriver", true}, {"isSpotter", false}}};
    j["stints"] = {{{"id", 1}, {"startTime", "2026-01-17T13:00:00.000Z"}, {"endTime", "2026-01-17T14:01:00.000Z"}}};
    j["availability"] = {{"D", {{"2026-01-17T13:00:00.000Z", "Available"}, {"2026-01-17T14:00:00.000Z", "Unavailable"}}}};
    j["firstStintDriver"] = nullptr;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    JresSolverOptions options = {};
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    bool hasViolation = false;
    for (int i = 0; i < output->diagnosis_len; ++i) {
        if (std::string(output->diagnosis[i]).find("No one could drive without being Unavailable") != std::string::npos) hasViolation = true;
    }

    EXPECT_TRUE(hasViolation);
    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
