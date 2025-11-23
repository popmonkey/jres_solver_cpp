#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

namespace {
  // --- Test 1: Basic Integrated Solve (Moved from test_solver.cpp) ---
  const char* SOLVABLE_JSON = R"({
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
    "teamMembers": [
      { "name": "Niki", "isDriver": true, "isSpotter": true, "preferredStints": 2 },
      { "name": "Ayrton", "isDriver": true, "isSpotter": true, "preferredStints": 2 },
      { "name": "Alain", "isDriver": false, "isSpotter": true, "preferredStints": 2 }
    ],
    "durationHours": 1.0,
    "raceStartUTC": "1973-06-09T14:37:00.000Z",
    "avgLapTimeInSeconds": 220.5,
    "fuelTankSize": 120,
    "fuelUsePerLap": 9.2,
    "pitTimeInSeconds": 150
  })";

  TEST(SpotterModeTest, BasicIntegratedSolve) {
      JresSolverOptions options;
      options.timeLimit = 30;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(SOLVABLE_JSON, options, &resultJsonCStr);

      ASSERT_EQ(resultCode, 0);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_TRUE(resultJson["success"].get<bool>());
      ASSERT_EQ(resultJson["schedule"].size(), 2);
      
      // Stint 1 (idx 0):
      // Niki is unavailable ("14:00"). Ayrton must drive.
      // Alain must spot (Ayrton is driving).
      EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
      EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "Alain");
  }

  // --- Test 2: Mode None ---
  const char* SPOTTER_NONE_JSON = R"({
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true }
    ],
    "durationHours": 0.5,
    "raceStartUTC": "1970-01-01T10:00:00.000Z",
    "avgLapTimeInSeconds": 120, "fuelTankSize": 100, "fuelUsePerLap": 5, "pitTimeInSeconds": 60
  })";

  TEST(SpotterModeTest, ModeNone) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_NONE;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(SPOTTER_NONE_JSON, options, &resultJsonCStr);

      ASSERT_EQ(resultCode, 0);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_TRUE(resultJson["success"].get<bool>());
      ASSERT_EQ(resultJson["schedule"].size(), 1);
      
      // Assert that the "spotter" field does NOT exist
      EXPECT_FALSE(resultJson["schedule"][0].contains("spotter"));
  }

  // --- Test 3: Integrated Driver/Spotter Conflict (Infeasible) ---
  const char* CONFLICT_INTEGRATED_JSON = R"({
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true }
    ],
    "durationHours": 0.5,
    "raceStartUTC": "1970-01-01T10:00:00.000Z",
    "avgLapTimeInSeconds": 120, "fuelTankSize": 100, "fuelUsePerLap": 5, "pitTimeInSeconds": 60
  })";
  // Note: 1 stint. Ayrton is the ONLY driver and ONLY spotter.
  // In Integrated mode, he cannot be both.

  TEST(SpotterModeTest, IntegratedConflictInfeasible) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = false; // <-- This forces the conflict
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(CONFLICT_INTEGRATED_JSON, options, &resultJsonCStr);

      // This must fail, as the model is infeasible
      ASSERT_EQ(resultCode, -1);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_FALSE(resultJson["success"].get<bool>());
      EXPECT_EQ(resultJson["error"].get<std::string>(), "Model is infeasible. No solution exists.");
  }

  // --- Test 4: Sequential Driver/Spotter Conflict (Solvable) ---
  const char* CONFLICT_SEQUENTIAL_JSON = R"({
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Alain": { "1970-01-01T10:00:00.000Z": "Available" }
    },
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": true },
      { "name": "Alain", "isDriver": false, "isSpotter": true }
    ],
    "durationHours": 0.5,
    "raceStartUTC": "1970-01-01T10:00:00.000Z",
    "avgLapTimeInSeconds": 120, "fuelTankSize": 100, "fuelUsePerLap": 5, "pitTimeInSeconds": 60
  })";
  // Note: 1 stint. Ayrton is only driver. Ayrton and Alain are spotters.
  // Sequential mode must assign Alain to spot, as Ayrton is driving.

  TEST(SpotterModeTest, SequentialConflictSolvable) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
      options.allowNoSpotter = false;
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(CONFLICT_SEQUENTIAL_JSON, options, &resultJsonCStr);

      ASSERT_EQ(resultCode, 0);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_TRUE(resultJson["success"].get<bool>());
      ASSERT_EQ(resultJson["schedule"].size(), 1);
      
      // Ayrton MUST drive
      EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
      // Alain MUST spot, because Ayrton is blocked by driving
      EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "Alain");
  }

  // --- Test 5: Allow No Spotter (Integrated) ---
  const char* NO_SPOTTERS_JSON = R"({
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Niki": { "1970-01-01T10:00:00.000Z": "Unavailable" }
    },
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": false },
      { "name": "Niki", "isDriver": false, "isSpotter": true }
    ],
    "durationHours": 0.5,
    "raceStartUTC": "1970-01-01T10:00:00.000Z",
    "avgLapTimeInSeconds": 120, "fuelTankSize": 100, "fuelUsePerLap": 5, "pitTimeInSeconds": 60
  })";
  // Note: 1 stint. Ayrton (driver) is available.
  // Niki (spotter) is unavailable.

  TEST(SpotterModeTest, AllowNoSpotterIntegrated) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
      options.allowNoSpotter = true; // <-- Key for this test
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(NO_SPOTTERS_JSON, options, &resultJsonCStr);

      ASSERT_EQ(resultCode, 0);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_TRUE(resultJson["success"].get<bool>());
      ASSERT_EQ(resultJson["schedule"].size(), 1);
      
      // Ayrton drives
      EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
      // No spotter is available, but this is allowed
      EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "N/A");
  }

  // --- Test 6: Allow No Spotter (Sequential) ---
  // We can reuse the same JSON as Test 5.

  TEST(SpotterModeTest, AllowNoSpotterSequential) {
      JresSolverOptions options;
      options.timeLimit = 10;
      options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
      options.allowNoSpotter = true; // <-- Key for this test
      options.optimalityGap = 0.0;
      options.quiet = true;

      char* resultJsonCStr = nullptr;
      int resultCode = solve_race_schedule(NO_SPOTTERS_JSON, options, &resultJsonCStr);

      ASSERT_EQ(resultCode, 0);
      ASSERT_NE(resultJsonCStr, nullptr);
      std::string resultJsonString(resultJsonCStr);
      free_solver_result(resultJsonCStr);
      json resultJson = json::parse(resultJsonString);

      ASSERT_TRUE(resultJson["success"].get<bool>());
      ASSERT_EQ(resultJson["schedule"].size(), 1);
      
      // Ayrton drives
      EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
      // No spotter is available, but this is allowed
      EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "N/A");
  }

  // --- Test 7: Sequential Infeasible Spotter ---
  // This test ensures that a failed *spotter* solve in sequential mode
  // does not fail the *entire* solve.
  const char* SEQ_INFEASIBLE_JSON = R"({
    "availability": {
      "Ayrton": { "1970-01-01T10:00:00.000Z": "Available" },
      "Niki": { "1970-01-01T10:00:00.000Z": "Unavailable" }
    },
    "teamMembers": [
      { "name": "Ayrton", "isDriver": true, "isSpotter": false },
      { "name": "Niki", "isDriver": false, "isSpotter": true }
    ],
    "durationHours": 0.5,
    "raceStartUTC": "1970-01-01T10:00:00.000Z",
    "avgLapTimeInSeconds": 120, "fuelTankSize": 100, "fuelUsePerLap": 5, "pitTimeInSeconds": 60
  })";
  // Note: 1 stint. Ayrton (driver) is available.
  // Niki (spotter) is unavailable. `allowNoSpotter` is FALSE.
}

TEST(SpotterModeTest, SequentialInfeasibleSpotter) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
    options.allowNoSpotter = false; // <-- Key for this test
    options.optimalityGap = 0.0;
    options.quiet = true;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(SEQ_INFEASIBLE_JSON, options, &resultJsonCStr);

    // The OVERALL solve should SUCCEED
    ASSERT_EQ(resultCode, 0);
    ASSERT_NE(resultJsonCStr, nullptr);
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    ASSERT_TRUE(resultJson["success"].get<bool>());
    ASSERT_EQ(resultJson["schedule"].size(), 1);
    
    // Ayrton drives
    EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
    // The sequential spotter solve failed, so this is N/A
    EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "N/A");
}
