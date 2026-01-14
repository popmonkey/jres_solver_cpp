#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include <string>

namespace {

    const char* DUPLICATE_NAMES_JSON = R"({
      "teamMembers": [ 
          { "name": "DriverA", "isDriver": true },
          { "name": "DriverA", "isDriver": true }
      ],
      "availability": {},
      "stints": [ { "id": 1, "startTime": "2024-01-01T12:00:00.000Z", "endTime": "2024-01-01T13:00:00.000Z" } ]
    })";

    TEST(ValidationTest, DuplicateNames) {
        JresSolverOptions options = {};
        options.spotterMode = JRES_SPOTTER_MODE_NONE;
        
        JresSolverInput* input = jres_input_from_json(DUPLICATE_NAMES_JSON);
        ASSERT_NE(input, nullptr);

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);

        // Expect diagnosis
        ASSERT_GT(output->diagnosis_len, 0);
        bool foundDuplicateMsg = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg = output->diagnosis[i];
            if (msg.find("Duplicate team member name") != std::string::npos) {
                foundDuplicateMsg = true;
                break;
            }
        }
        EXPECT_TRUE(foundDuplicateMsg) << "Should diagnose duplicate names";

        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }

    const char* NO_SPOTTERS_SEQUENTIAL_JSON = R"({
      "teamMembers": [ 
          { "name": "DriverA", "isDriver": true, "isSpotter": false },
          { "name": "SpotterA", "isDriver": false, "isSpotter": false }
      ],
      "availability": {},
      "stints": [ { "id": 1, "startTime": "2024-01-01T12:00:00.000Z", "endTime": "2024-01-01T13:00:00.000Z" } ]
    })";

    TEST(ValidationTest, UnassignedSpotterSequential) {
        JresSolverOptions options = {};
        options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
        options.allowNoSpotter = false;
        
        JresSolverInput* input = jres_input_from_json(NO_SPOTTERS_SEQUENTIAL_JSON);
        ASSERT_NE(input, nullptr);

        JresSolverOutput* output = solve_race_schedule(input, &options);
        ASSERT_NE(output, nullptr);

        // Expect diagnosis about unassigned spotter
        ASSERT_GT(output->diagnosis_len, 0);
        bool foundSpecificMsg = false;
        for (int i = 0; i < output->diagnosis_len; ++i) {
            std::string msg = output->diagnosis[i];
            if (msg.find("Stint 0") != std::string::npos && msg.find("has no assigned spotter") != std::string::npos) {
                foundSpecificMsg = true;
                break;
            }
        }

        EXPECT_TRUE(foundSpecificMsg) << "Should diagnose specific unassigned stint for spotter";

        free_jres_solver_input(input);
        free_jres_solver_output(output);
    }
}
