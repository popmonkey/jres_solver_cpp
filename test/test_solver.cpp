#include "gtest/gtest.h"
#include "jres_solver.hpp"
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Test: A solvable race ---
const char* SOLVABLE_JSON = R"({
  "availability": {
    "Nikki": {
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
    { "name": "Nikki", "isDriver": true, "isSpotter": true, "preferredStints": 2 },
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
// Note: This 1-hour race will result in 2 stints:
// Stint 0 @ 14:37 (needs 14:00 and 15:00 keys)
// Stint 1 @ 15:27 (needs 15:00 and 16:00 keys)

TEST(SolverTest, BasicIntegratedSolve) {
    JresSolverOptions options;
    options.timeLimit = 30;
    options.spotterMode = "integrated";
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.quiet = true;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(SOLVABLE_JSON, options, &resultJsonCStr);

    // Check for successful run
    ASSERT_EQ(resultCode, 0);
    ASSERT_NE(resultJsonCStr, nullptr);

    // Parse the result JSON
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr); // Free memory
    json resultJson = json::parse(resultJsonString);

    // Check the solution
    ASSERT_TRUE(resultJson["success"].get<bool>());
    ASSERT_EQ(resultJson["schedule"].size(), 2);

    // Stint 1 (idx 0):
    // Nikki is unavailable ("14:00"). Ayrton must drive.
    // Alain must spot (Ayrton is driving).
    EXPECT_EQ(resultJson["schedule"][0]["driver"].get<std::string>(), "Ayrton");
    EXPECT_EQ(resultJson["schedule"][0]["spotter"].get<std::string>(), "Alain");

    // Stint 2 (idx 1):
    // Both Nikki and Ayrton are available. Solver will pick one.
    // Alain is also available as a spotter.
    // We just assert that a valid driver and spotter were assigned.
    EXPECT_NE(resultJson["schedule"][1]["driver"].get<std::string>(), "N/A");
    EXPECT_NE(resultJson["schedule"][1]["spotter"].get<std::string>(), "N/A");
    EXPECT_NE(resultJson["schedule"][1]["driver"].get<std::string>(), "Alain");
}

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
// Note: This 0.5-hour race will result in 1 stint:
// Stint 0 @ 14:37 (needs 14:00 and 14:00 keys)
// Both drivers are unavailable.

TEST(SolverTest, InfeasibleModel) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = "none";
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;
    options.quiet = true;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(INFEASIBLE_JSON, options, &resultJsonCStr);

    // Check for failure code
    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);

    // Parse the error JSON
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    // Check the error message
    ASSERT_FALSE(resultJson["success"].get<bool>());
    
    // After we fix `jres_solver.cpp`, this test will correctly
    // fail with "Model is infeasible..." instead of "Invalid race parameters..."
    EXPECT_EQ(resultJson["error"].get<std::string>(), "Model is infeasible. No solution exists.");
}