#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include <string>

namespace {
    const char* MALFORMED_JSON = R"({ "teamMembers": [ { "name": "Niki", "isDriver": true } )";
    TEST(ErrorTest, MalformedJson) {
        JresSolverInput* input = jres_input_from_json(MALFORMED_JSON);
        ASSERT_EQ(input, nullptr);
    }

    const char* MISSING_KEY_JSON = R"({ "stints": [] })";
    TEST(ErrorTest, MissingSchemaKey) {
        JresSolverInput* input = jres_input_from_json(MISSING_KEY_JSON);
        ASSERT_EQ(input, nullptr);
    }

    const char* NO_DRIVERS_V2_JSON = R"({
      "teamMembers": [ { "name": "Alain", "isDriver": false, "isSpotter": true } ],
      "availability": {},
      "stints": [ { "id": 1, "startTime": "1973-06-09T14:37:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" } ]
    })";
    TEST(ErrorTest, NoDrivers) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        JresSolverInput* input = jres_input_from_json(NO_DRIVERS_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        std::string msg(output->diagnosis[0]);
        EXPECT_TRUE(msg.find("No drivers available") != std::string::npos);
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* NO_SPOTTERS_REQUIRED_V2_JSON = R"({
      "teamMembers": [      { "name": "Ayrton", "isDriver": true, "isSpotter": false, "maxStints": 1, "minimumRestHours": 0 } ],
      "availability": { "Ayrton": { "1973-06-09T14:00:00.000Z": "Available" } },
      "stints": [ { "id": 1, "startTime": "1973-06-09T14:37:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" } ]
    })";
    TEST(ErrorTest, NoSpottersRequired) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
        options.allowNoSpotter = false;
        JresSolverInput* input = jres_input_from_json(NO_SPOTTERS_REQUIRED_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        std::string msg(output->diagnosis[0]);
        EXPECT_TRUE(msg.find("No spotters available for Integrated Mode") != std::string::npos);
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* ZERO_STINTS_V2_JSON = R"({
      "teamMembers": [ { "name": "Ayrton", "isDriver": true, "maxStints": 1, "minimumRestHours": 0 } ],
      "availability": { "Ayrton": { "1973-06-09T14:00:00.000Z": "Available" } },
      "stints": []
    })";
    TEST(ErrorTest, ZeroStints) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        JresSolverInput* input = jres_input_from_json(ZERO_STINTS_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(output->schedule_len, 0);
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }
}
