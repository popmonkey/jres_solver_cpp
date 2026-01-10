/**
 * @file test/test_hard_consecutive_limit.cpp
 * @brief Tests for hard constraints on consecutive stints.
 */

#include <gtest/gtest.h>
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>

using json = nlohmann::json;

/**
 * @brief Verify that the solver strictly enforces the 'consecutiveStints' limit.
 * 
 * This test constructs a scenario where we search for a violation of the consecutive stint limit.
 * It ensures that no driver is assigned more than 'consecutiveStints' stints in a row.
 */
TEST(ConstraintTest, EnforceConsecutiveStintsHardLimit) {
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 2;
    j["minimumRestHours"] = 0;
    
    // 2 Drivers
    json members = json::array();
    members.push_back({{"name", "DriverA"}, {"isDriver", true}, {"isSpotter", false}});
    members.push_back({{"name", "DriverB"}, {"isDriver", true}, {"isSpotter", false}});
    j["teamMembers"] = members;

    // 8 Stints (4 blocks of 2)
    json stints = json::array();
    for (int i = 0; i < 8; ++i) {
        stints.push_back({
            {"id", i + 1},
            {"startTime", "2026-01-17T0" + std::to_string(12 + i) + ":00:00.000Z"},
            {"endTime", "2026-01-17T0" + std::to_string(12 + i + 1) + ":00:00.000Z"}
        });
    }
    j["stints"] = stints;
    j["availability"] = json::object(); 

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE; // Focus on drivers
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0; 

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 8);

    std::vector<std::string> assigned(8);
    for (int i = 0; i < output->schedule_len; ++i) {
        // Output schedule usually sorted but trust index if id matches
        int id = output->schedule[i].id;
        if (id >= 1 && id <= 8) {
            assigned[id - 1] = output->schedule[i].driver;
        }
    }

    // Check for 4 consecutive stints (which would be a violation as limit is 2)
    // We scan for any sequence of 4 identical drivers.
    for(int i=0; i <= 4; ++i) {
        bool all_same = true;
        std::string first = assigned[i];
        for(int k=1; k<4; ++k) {
            if (assigned[i+k] != first) {
                all_same = false;
                break;
            }
        }
        if (all_same) {
            FAIL() << "Found 4 consecutive stints for " << first << " starting at index " << i 
                   << ". Limit is " << j["consecutiveStints"];
        }
    }

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}
