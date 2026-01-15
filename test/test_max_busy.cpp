/**
 * @author popmonkey+jres@gmail.com
 * @file test/test_max_busy.cpp
 * @brief Tests for max busy time logic.
 */
#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "jres_internal_types.hpp"
#include <string>
#include <vector>

TEST(MaxBusyTest, MaxBusyHoursInfeasible) {
    // 3 stints of 1 hour. Total 3h.
    // MaxBusy = 2h.
    // Driver cannot do S1, S2, S3 continuously.
    
    std::string json_str = R"({
      "maximumBusyHours": 2,
      "teamMembers": [
        { "name": "D1", "isDriver": true }
      ],
      "availability": {
        "D1": {}
      },
      "stints": [
        { "id": 1, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" }
      ]
    })";

    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    // With only 1 driver, and they can't do all 3 stints, this should be infeasible.
    bool feasible = (output->schedule_len == 3);
    if (output->diagnosis_len > 0) {
        std::string msg = output->diagnosis[0];
        if (msg.find("infeasible") != std::string::npos) feasible = false;
    }
    
    // It might output partial schedule or empty schedule
    if (output->schedule_len < 3) feasible = false;

    ASSERT_FALSE(feasible);

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}

TEST(MaxBusyTest, MaxBusyHoursFeasible) {
    // 3 stints of 1 hour.
    // MaxBusy = 2h.
    // 2 Drivers.
    // D1: S1, S2. D2: S3. Valid.
    
    std::string json_str = R"({
      "maximumBusyHours": 2,
      "teamMembers": [
        { "name": "D1", "isDriver": true },
        { "name": "D2", "isDriver": true }
      ],
      "availability": {
        "D1": {}, "D2": {}
      },
      "stints": [
        { "id": 1, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" }
      ]
    })";

    JresSolverOptions options = {};
    options.spotterMode = JRES_SPOTTER_MODE_NONE;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->schedule_len, 3);
    
    // Verify no one worked > 2h
    // Simple check: S1+S2+S3 same driver?
    std::string d1 = output->schedule[0].driver;
    std::string d2 = output->schedule[1].driver;
    std::string d3 = output->schedule[2].driver;
    
    ASSERT_FALSE(d1 == d2 && d2 == d3); // If all equal, they worked 3h

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}

TEST(MaxBusyTest, MaxBusySpotterIntegrated) {
    // 3 stints of 1 hour. Total 3h.
    // MaxBusy = 2h.
    // 1 Driver (can drive all, because we only care about spotter here)
    // 1 Spotter (can spot)
    // We need spotters for all stints.
    // Spotter cannot do S1, S2, S3 continuously.
    
     std::string json_str = R"({
      "maximumBusyHours": 2,
      "teamMembers": [
        { "name": "D1", "isDriver": true, "isSpotter": false },
        { "name": "D2", "isDriver": true, "isSpotter": false },
        { "name": "S1", "isDriver": false, "isSpotter": true }
      ],
      "availability": {
        "D1": {}, "D2": {}, "S1": {}
      },
      "stints": [
        { "id": 1, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" }
      ]
    })";

    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    bool feasible = (output->schedule_len == 3);
    if (output->diagnosis_len > 0) {
        std::string msg = output->diagnosis[0];
        if (msg.find("infeasible") != std::string::npos) feasible = false;
    }
    
    ASSERT_FALSE(feasible);

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}

TEST(MaxBusyTest, MaxBusySequential) {
    // 8 stints of 1 hour. MaxBusy = 6.
    // Kyle (D/S).
    // S2 (S).
    // Sequential mode.
    
    // Kyle drives 0-3 (4h) [Preferred].
    // S2 spots 0-3 (4h) [Preferred].
    // Kyle spots 4-7 (4h) [Only option].
    // S2 unavailable 4-7.
    
    // Combined Kyle = 8h. > 6h.
    
    std::string json_str = R"({
      "maximumBusyHours": 6,
      "teamMembers": [
        { "name": "Kyle", "isDriver": true, "isSpotter": true },
        { "name": "D2", "isDriver": true, "isSpotter": false },
        { "name": "S2", "isDriver": false, "isSpotter": true }
      ],
      "availability": {
        "Kyle": { 
            "2026-01-01T00:00:00.000Z":"Preferred", "2026-01-01T01:00:00.000Z":"Preferred", 
            "2026-01-01T02:00:00.000Z":"Preferred", "2026-01-01T03:00:00.000Z":"Preferred" 
        },
        "D2": {},
        "S2": {
            "2026-01-01T00:00:00.000Z":"Preferred", "2026-01-01T01:00:00.000Z":"Preferred", 
            "2026-01-01T02:00:00.000Z":"Preferred", "2026-01-01T03:00:00.000Z":"Preferred",
            "2026-01-01T04:00:00.000Z":"Unavailable", "2026-01-01T05:00:00.000Z":"Unavailable", 
            "2026-01-01T06:00:00.000Z":"Unavailable", "2026-01-01T07:00:00.000Z":"Unavailable"
        }
      },
      "stints": [
        { "id": 0, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 1, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T03:00:00Z", "endTime": "2026-01-01T04:00:00Z" },
        { "id": 4, "startTime": "2026-01-01T04:00:00Z", "endTime": "2026-01-01T05:00:00Z" },
        { "id": 5, "startTime": "2026-01-01T05:00:00Z", "endTime": "2026-01-01T06:00:00Z" },
        { "id": 6, "startTime": "2026-01-01T06:00:00Z", "endTime": "2026-01-01T07:00:00Z" },
        { "id": 7, "startTime": "2026-01-01T07:00:00Z", "endTime": "2026-01-01T08:00:00Z" }
      ]
    })";
    
    // We prefer Kyle for first 4 stints to encourage him driving them.
    // D2 can drive rest.
    // Kyle is ONLY spotter.
    // If Kyle drives 0-3 (4h).
    // He must spot 4-7 (4h)?
    // Or spot 0-3? (Cannot spot if driving).
    // Spot 4-7.
    // Total 8h. > 6h.
    
    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
    options.allowNoSpotter = false;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    bool feasible = (output->schedule_len == 8);
    // If output->diagnosis says infeasible spotter, then feasible=false.
    if (output->diagnosis_len > 0) {
         // Check if spotter assignment failed
         std::string msg = output->diagnosis[0];
         if (msg.find("Spotter assignment infeasible") != std::string::npos) feasible = false;
    }
    
    // If logic is MISSING, it will return a schedule where Kyle does 8h.
    if (feasible) {
        // Verify Kyle's workload
        int kyleCount = 0;
        for(int i=0; i<8; ++i) {
            if (std::string(output->schedule[i].driver) == "Kyle") kyleCount++;
            if (std::string(output->schedule[i].spotter) == "Kyle") kyleCount++;
        }
        if (kyleCount > 6) {
            // This confirms the bug
            std::cout << "Kyle worked " << kyleCount << " hours (Expected failure > 6)" << std::endl;
             // We EXPECT this to fail if bug is present.
             // Assert FALSE to signal "Yes, we reproduced the bug / we want to ensure it is fixed".
             // Since we are writing the test to ensure it IS fixed:
             FAIL() << "Sequential solver allowed maxBusy violation.";
        }
    }

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}