/**
 * @file test/test_optimization.cpp
 * @brief Tests for optimization incentives (consecutive stints, preferences, penalties).
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(OptimizationTest, EnforceConsecutiveStints) {
    // Scenario: 2 Team Members (can drive and spot), 4 Stints.
    // We want to see AABB or BBAA patterns for BOTH drivers and spotters.
    // Global requirement: consecutiveStints = 2.
    
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 2;
    j["minimumRestHours"] = 0;
    
    json members = json::array();
    std::vector<std::string> names = {"Member A", "Member B"};
    for (const auto& name : names) {
        members.push_back({
            {"name", name},
            {"isDriver", true},
            {"isSpotter", true}
        });
    }
    j["teamMembers"] = members;

    json stints = json::array();
    for (int i = 0; i < 4; ++i) {
        stints.push_back({
            {"id", i + 1},
            {"startTime", "2026-01-17T0" + std::to_string(i) + ":00:00.000Z"},
            {"endTime", "2026-01-17T0" + std::to_string(i+1) + ":00:00.000Z"}
        });
    }
    j["stints"] = stints;
    j["availability"] = json::object(); // All available
    j["firstStintDriver"] = nullptr;

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    // --- Sub-test: Integrated Mode ---
    {
        JresSolverOptions options = {};
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
        options.allowNoSpotter = false;
        options.optimalityGap = 0.0;

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 4);

        int driver_consecutive = 0;
        int spotter_consecutive = 0;
        for (int i = 0; i < output->schedule_len - 1; ++i) {
            if (std::string(output->schedule[i].driver) == std::string(output->schedule[i+1].driver)) {
                driver_consecutive++;
            }
            if (std::string(output->schedule[i].spotter) == std::string(output->schedule[i+1].spotter)) {
                spotter_consecutive++;
            }
        }

        // With 4 stints and consecutiveStints=2, we have 2 blocks.
        // Block 0: Stints 0,1. Block 1: Stints 2,3.
        // Stint 0 and 1 MUST be same driver. Stint 2 and 3 MUST be same driver.
        // So we guarantee at least 2 consecutive pairs (0-1 and 2-3).
        // If driver stays for both blocks (AAAA), we get 3 consecutive pairs.
        EXPECT_GE(driver_consecutive, 2) << "Integrated: Drivers should be consolidated (e.g. AABB).";
        EXPECT_GE(spotter_consecutive, 2) << "Integrated: Spotters should be consolidated (e.g. BBAA).";

        free_jres_solver_output(output);
    }

    // --- Sub-test: Sequential Mode ---
    {
        JresSolverOptions options = {};
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
        options.allowNoSpotter = false;
        options.optimalityGap = 0.0;

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 4);

        int driver_consecutive = 0;
        int spotter_consecutive = 0;
        for (int i = 0; i < output->schedule_len - 1; ++i) {
            if (std::string(output->schedule[i].driver) == std::string(output->schedule[i+1].driver)) {
                driver_consecutive++;
            }
            if (std::string(output->schedule[i].spotter) == std::string(output->schedule[i+1].spotter)) {
                spotter_consecutive++;
            }
        }

        EXPECT_GE(driver_consecutive, 2) << "Sequential: Drivers should be consolidated (e.g. AABB).";
        EXPECT_GE(spotter_consecutive, 2) << "Sequential: Spotters should be consolidated (e.g. BBAA).";

        free_jres_solver_output(output);
    }

    free_jres_solver_input(input);
}

TEST(OptimizationTest, PreferredOverAvailable) {
    // Scenario: 2 Drivers, 2 Stints. consecutiveStints=1.
    // Driver A: Stint 1 (Available), Stint 2 (Preferred)
    // Driver B: Stint 1 (Preferred), Stint 2 (Available)
    //
    // Naive/Round-Robin/Alphabetical Order might try: A then B.
    // - S1 (A, Avail) + S2 (B, Avail) -> Cost 0.
    //
    // Optimal Preference Order: B then A.
    // - S1 (B, Pref) + S2 (A, Pref) -> Cost -2.
    //
    // This forces the solver to pick B first, proving it's looking at the "Preferred" weight.

    json j;
    j["success"] = true;
    j["consecutiveStints"] = 1;
    j["minimumRestHours"] = 0;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}},
        {{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}}
    };
    j["stints"] = {
        {{"id", 1}, {"startTime", "2026-01-17T00:00:00.000Z"}, {"endTime", "2026-01-17T01:00:00.000Z"}},
        {{"id", 2}, {"startTime", "2026-01-17T01:00:00.000Z"}, {"endTime", "2026-01-17T02:00:00.000Z"}}
    };
    j["availability"] = {
        {"Driver A", {{"2026-01-17T00:00:00.000Z", "Available"}, {"2026-01-17T01:00:00.000Z", "Preferred"}}},
        {"Driver B", {{"2026-01-17T00:00:00.000Z", "Preferred"}, {"2026-01-17T01:00:00.000Z", "Available"}}}
    };
    j["firstStintDriver"] = nullptr;

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 2);

    // Expect B then A
    EXPECT_STREQ(output->schedule[0].driver, "Driver B") << "Stint 1 should be Driver B (Preferred)";
    EXPECT_STREQ(output->schedule[1].driver, "Driver A") << "Stint 2 should be Driver A (Preferred)";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

TEST(OptimizationTest, ConsecutiveOverPreferred) {
    // Scenario: 2 Drivers, 4 Stints.
    // Availability Pattern (Alternating Preference):
    // Stint 1: A=Pref, B=Avail
    // Stint 2: A=Avail, B=Pref
    // Stint 3: A=Pref, B=Avail
    // Stint 4: A=Avail, B=Pref
    //
    // Global Requirement: consecutiveStints = 2.
    // This forces blocks of 2 stints.
    // Block 0 (S1, S2):
    // - A: Pref + Avail = Cost -1.
    // - B: Avail + Pref = Cost -1.
    // Cost is equal.
    //
    // Block 1 (S3, S4):
    // - A: Pref + Avail = Cost -1.
    // - B: Avail + Pref = Cost -1.
    // Cost is equal.
    //
    // So AA BB or BB AA have same cost (-2).
    // Mixed AB AB is IMPOSSIBLE because it violates consecutiveStints=2 (A would drive S1 only).
    //
    // So the solver MUST output AABB or BBAA or AAAA or BBBB.
    // AAAA cost: (-1) + (-1) = -2.
    // So all valid solutions have same cost.
    // We just check that we get blocks.

    json j;
    j["success"] = true;
    j["consecutiveStints"] = 2;
    j["minimumRestHours"] = 0;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}},
        {{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}}
    };
    j["stints"] = {
        {{"id", 1}, {"startTime", "2026-01-17T00:00:00.000Z"}, {"endTime", "2026-01-17T01:00:00.000Z"}},
        {{"id", 2}, {"startTime", "2026-01-17T01:00:00.000Z"}, {"endTime", "2026-01-17T02:00:00.000Z"}},
        {{"id", 3}, {"startTime", "2026-01-17T02:00:00.000Z"}, {"endTime", "2026-01-17T03:00:00.000Z"}},
        {{"id", 4}, {"startTime", "2026-01-17T03:00:00.000Z"}, {"endTime", "2026-01-17T04:00:00.000Z"}}
    };
    j["availability"] = {
        {"Driver A", {
            {"2026-01-17T00:00:00.000Z", "Preferred"}, 
            {"2026-01-17T01:00:00.000Z", "Available"},
            {"2026-01-17T02:00:00.000Z", "Preferred"},
            {"2026-01-17T03:00:00.000Z", "Available"}
        }},
        {"Driver B", {
            {"2026-01-17T00:00:00.000Z", "Available"}, 
            {"2026-01-17T01:00:00.000Z", "Preferred"},
            {"2026-01-17T02:00:00.000Z", "Available"},
            {"2026-01-17T03:00:00.000Z", "Preferred"}
        }}
    };
    j["firstStintDriver"] = nullptr;

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 4);

    // Check for consecutive blocks
    std::string d1 = output->schedule[0].driver;
    std::string d2 = output->schedule[1].driver;
    std::string d3 = output->schedule[2].driver;
    std::string d4 = output->schedule[3].driver;

    // We expect pairs like AA BB or BB AA
    EXPECT_EQ(d1, d2) << "Stints 1 and 2 should be consecutive";
    EXPECT_EQ(d3, d4) << "Stints 3 and 4 should be consecutive";
    // We don't necessarily expect a switch between blocks if cost is identical, 
    // but typically the solver will find one of the optimal solutions.
    // The previous test asserted EXPECT_NE(d2, d3). 
    // If we have AAAA, it fails.
    // But AAAA has same cost.
    // However, usually we want to see distribution.
    // Let's relax the check to just verifying blocks are consistent.
    
    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
