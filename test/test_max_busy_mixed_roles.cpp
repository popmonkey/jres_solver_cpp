/**
 * @author popmonkey+jres@gmail.com
 * @file test/test_max_busy_mixed_roles.cpp
 * @brief Tests for max busy time with mixed roles.
 */
#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "jres_internal_types.hpp"
#include <string>
#include <vector>

TEST(MaxBusyMixedRoles, AlternatingWorkload) {
    // 12 Stints of 1 hour. Total 12 hours.
    // MaxBusy = 8 hours.
    // Kyle (Driver/Spotter).
    // We want to force him to work all stints to see if constraint catches it.
    // To force him, we only provide him.
    // If constraint works -> Infeasible.
    // If constraint fails -> Schedule produced.
    
    // We set consecutiveStints = 2 to match user pattern roughly.
    // Stint 0,1: Block 0.
    // Stint 2,3: Block 1.
    
    std::string json_str = R"({
      "consecutiveStints": 2,
      "maximumBusyHours": 8,
      "teamMembers": [
        { "name": "Kyle", "isDriver": true, "isSpotter": true }
      ],
      "availability": {
        "Kyle": {}
      },
      "stints": [
        { "id": 0, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 1, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T03:00:00Z", "endTime": "2026-01-01T04:00:00Z" },
        { "id": 4, "startTime": "2026-01-01T04:00:00Z", "endTime": "2026-01-01T05:00:00Z" },
        { "id": 5, "startTime": "2026-01-01T05:00:00Z", "endTime": "2026-01-01T06:00:00Z" },
        { "id": 6, "startTime": "2026-01-01T06:00:00Z", "endTime": "2026-01-01T07:00:00Z" },
        { "id": 7, "startTime": "2026-01-01T07:00:00Z", "endTime": "2026-01-01T08:00:00Z" },
        { "id": 8, "startTime": "2026-01-01T08:00:00Z", "endTime": "2026-01-01T09:00:00Z" },
        { "id": 9, "startTime": "2026-01-01T09:00:00Z", "endTime": "2026-01-01T10:00:00Z" },
        { "id": 10, "startTime": "2026-01-01T10:00:00Z", "endTime": "2026-01-01T11:00:00Z" },
        { "id": 11, "startTime": "2026-01-01T11:00:00Z", "endTime": "2026-01-01T12:00:00Z" }
      ]
    })";

    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    // Check feasibility
    bool feasible = (output->schedule_len == 12);
    if (output->diagnosis_len > 0) {
        std::string msg = output->diagnosis[0];
        if (msg.find("infeasible") != std::string::npos) feasible = false;
    }
    
    // We EXPECT it to be infeasible because Kyle cannot work 12h straight with maxBusy=8.
    // If feasible is true, the bug is reproduced.
    if (feasible) {
        // Double check he is actually assigned everything
        // (He has to be, he is the only one)
        std::cout << "Schedule produced (UNEXPECTED):" << std::endl;
        for(int i=0; i<12; ++i) {
            std::cout << i << ": " << output->schedule[i].driver << " / " << output->schedule[i].spotter << std::endl;
        }
        FAIL() << "Solver produced a schedule violating maximumBusyHours with alternating roles.";
    }

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}
