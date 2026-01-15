/**
 * @author popmonkey+jres@gmail.com
 * @file test/test_availability_overlap.cpp
 * @brief Tests for availability overlap detection.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

TEST(AvailabilityOverlapTest, StintOverlapsUnavailableSlot) {
    // Scenario:
    // Stint starts at 13:55 and ends at 14:55 (1 hour duration).
    //
    // Driver "Overlapper":
    // - 13:00: Preferred (So solver wants to pick them if looking at start time only)
    // - 14:00: Unavailable
    //
    // Driver "SafeDriver":
    // - 13:00: Available
    // - 14:00: Available
    
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 1;
    j["minimumRestHours"] = 0;
    
    j["teamMembers"] = {
        {{"name", "Overlapper"}, {"isDriver", true}, {"isSpotter", false}},
        {{"name", "SafeDriver"}, {"isDriver", true}, {"isSpotter", false}}
    };

    j["stints"] = {
        {
            {"id", 1},
            {"startTime", "2026-01-17T13:55:00.000Z"},
            {"endTime", "2026-01-17T14:55:00.000Z"}
        }
    };

    j["availability"] = {
        {"Overlapper", {
            {"2026-01-17T13:00:00.000Z", "Preferred"},
            {"2026-01-17T14:00:00.000Z", "Unavailable"}
        }},
        {"SafeDriver", {
            {"2026-01-17T13:00:00.000Z", "Available"},
            {"2026-01-17T14:00:00.000Z", "Available"}
        }}
    };
    
    j["firstStintDriver"] = nullptr;

    JresSolverInput* input = jres_input_from_json(j.dump().c_str());
    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 1);

    std::string assignedDriver = output->schedule[0].driver;
    EXPECT_EQ(assignedDriver, "SafeDriver") << "Solver assigned a driver who is unavailable during the latter part of the stint.";

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
