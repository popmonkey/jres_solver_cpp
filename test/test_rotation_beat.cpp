/**
 * @file test/test_rotation_beat.cpp
 * @brief Tests for rotation beat (rhythm) enforcement.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(RotationBeatTest, EnforcesPattern) {
    // Scenario: 3 Drivers (A, B, C). 6 Stints.
    // N = 3.
    // Stint 0, 1, 2: A, B, C (forced by Preference or availability)
    // Stint 3, 4, 5: Must follow A, B, C pattern if Rotation Beat is high.
    
    // Setup:
    // Stint 0: A (Preferred), B (Available), C (Available)
    // Stint 1: B (Preferred), A (Available), C (Available)
    // Stint 2: C (Preferred), A (Available), B (Available)
    // This establishes A, B, C for 0-2.
    
    // Stint 3: A (Available) -> Matches Stint 0 (A).
    // Stint 4: A (Preferred!), B (Available).
    //    - Without Beat: Solver picks A (Preferred).
    //    - With Beat: Solver picks B (Available) to match Stint 1 (B).
    // Stint 5: C (Available). Matches Stint 2 (C).

    json j;
    j["success"] = true;
    
    std::vector<std::string> names = {"Driver A", "Driver B", "Driver C"};
    json members = json::array();
    for (const auto& name : names) {
        members.push_back({
            {"name", name},
            {"isDriver", true},
            {"isSpotter", false},
            {"maxStints", 1}, // Disable consecutive bonus to isolate pattern logic
            {"minimumRestHours", 0}
        });
    }
    j["teamMembers"] = members;

    json stints = json::array();
    for (int i = 0; i < 6; ++i) {
        stints.push_back({
            {"id", i + 1},
            {"startTime", "2026-01-17T0" + std::to_string(i) + ":00:00.000Z"},
            {"endTime", "2026-01-17T0" + std::to_string(i+1) + ":00:00.000Z"}
        });
    }
    j["stints"] = stints;

    json avail = json::object();
    // Default everyone to Available
    for (const auto& name : names) {
        json times = json::object();
        for (int i = 0; i < 6; ++i) {
            std::string t = "2026-01-17T0" + std::to_string(i) + ":00:00.000Z";
            times[t] = "Available";
        }
        avail[name] = times;
    }
    j["availability"] = avail;

    // Apply Specific Preferences
    // Stint 0 (00:00): A Preferred
    j["availability"]["Driver A"]["2026-01-17T00:00:00.000Z"] = "Preferred";
    // Stint 1 (01:00): B Preferred
    j["availability"]["Driver B"]["2026-01-17T01:00:00.000Z"] = "Preferred";
    // Stint 2 (02:00): C Preferred
    j["availability"]["Driver C"]["2026-01-17T02:00:00.000Z"] = "Preferred";

    // Stint 4 (04:00): A Preferred (Trap!)
    j["availability"]["Driver A"]["2026-01-17T04:00:00.000Z"] = "Preferred";

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    // --- Case: Without Rotation Beat ---
    {
        JresSolverOptions options = {};
        options.timeLimit = 5;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        options.allowNoSpotter = true;
        options.optimalityGap = 0.0;
        options.rotationBeatWeight = 0.0;

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 6);

        // Expect A, B, C for first 3 (due to preferences)
        EXPECT_STREQ(output->schedule[0].driver, "Driver A");
        EXPECT_STREQ(output->schedule[1].driver, "Driver B");
        EXPECT_STREQ(output->schedule[2].driver, "Driver C");

        // Stint 4: A should be picked because A prefers it (-1 cost vs 0)
        EXPECT_STREQ(output->schedule[4].driver, "Driver A") << "Without beat, A should drive Stint 4 (Preferred)";

        free_jres_solver_output(output);
    }

    // --- Case: With Rotation Beat ---
    {
        JresSolverOptions options = {};
        options.timeLimit = 5;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        options.allowNoSpotter = true;
        options.optimalityGap = 0.0;
        options.rotationBeatWeight = 10.0; // Strong penalty

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 6);

        // Expect A, B, C for first 3
        EXPECT_STREQ(output->schedule[0].driver, "Driver A");
        EXPECT_STREQ(output->schedule[1].driver, "Driver B");
        EXPECT_STREQ(output->schedule[2].driver, "Driver C");

        // Stint 3: Should match Stint 0 (A)
        EXPECT_STREQ(output->schedule[3].driver, "Driver A");
        
        // Stint 4: Should match Stint 1 (B), overcoming A's preference
        EXPECT_STREQ(output->schedule[4].driver, "Driver B") << "With beat, B should drive Stint 4 to match Stint 1";
        
        // Stint 5: Should match Stint 2 (C)
        EXPECT_STREQ(output->schedule[5].driver, "Driver C");

        free_jres_solver_output(output);
    }

    free_jres_solver_input(input);
}
