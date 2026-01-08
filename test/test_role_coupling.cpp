#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(RoleCouplingTest, SelectionLogic) {
    // Scenario: 3 People: A, B, C.
    // A: Driver+Spotter, Max 1 stint.
    // B: Driver+Spotter, Max 1 stint.
    // C: Spotter Only.
    //
    // Stint 1: A drives (forced by B unavailable).
    // Stint 2: B drives (A maxed).
    //
    // Spotter S2 Candidates:
    // - A (Finished driving S1). Available.
    // - C (Always available).
    //
    // Logic:
    // Without Role Coupling, A and C are equal cost (0). Solver picks arbitrary (often first or alphabetical).
    // With Role Coupling, A gets a huge reward (-100) for "Driver(S1) -> Spotter(S2)".
    // So Solver MUST pick A.

    json j;
    j["success"] = true;
    j["teamMembers"] = {
        {{"name", "A"}, {"isDriver", true}, {"isSpotter", true}, {"maxStints", 1}, {"minimumRestHours", 0}},
        {{"name", "B"}, {"isDriver", true}, {"isSpotter", true}, {"maxStints", 1}, {"minimumRestHours", 0}},
        {{"name", "C"}, {"isDriver", false}, {"isSpotter", true}, {"maxStints", 99}, {"minimumRestHours", 0}}
    };
    j["stints"] = {
        {{"id", 1}, {"startTime", "2026-01-17T00:00:00.000Z"}, {"endTime", "2026-01-17T01:00:00.000Z"}},
        {{"id", 2}, {"startTime", "2026-01-17T01:00:00.000Z"}, {"endTime", "2026-01-17T02:00:00.000Z"}}
    };
    
    // Availability: 
    // S1: B Unavailable (Forces A to drive).
    j["availability"] = {
        {"A", {{"2026-01-17T00:00:00.000Z", "Available"}, {"2026-01-17T01:00:00.000Z", "Available"}}},
        {"B", {{"2026-01-17T00:00:00.000Z", "Unavailable"}, {"2026-01-17T01:00:00.000Z", "Available"}}},
        {"C", {{"2026-01-17T00:00:00.000Z", "Available"}, {"2026-01-17T01:00:00.000Z", "Available"}}}
    };
    j["firstStintDriver"] = nullptr;
    
    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    // --- Integrated Mode ---
    {
        JresSolverOptions options;
        options.timeLimit = 5;
        options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
        options.allowNoSpotter = false;
        options.optimalityGap = 0.0;
        options.roleCouplingWeight = 100.0; 

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 2);

        // Verify Driving Schedule
        EXPECT_STREQ(output->schedule[0].driver, "A");
        EXPECT_STREQ(output->schedule[1].driver, "B");

        // Verify Spotting Schedule
        // S2 Spotter should be A
        EXPECT_STREQ(output->schedule[1].spotter, "A") << "Integrated: A should spot S2 after driving S1";

        free_jres_solver_output(output);
    }

    // --- Sequential Mode ---
    {
        JresSolverOptions options;
        options.timeLimit = 5;
        options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
        options.allowNoSpotter = false;
        options.optimalityGap = 0.0;
        options.roleCouplingWeight = 100.0; 

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 2);

        // Verify Driving Schedule
        EXPECT_STREQ(output->schedule[0].driver, "A");
        EXPECT_STREQ(output->schedule[1].driver, "B");

        // Verify Spotting Schedule
        EXPECT_STREQ(output->schedule[1].spotter, "A") << "Sequential: A should spot S2 after driving S1";

        free_jres_solver_output(output);
    }

    free_jres_solver_input(input);
}
