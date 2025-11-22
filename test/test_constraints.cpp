// test/test_constraints.cpp

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Test: An infeasible race ---
const char* INFEASIBLE_JSON = R"({
  "availability": {
    "Nikki": { "1973-06-09T14:00:00.000Z": "Unavailable" },
    "Ayrton": { "1973-06-09T14:00:00.000Z": "Unavailable" }
  },
  "teamMembers": [
    { "name": "Nikki", "isDriver": true },
    { "name": "Ayrton", "isDriver": true }
  ],
  "durationHours": 0.5,
  "raceStartUTC": "1973-06-09T14:37:00.000Z",
  "avgLapTimeInSeconds": 220.5,
  "fuelTankSize": 120,
  "fuelUsePerLap": 9.2,
  "pitTimeInSeconds": 150
})";

TEST(ConstraintTest, InfeasibleModel) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.quiet = true;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(INFEASIBLE_JSON, options, &resultJsonCStr);

    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);

    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    ASSERT_FALSE(resultJson["success"].get<bool>());
    EXPECT_EQ(resultJson["error"].get<std::string>(), "Model is infeasible. No solution exists.");
}

// --- Test: Preferred Availability ---
const char* PREFERRED_SLOT_JSON = R"({
  "availability": {
    "Driver A": {
      "1970-01-01T10:00:00.000Z": "Available"
    },
    "Driver B": {
      "1970-01-01T10:00:00.000Z": "Preferred"
    }
  },
  "teamMembers": [
    { "name": "Driver A", "isDriver": true, "preferredStints": 2 },
    { "name": "Driver B", "isDriver": true, "preferredStints": 2 }
  ],
  "durationHours": 0.5,
  "raceStartUTC": "1970-01-01T10:00:00.000Z",
  "avgLapTimeInSeconds": 120.0,
  "fuelTankSize": 100,
  "fuelUsePerLap": 5.0,
  "pitTimeInSeconds": 60
})";
// Note: This 0.5-hour race will result in 1 stint:
// Stint 0 @ 10:00 (needs 10:00 key)
// Both drivers are available, but B is preferred.

TEST(ConstraintTest, PreferredSlot) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.quiet = true;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(PREFERRED_SLOT_JSON, options, &resultJsonCStr);

    // Check for successful run
    ASSERT_EQ(resultCode, 0);
    ASSERT_NE(resultJsonCStr, nullptr);

    // Parse the result JSON
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr); // Free memory
    json resultJson = json::parse(resultJsonString);

    // Check the solution
    ASSERT_TRUE(resultJson["success"].get<bool>());
    ASSERT_EQ(resultJson["schedule"].size(), 1);

    // Assert that Driver B was chosen due to "Preferred"
    EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Driver B");
}
