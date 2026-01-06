#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(OptimizationTest, IncentivizeConsecutiveStints) {
    // Scenario: 4 Drivers, 8 Stints.
    // Each driver can do max 2 stints.
    // Optimal with incentive (Max Consecutive): Driver A (1,2), Driver B (3,4), Driver C (5,6), Driver D (7,8)
    // This results in 4 consecutive pairs: (1-2), (3-4), (5-6), (7-8).
    // Suboptimal (Alternating): A, B, A, B... results in 0 consecutive pairs.
    
    json j;
    j["success"] = true;
    
    json drivers = json::array();
    std::vector<std::string> names = {"Driver A", "Driver B", "Driver C", "Driver D"};
    for (const auto& name : names) {
        drivers.push_back({
            {"name", name},
            {"isDriver", true},
            {"isSpotter", false},
            {"maxStints", 2},
            {"minimumRestHours", 0}
        });
    }
    j["teamMembers"] = drivers;

    json stints = json::array();
    for (int i = 0; i < 8; ++i) {
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

    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE; // Focus on driver optimization
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 8);

    int consecutive_count = 0;
    for (int i = 0; i < output->schedule_len - 1; ++i) {
        std::string current = output->schedule[i].driver;
        std::string next = output->schedule[i+1].driver;
        if (current == next) {
            consecutive_count++;
        }
    }

    // We expect the solver to maximize consecutive stints.
    // In a perfect 2-stint blocks schedule: A A B B C C D D
    // Pairs: (A,A), (A,B), (B,B), (B,C), (C,C), (C,D), (D,D)
    // Consecutive: 4.
    // We accept at least 3 to allow for some minor variation, but definitely > 0.
    EXPECT_GE(consecutive_count, 3) << "Solver should prioritize consecutive stints. Found " << consecutive_count << " consecutive pairs.";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
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
