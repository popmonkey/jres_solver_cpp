#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

// Helper to format ISO8601 time
std::string format_time(int hour) {
    std::ostringstream oss;
    int day = 17 + (hour / 24);
    oss << "2026-01-" << std::setfill('0') << std::setw(2) << day << "T" 
        << std::setfill('0') << std::setw(2) << (hour % 24) << ":00:00";
    return oss.str();
}

TEST(LongRaceTest, Solves48HourRace) {
    // Scenario: 4 Team Members, 48 Stints (1 hour each).
    // This tests if the solver can handle a larger problem size (longer duration).
    
    json j;
    j["success"] = true;
    j["consecutiveStints"] = 2; // Encourage double stints
    j["minimumRestHours"] = 6; // Mandatory rest
    
    json members = json::array();
    std::vector<std::string> names = {"Driver A", "Driver B", "Driver C", "Driver D"};
    for (const auto& name : names) {
        members.push_back({
            {"name", name},
            {"isDriver", true},
            {"isSpotter", true}
        });
    }
    j["teamMembers"] = members;

    json stints = json::array();
    int num_stints = 48;
    for (int i = 0; i < num_stints; ++i) {
        stints.push_back({
            {"id", i + 1},
            {"startTime", format_time(i)},
            {"endTime", format_time(i + 1)}
        });
    }
    j["stints"] = stints;
    
    // Everyone available all the time
    j["availability"] = json::object();
    for (const auto& name : names) {
        json member_avail = json::object();
        for (int i = 0; i < num_stints; ++i) {
            member_avail[format_time(i)] = "Available";
        }
        // Also add the end time of the last stint as an availability point? 
        // The solver typically looks at stint start times or specific intervals.
        // Based on other tests, it seems to be keyed by time.
        // Let's just ensure we cover the stint start times.
        j["availability"][name] = member_avail;
    }
    
    j["firstStintDriver"] = nullptr;

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOptions options = {};
    options.timeLimit = 30; // Give it a bit more time for a larger problem
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.05; // Allow 5% gap to speed it up

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    // Check if we got a solution
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->schedule_len, num_stints);
    EXPECT_EQ(output->diagnosis_len, 0);
    
    // Basic validation: ensure no one drives > 4 hours in 6 hours (implicit check by solver, but good to know it solved)
    
    free_jres_solver_output(output);
    free_jres_solver_input(input);
}

TEST(LongRaceTest, DaySpecificAvailability) {
    // Scenario: Verify that availability at 10:00 Day 1 is distinct from 10:00 Day 2.
    // Stint 10 starts at 10:00 on Day 1.
    // Stint 34 starts at 10:00 on Day 2 (10 + 24 = 34).
    
    // We will restrict availability such that:
    // - Stint 10 MUST be driven by Driver A (Driver B is unavailable).
    // - Stint 34 MUST be driven by Driver B (Driver A is unavailable).
    // If the solver conflates days, it might think Driver A is unavailable at Stint 10 (if Day 2 overwrites Day 1)
    // or Driver A is available at Stint 34 (if Day 1 overwrites Day 2).

    json j;
    j["success"] = true;
    j["consecutiveStints"] = 1; 
    j["minimumRestHours"] = 0;
    
    json members = json::array();
    members.push_back({{"name", "Driver A"}, {"isDriver", true}, {"isSpotter", false}});
    members.push_back({{"name", "Driver B"}, {"isDriver", true}, {"isSpotter", false}});
    members.push_back({{"name", "Driver C"}, {"isDriver", true}, {"isSpotter", false}});
    j["teamMembers"] = members;

    json stints = json::array();
    int num_stints = 48;
    for (int i = 0; i < num_stints; ++i) {
        stints.push_back({
            {"id", i + 1},
            {"startTime", format_time(i)},
            {"endTime", format_time(i + 1)}
        });
    }
    j["stints"] = stints;
    
    j["availability"] = json::object();
    
    // Default: Everyone available everywhere
    json avail_A = json::object();
    json avail_B = json::object();
    json avail_C = json::object();
    for (int i = 0; i < num_stints; ++i) {
        avail_A[format_time(i)] = "Available";
        avail_B[format_time(i)] = "Available";
        avail_C[format_time(i)] = "Available";
    }

    // Constraint for Stint 10 (Hour 10)
    // Driver B and C are unavailable, so A must drive
    avail_B[format_time(10)] = "Unavailable";
    avail_C[format_time(10)] = "Unavailable";

    // Constraint for Stint 34 (Hour 34 = 10 + 24)
    // Driver A and C are unavailable, so B must drive
    avail_A[format_time(34)] = "Unavailable";
    avail_C[format_time(34)] = "Unavailable";

    j["availability"]["Driver A"] = avail_A;
    j["availability"]["Driver B"] = avail_B;
    j["availability"]["Driver C"] = avail_C;
    
    j["firstStintDriver"] = nullptr;

    std::string json_str = j.dump();
    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOptions options = {};
    options.timeLimit = 30;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true; // No spotters needed for this test
    options.optimalityGap = 0.0;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    ASSERT_NE(output, nullptr);
    if (output->diagnosis_len > 0) {
        for(int i=0; i<output->diagnosis_len; ++i) {
            std::cout << "Diagnosis: " << output->diagnosis[i] << std::endl;
        }
    }
    EXPECT_EQ(output->diagnosis_len, 0);
    EXPECT_EQ(output->schedule_len, num_stints);

    // Verify Stint 10
    // schedule[10] corresponds to stint with id 11 (index 10)
    // startTime is format_time(10)
    EXPECT_STREQ(output->schedule[10].driver, "Driver A") 
        << "Stint 10 (Day 1 10:00) should be Driver A. Driver B was unavailable.";

    // Verify Stint 34
    // schedule[34] corresponds to stint with id 35 (index 34)
    // startTime is format_time(34)
    EXPECT_STREQ(output->schedule[34].driver, "Driver B") 
        << "Stint 34 (Day 2 10:00) should be Driver B. Driver A was unavailable.";

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}
