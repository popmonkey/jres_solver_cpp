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
