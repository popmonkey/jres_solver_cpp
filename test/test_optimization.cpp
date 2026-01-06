#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(OptimizationTest, IncentivizeConsecutiveStints) {
    // Scenario: 2 Team Members (can drive and spot), 4 Stints.
    // Each member can do max 2 stints.
    // We want to see AABB or BBAA patterns for BOTH drivers and spotters.
    
    json j;
    j["success"] = true;
    
    json members = json::array();
    std::vector<std::string> names = {"Member A", "Member B"};
    for (const auto& name : names) {
        members.push_back({
            {"name", name},
            {"isDriver", true},
            {"isSpotter", true},
            {"maxStints", 2},
            {"minimumRestHours", 0}
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

    // --- Sub-test 1: Integrated Mode ---
    {
        JresSolverOptions options;
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

        // With 4 stints and maxStints=2, perfect consolidation is 2 switches (one middle break for each role, or AABB/BBAA)
        // Consecutive pairs in AABB is 2: (A,A) and (B,B).
        // Minimal acceptable is AABB or BBAA => 2 consecutive pairs.
        EXPECT_GE(driver_consecutive, 2) << "Integrated: Drivers should be consolidated (e.g. AABB).";
        EXPECT_GE(spotter_consecutive, 2) << "Integrated: Spotters should be consolidated (e.g. BBAA).";

        free_jres_solver_output(output);
    }

    // --- Sub-test 2: Sequential Mode ---
    {
        JresSolverOptions options;
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
    // Scenario: 2 Drivers, 2 Stints. maxStints=1 (Disable consecutive bonus).
    // Driver A: Stint 1 (Available), Stint 2 (Preferred)
    // Driver B: Stint 1 (Preferred), Stint 2 (Available)
    //
    // Naive/Round-Robin/Alphabetical Order might try: A then B.
    // - S1 (A, Avail) + S2 (B, Avail) -> Cost 0.
    //
    // Optimal Preference Order: B then A.
    // - S1 (B, Pref) + S2 (A, Pref) -> Cost -2.
    //
    // This forces the solver to pick B first, proving it's looking at the "Preferred" weight
    // and not just assigning in list order.

    json j;
    j["success"] = true;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 1}, {"minimumRestHours", 0}},
        {{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 1}, {"minimumRestHours", 0}}
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

    JresSolverOptions options;
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
    // Option 1 (Alternating/Split): A, B, A, B
    // - Everyone gets their Preferred slots.
    // - Total Preferred = 4. Cost = -4.0.
    // - Consecutive Pairs = 0. Cost = 0.0.
    // - Balance: Perfect (2 each). Cost = 0.
    // - Total Cost = -4.0.
    //
    // Option 2 (Consecutive Blocks): A, A, B, B
    // - A takes S1(Pref), S2(Avail). B takes S3(Avail), S4(Pref).
    // - Total Preferred = 2. Cost = -2.0.
    // - Consecutive Pairs = 2 (A-A, B-B). Cost = 2 * -1.5 = -3.0.
    // - Balance: Perfect (2 each). Cost = 0.
    // - Total Cost = -5.0.
    //
    // Since -5.0 < -4.0, the solver MUST choose Option 2 (Consecutive Blocks).
    // This proves that the Consecutive Bonus (-1.5) outweighs the loss of a Preferred slot (1.0 difference).

    json j;
    j["success"] = true;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 2}, {"minimumRestHours", 0}},
        {{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 2}, {"minimumRestHours", 0}}
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

    JresSolverOptions options;
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
    EXPECT_NE(d2, d3) << "Drivers should switch between blocks";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
