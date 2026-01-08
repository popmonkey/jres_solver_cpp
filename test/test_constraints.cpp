/**
 * @file test/test_constraints.cpp
 * @brief Tests for various hard constraints (infeasibility, preferred slots, coverage).
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Test: An infeasible race ---
const char* INFEASIBLE_V2_JSON = R"({
  "teamMembers": [
    { "name": "Niki", "isDriver": true },
    { "name": "Ayrton", "isDriver": true }
  ],
  "availability": {
    "Niki": { "1973-06-09T14:00:00.000Z": "Unavailable" },
    "Ayrton": { "1973-06-09T14:00:00.000Z": "Unavailable" }
  },
  "stints": [
    { "id": 1, "startTime": "1973-06-09T14:37:00.000Z", "endTime": "1973-06-09T15:00:00.000Z" }
  ]
})";

TEST(ConstraintTest, InfeasibleModel) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(INFEASIBLE_V2_JSON);
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);

    // Infeasible, so schedule should be empty
    ASSERT_EQ(output->schedule_len, 0);

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

const char* PREFERRED_SLOT_V2_JSON = R"({
  "consecutiveStints": 1,
  "minimumRestHours": 0,
  "teamMembers": [
    { "name": "Driver A", "isDriver": true },
    { "name": "Driver B", "isDriver": true }
  ],
  "availability": {
    "Driver A": {
      "1970-01-01T10:00:00.000Z": "Available"
    },
    "Driver B": {
      "1970-01-01T10:00:00.000Z": "Preferred"
    }
  },
  "stints": [
    { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
  ]
})";

TEST(ConstraintTest, PreferredSlot) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(PREFERRED_SLOT_V2_JSON);
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);

    ASSERT_EQ(output->schedule_len, 1);
    free_jres_solver_input(input);
    free_jres_solver_output(output);
}

const char* NO_DRIVER_FOR_STINT_V2_JSON = R"({
  "consecutiveStints": 2,
  "minimumRestHours": 0,
  "teamMembers": [
    { "name": "Brandon", "isDriver": true, "isSpotter": true },
    { "name": "Cesar", "isDriver": true, "isSpotter": true },
    { "name": "Harvey", "isDriver": true, "isSpotter": true },
    { "name": "Jay", "isDriver": true, "isSpotter": true },
    { "name": "Jack", "isDriver": true, "isSpotter": true }
  ],
  "availability": {
    "Brandon": { "2025-11-22T14:00:00.000Z": "Unavailable" },
    "Cesar": { "2025-11-22T14:00:00.000Z": "Unavailable" },
    "Harvey": { "2025-11-22T14:00:00.000Z": "Unavailable" },
    "Jay": { "2025-11-22T14:00:00.000Z": "Unavailable" },
    "Jack": { "2025-11-22T14:00:00.000Z": "Unavailable" }
  },
  "stints": [
    { "id": 1, "startTime": "2025-11-22T14:00:00.000Z", "endTime": "2025-11-22T15:00:00.000Z" }
  ]
})";

TEST(ConstraintTest, NoDriverForStint) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(NO_DRIVER_FOR_STINT_V2_JSON);
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);

    ASSERT_EQ(output->schedule_len, 0);
    ASSERT_GT(output->diagnosis_len, 0);
    std::string msg(output->diagnosis[0]);
    bool correctMsg = (msg.find("Insufficient driver capacity") != std::string::npos);
    bool correctDetails = (msg.find(", MinRest=") != std::string::npos);
    EXPECT_EQ(correctMsg, true);
    EXPECT_EQ(correctDetails, true);

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
