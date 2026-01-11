#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "jres_internal_types.hpp"
#include <string>
#include <vector>
#include <iostream>

TEST(MaxBusyDefaults, DefaultValueCheck) {
    // 10 Stints of 1 hour. Total 10 hours.
    // Default MaxBusy = 8 hours.
    // Kyle (D/S).
    // Kyle should NOT be able to work all 10 hours.
    
    // We omit maxBusyHours from JSON.
    std::string json_str = R"({
      "teamMembers": [
        { "name": "Kyle", "isDriver": true, "isSpotter": true }
      ],
      "availability": {
        "Kyle": {}
      },
      "stints": [
        { "id": 1, "startTime": "2026-01-01T00:00:00Z", "endTime": "2026-01-01T01:00:00Z" },
        { "id": 2, "startTime": "2026-01-01T01:00:00Z", "endTime": "2026-01-01T02:00:00Z" },
        { "id": 3, "startTime": "2026-01-01T02:00:00Z", "endTime": "2026-01-01T03:00:00Z" },
        { "id": 4, "startTime": "2026-01-01T03:00:00Z", "endTime": "2026-01-01T04:00:00Z" },
        { "id": 5, "startTime": "2026-01-01T04:00:00Z", "endTime": "2026-01-01T05:00:00Z" },
        { "id": 6, "startTime": "2026-01-01T05:00:00Z", "endTime": "2026-01-01T06:00:00Z" },
        { "id": 7, "startTime": "2026-01-01T06:00:00Z", "endTime": "2026-01-01T07:00:00Z" },
        { "id": 8, "startTime": "2026-01-01T07:00:00Z", "endTime": "2026-01-01T08:00:00Z" },
        { "id": 9, "startTime": "2026-01-01T08:00:00Z", "endTime": "2026-01-01T09:00:00Z" },
        { "id": 10, "startTime": "2026-01-01T09:00:00Z", "endTime": "2026-01-01T10:00:00Z" }
      ]
    })";

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    // Verify manually that input->maxBusyHours is 8 (if we can inspect it? It's in the opaque struct)
    // Actually we can inspect it because we have the struct definition in header.
    // But JresSolverInput definition is in jres_solver.hpp which is included.
    EXPECT_EQ(input->maximumBusyHours, 8);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    
    // Should be infeasible (Kyle cannot do 10h > 8h).
    bool feasible = (output->schedule_len == 10);
    if (output->diagnosis_len > 0) {
        std::string msg = output->diagnosis[0];
        if (msg.find("infeasible") != std::string::npos) feasible = false;
    }
    
    if (feasible) {
         FAIL() << "Solver found schedule despite default maximumBusyHours=8 violation.";
    }

    free_jres_solver_output(output);
    free_jres_solver_input(input);
}
