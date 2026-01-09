/**
 * @file test/test_spotter_balancing.cpp
 * @brief Tests for spotter workload balancing.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include <vector>
#include <string>
#include <map>

TEST(SpotterBalancingTest, IntegratedBalancing) {
    const char* input_json = R"({
        "consecutiveStints": 1,
        "minimumRestHours": 0,
        "teamMembers": [
            { "name": "DriverA", "isDriver": true, "isSpotter": false },
            { "name": "DriverB", "isDriver": true, "isSpotter": false },
            { "name": "Spotter1", "isDriver": false, "isSpotter": true },
            { "name": "Spotter2", "isDriver": false, "isSpotter": true }
        ],
        "availability": {},
        "stints": [
            { "id": 1, "startTime": "2023-01-01T12:00:00Z", "endTime": "2023-01-01T13:00:00Z" },
            { "id": 2, "startTime": "2023-01-01T13:00:00Z", "endTime": "2023-01-01T14:00:00Z" },
            { "id": 3, "startTime": "2023-01-01T14:00:00Z", "endTime": "2023-01-01T15:00:00Z" },
            { "id": 4, "startTime": "2023-01-01T15:00:00Z", "endTime": "2023-01-01T16:00:00Z" },
            { "id": 5, "startTime": "2023-01-01T16:00:00Z", "endTime": "2023-01-01T17:00:00Z" },
            { "id": 6, "startTime": "2023-01-01T17:00:00Z", "endTime": "2023-01-01T18:00:00Z" },
            { "id": 7, "startTime": "2023-01-01T18:00:00Z", "endTime": "2023-01-01T19:00:00Z" },
            { "id": 8, "startTime": "2023-01-01T19:00:00Z", "endTime": "2023-01-01T20:00:00Z" }
        ]
    })";

    JresSolverInput* input = jres_input_from_json(input_json);
    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.switchingPenalty = 0.0;
    options.rotationBeatWeight = 0.0;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->diagnosis_len, 0);

    std::map<std::string, int> counts;
    for (int i = 0; i < output->schedule_len; ++i) {
        counts[output->schedule[i].spotter]++;
    }

    EXPECT_EQ(counts["Spotter1"], 4);
    EXPECT_EQ(counts["Spotter2"], 4);

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

TEST(SpotterBalancingTest, SequentialBalancing) {
    const char* input_json = R"({
        "consecutiveStints": 1,
        "minimumRestHours": 0,
        "teamMembers": [
            { "name": "DriverA", "isDriver": true, "isSpotter": false },
            { "name": "DriverB", "isDriver": true, "isSpotter": false },
            { "name": "Spotter1", "isDriver": false, "isSpotter": true },
            { "name": "Spotter2", "isDriver": false, "isSpotter": true }
        ],
        "availability": {},
        "stints": [
            { "id": 1, "startTime": "2023-01-01T12:00:00Z", "endTime": "2023-01-01T13:00:00Z" },
            { "id": 2, "startTime": "2023-01-01T13:00:00Z", "endTime": "2023-01-01T14:00:00Z" },
            { "id": 3, "startTime": "2023-01-01T14:00:00Z", "endTime": "2023-01-01T15:00:00Z" },
            { "id": 4, "startTime": "2023-01-01T15:00:00Z", "endTime": "2023-01-01T16:00:00Z" },
            { "id": 5, "startTime": "2023-01-01T16:00:00Z", "endTime": "2023-01-01T17:00:00Z" },
            { "id": 6, "startTime": "2023-01-01T17:00:00Z", "endTime": "2023-01-01T18:00:00Z" },
            { "id": 7, "startTime": "2023-01-01T18:00:00Z", "endTime": "2023-01-01T19:00:00Z" },
            { "id": 8, "startTime": "2023-01-01T19:00:00Z", "endTime": "2023-01-01T20:00:00Z" }
        ]
    })";

    JresSolverInput* input = jres_input_from_json(input_json);
    JresSolverOptions options = {};
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.switchingPenalty = 0.0;
    options.rotationBeatWeight = 0.0;

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->diagnosis_len, 0);

    std::map<std::string, int> counts;
    for (int i = 0; i < output->schedule_len; ++i) {
        counts[output->schedule[i].spotter]++;
    }

    EXPECT_EQ(counts["Spotter1"], 4);
    EXPECT_EQ(counts["Spotter2"], 4);

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
