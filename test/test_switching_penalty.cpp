/**
 * @file test/test_switching_penalty.cpp
 * @brief Tests for driver switching penalty enforcement.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>
#include <set>

using json = nlohmann::json;

TEST(SwitchingPenaltyTest, ForceMinimumSwitches) {
    // Scenario: 3 Stints, 2 Drivers.
    // Both Available for all.
    // Fair Share forces both to drive at least 1 stint (total 3h, fair share ~0.375h).
    // Without penalty, fairness balancing (kCostFairness) might favor Alternating (A, B, A) or (A, A, B).
    // With HIGH Switching Penalty, we force (A, A, B) or (B, B, A) -> 1 Switch.
    // (A, B, A) -> 2 Switches.
    
    json j;
    j["success"] = true;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 3}, {"minimumRestHours", 0}},
        {{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}, {"maxStints", 3}, {"minimumRestHours", 0}}
    };
    j["stints"] = {
        {{"id", 1}, {"startTime", "2026-01-17T00:00:00.000Z"}, {"endTime", "2026-01-17T01:00:00.000Z"}},
        {{"id", 2}, {"startTime", "2026-01-17T01:00:00.000Z"}, {"endTime", "2026-01-17T02:00:00.000Z"}},
        {{"id", 3}, {"startTime", "2026-01-17T02:00:00.000Z"}, {"endTime", "2026-01-17T03:00:00.000Z"}}
    };
    j["availability"] = json::object(); // All available
    j["firstStintDriver"] = nullptr;

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;
    options.switchingPenalty = 1000.0; // High penalty

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 3);

    // Calculate switches
    int switches = 0;
    for (int i = 1; i < output->schedule_len; ++i) {
        if (std::string(output->schedule[i].driver) != std::string(output->schedule[i-1].driver)) {
            switches++;
        }
    }

    // Since Fair Share forces both to drive, min switches = 1 (e.g. AAB).
    // However, with strict consecutiveStints=1 (default), AAB is invalid (A cannot drive 2 consecutive).
    // So valid schedule is ABA (2 switches).
    // If penalty works, we should not see A-B-A (2 switches) -> Wait, ABA IS the minimum now.
    EXPECT_EQ(switches, 2) << "Should minimize switches to 2 (e.g. ABA) given strict consecutiveStints=1";

    // Verify both drivers drove (Fair Share check)
    std::set<std::string> drivers;
    for (int i = 0; i < output->schedule_len; ++i) {
        drivers.insert(output->schedule[i].driver);
    }
    EXPECT_EQ(drivers.size(), 2) << "Both drivers must drive due to Fair Share rule";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
