#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include <string>

namespace {
    const char* UNAVAILABLE_V2_JSON = R"({
      "teamMembers": [
        { "name": "Ayrton", "isDriver": true }
      ],
      "availability": {
        "Ayrton": { "1973-06-09T14:00:00.000Z": "Unavailable" }
      },
      "stints": [
        { "id": 1, "startTime": "1973-06-09T14:00:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" }
      ]
    })";
    TEST(DiagnosisTest, UnavailableDriver) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        JresSolverInput* input = jres_input_from_json(UNAVAILABLE_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = diagnose_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        bool found = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg(output->diagnosis[i]);
            if (msg.find("CRITICAL: No drivers could be assigned") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'CRITICAL: No drivers could be assigned'";
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* MAX_CONSECUTIVE_V2_JSON = R"({
      "teamMembers": [
        { "name": "Ayrton", "isDriver": true, "maxStints": 1, "minimumRestHours": 0 }
      ],
      "availability": {
        "Ayrton": {
          "1973-06-09T14:00:00.000Z": "Available",
          "1973-06-09T15:00:00.000Z": "Available",
          "1973-06-09T16:00:00.000Z": "Available"
        }
      },
      "stints": [
        { "id": 1, "startTime": "1973-06-09T14:00:00.000Z", "endTime": "1973-06-09T14:30:00.000Z" },
        { "id": 2, "startTime": "1973-06-09T14:30:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" },
        { "id": 3, "startTime": "1973-06-09T15:00:00.000Z", "endTime": "1973-06-09T15:30:00.000Z" }
      ]
    })";
    TEST(DiagnosisTest, MaxConsecutive) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        JresSolverInput* input = jres_input_from_json(MAX_CONSECUTIVE_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = diagnose_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        bool found = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg(output->diagnosis[i]);
            if (msg.find("exceeded max consecutive stint limit") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'exceeded max consecutive stint limit'";
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* UNAVAILABLE_SPOTTER_INTEGRATED_V2_JSON = R"({
      "teamMembers": [
        { "name": "Lauda", "isDriver": true, "minimumRestHours": 0 },
        { "name": "Prost", "isSpotter": true, "minimumRestHours": 0 }
      ],
      "availability": {
        "Lauda": { "1973-06-09T14:00:00.000Z": "Available" },
        "Prost": { "1973-06-09T14:00:00.000Z": "Unavailable" }
      },
      "stints": [
        { "id": 1, "startTime": "1973-06-09T14:00:00.000Z", "endTime": "1973-06-09T14:30:00.000Z" }
      ]
    })";
    TEST(DiagnosisTest, UnavailableSpotterIntegrated) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
        options.allowNoSpotter = false;
        JresSolverInput* input = jres_input_from_json(UNAVAILABLE_SPOTTER_INTEGRATED_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = diagnose_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        bool found = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg(output->diagnosis[i]);
            if (msg.find("CRITICAL: No spotters could be assigned") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'CRITICAL: No spotters could be assigned'";
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* SEQUENTIAL_SPOTTER_MODE_V2_JSON = R"({
      "teamMembers": [
        { "name": "Lauda", "isDriver": true, "isSpotter": true, "minimumRestHours": 0 },
        { "name": "Prost", "isDriver": true, "isSpotter": true, "minimumRestHours": 0 },
        { "name": "Senna", "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
      ],
      "availability": {
        "Lauda": { "1973-06-09T14:00:00.000Z": "Available" },
        "Prost": { "1973-06-09T14:00:00.000Z": "Available" },
        "Senna": { "1973-06-09T14:00:00.000Z": "Available" }
      },
      "stints": [
        { "id": 1, "startTime": "1973-06-09T14:00:00.000Z", "endTime": "1973-06-09T14:30:00.000Z" },
        { "id": 2, "startTime": "1973-06-09T14:30:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" }
      ]
    })";
    TEST(DiagnosisTest, SequentialSpotterMode) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
        options.allowNoSpotter = false;
        JresSolverInput* input = jres_input_from_json(SEQUENTIAL_SPOTTER_MODE_V2_JSON);
        ASSERT_NE(input, nullptr);
        JresSolverOutput* output = diagnose_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);
        ASSERT_GT(output->diagnosis_len, 0);
        bool found = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg(output->diagnosis[i]);
            if (msg.find("Diagnosis complete.") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'Diagnosis complete.'";
        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }
}