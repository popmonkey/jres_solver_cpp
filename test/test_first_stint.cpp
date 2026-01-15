/**
 * @author popmonkey+jres@gmail.com
 * @file test/test_first_stint.cpp
 * @brief Tests for the firstStintDriver hard constraint.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(FirstStintTest, EnforceFirstStintDriver) {
    // Scenario: 2 Drivers, 2 Stints.
    // Driver A: Preferred for Stint 1
    // Driver B: Preferred for Stint 2, and set as firstStintDriver.
    //
    // Normally, Driver A would take Stint 1 due to preference.
    // But since Driver B is forced as firstStintDriver, Driver B MUST take Stint 1.

    json j;
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
        {"Driver A", {{"2026-01-17T00:00:00.000Z", "Preferred"}, {"2026-01-17T01:00:00.000Z", "Available"}}},
        {"Driver B", {{"2026-01-17T00:00:00.000Z", "Available"}, {"2026-01-17T01:00:00.000Z", "Preferred"}}}
    };
    j["firstStintDriver"] = "Driver B";

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    ASSERT_NE(input, nullptr);
    EXPECT_STREQ(input->firstStintDriver, "Driver B");

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 2);

    // Expect Driver B then Driver A (or B if cost doesn't matter, but B MUST be first)
    EXPECT_STREQ(output->schedule[0].driver, "Driver B") << "Stint 1 should be Driver B (Forced)";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

TEST(FirstStintTest, InvalidFirstStintDriverThrows) {
    json j;
    j["consecutiveStints"] = 1;
    j["minimumRestHours"] = 0;
    j["teamMembers"] = {
        {{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}}
    };
    j["stints"] = {
        {{"id", 1}, {"startTime", "2026-01-17T00:00:00.000Z"}, {"endTime", "2026-01-17T01:00:00.000Z"}}
    };
    j["availability"] = {
        {"Driver A", {{"2026-01-17T00:00:00.000Z", "Available"}}}
    };
    j["firstStintDriver"] = "NonExistent";

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    EXPECT_GT(output->diagnosis_len, 0);
    EXPECT_TRUE(std::string(output->diagnosis[0]).find("is not a valid driver") != std::string::npos);

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}
