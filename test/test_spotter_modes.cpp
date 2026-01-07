#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

namespace {
  const char* SOLVABLE_V2_JSON = R"({
    "teamMembers": [
      { "name": "Niki", "isDriver": true, "isSpotter": true, "maxStints": 2, "minimumRestHours": 0 },
      { "name": "Ayrton", "isDriver": true, "isSpotter": true, "maxStints": 2, "minimumRestHours": 0 },
      { "name": "Alain", "isDriver": false, "isSpotter": true, "maxStints": 2, "minimumRestHours": 0 }
    ],
    "availability": {
      "Niki": {
        "1973-06-09T14:00:00.000Z": "Unavailable",
        "1973-06-09T15:00:00.000Z": "Available",
        "1973-06-09T16:00:00.000Z": "Available"
      },
      "Ayrton": {
        "1973-06-09T14:00:00.000Z": "Available",
        "1973-06-09T15:00:00.000Z": "Available",
        "1973-06-09T16:00:00.000Z": "Available"
      },
      "Alain": {
        "1973-06-09T14:00:00.000Z": "Available",
        "1973-06-09T15:00:00.000Z": "Available",
        "1973-06-09T16:00:00.000Z": "Available"
      }
    },
    "stints": [
        { "id": 1, "startTime": "1973-06-09T14:37:00.000Z", "endTime": "1973-06-09T15:27:16.500Z" },
        { "id": 2, "startTime": "1973-06-09T15:27:16.500Z", "endTime": "1973-06-09T16:17:33.000Z" }
    ]
  })";

  TEST(SpotterModeTest, BasicIntegratedSolve) {
      JresSolverOptions options;
      options.timeLimit = 30;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(SOLVABLE_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 2);
      
      // Stint 1 (id 1):
      // Niki is unavailable. Ayrton must drive.
      // Alain must spot (Ayrton is driving).
      EXPECT_STREQ(output->schedule[0].driver, "Ayrton");
      EXPECT_STREQ(output->schedule[0].spotter, "Alain");

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  const char* SPOTTER_NONE_V2_JSON = R"({
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
    ],
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "stints": [
        { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
    ]
  })";

  TEST(SpotterModeTest, ModeNone) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_NONE;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(SPOTTER_NONE_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 1);
      
      EXPECT_STREQ(output->schedule[0].spotter, "N/A");

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  const char* CONFLICT_INTEGRATED_V2_JSON = R"({
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
    ],
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "stints": [
        { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
    ]
  })";

  TEST(SpotterModeTest, IntegratedConflictInfeasible) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(CONFLICT_INTEGRATED_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 0);
      ASSERT_GT(output->diagnosis_len, 0);
      std::string msg(output->diagnosis[0]);
      EXPECT_TRUE(msg.find("Model is infeasible") != std::string::npos);

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  const char* CONFLICT_SEQUENTIAL_V2_JSON = R"({
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 },
      { "name": "Alain", "isDriver": false, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
    ],
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Alain": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "stints": [
        { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
    ]
  })";

  TEST(SpotterModeTest, SequentialConflictSolvable) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(CONFLICT_SEQUENTIAL_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 1);
      
      EXPECT_STREQ(output->schedule[0].driver, "Ayrton");
      EXPECT_STREQ(output->schedule[0].spotter, "Alain");

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  const char* NO_SPOTTERS_V2_JSON = R"({
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": false, "maxStints": 1, "minimumRestHours": 0 },
      { "name": "Niki", "isDriver": false, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
    ],
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Niki": { "1970-01-01T10:00:00.000Z": "Unavailable" }
    },
    "stints": [
        { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
    ]
  })";

  TEST(SpotterModeTest, AllowNoSpotterIntegrated) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = true;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(NO_SPOTTERS_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 1);
      
      EXPECT_STREQ(output->schedule[0].driver, "Ayrton");
      EXPECT_STREQ(output->schedule[0].spotter, "N/A");

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  TEST(SpotterModeTest, AllowNoSpotterSequential) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
      options.allowNoSpotter = true;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(NO_SPOTTERS_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 1);
      
      EXPECT_STREQ(output->schedule[0].driver, "Ayrton");
      EXPECT_STREQ(output->schedule[0].spotter, "N/A");

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }

  const char* SEQ_INFEASIBLE_V2_JSON = R"({
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": false, "maxStints": 1, "minimumRestHours": 0 },
      { "name": "Niki", "isDriver": false, "isSpotter": true, "maxStints": 1, "minimumRestHours": 0 }
    ],
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Niki": { "1970-01-01T10:00:00.000Z": "Unavailable" }
    },
    "stints": [
        { "id": 1, "startTime": "1970-01-01T10:00:00.000Z", "endTime": "1970-01-01T10:30:00.000Z" }
    ]
  })";

  TEST(SpotterModeTest, SequentialInfeasibleSpotter) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;

      JresSolverInput* input = jres_input_from_json(SEQ_INFEASIBLE_V2_JSON);
      ASSERT_NE(input, nullptr);

      JresSolverOutput* output = solve_race_schedule(input, &options);
      ASSERT_NE(output, nullptr);

      ASSERT_EQ(output->schedule_len, 1);
      
      EXPECT_STREQ(output->schedule[0].driver, "Ayrton");
      // Elastic solver will assign Niki despite unavailability, but report violation
      EXPECT_STREQ(output->schedule[0].spotter, "Niki");
      
      bool found = false;
      for (int i = 0; i < output->diagnosis_len; ++i) {
          std::string msg(output->diagnosis[i]);
          if (msg.find("Violation: Unavailable Spotter") != std::string::npos) {
              found = true;
              break;
          }
      }
      EXPECT_TRUE(found);

      free_jres_solver_input(input);
      free_jres_solver_output(output);
  }
}
