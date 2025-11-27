#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <string> // For std::string::find

// Use the nlohmann::json namespace
using json = nlohmann::json;

namespace {
    // --- Test 1: Malformed JSON Input ---
    //  Missing closing brace
     const char* MALFORMED_JSON = R"({
    "availability": {
        "Niki": { "1973-06-09T14:00:00.000Z": "Available" }
    },
    "teamMembers": [
        { "name": "Niki", "isDriver": true }
    ]
    )";

    TEST(ErrorTest, MalformedJson) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;

        char* resultJsonCStr = nullptr;
        int resultCode = solve_race_schedule(MALFORMED_JSON, options, &resultJsonCStr);

        // Check for failure code
        ASSERT_EQ(resultCode, -1);
        ASSERT_NE(resultJsonCStr, nullptr);

        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        // Check the error message
        ASSERT_FALSE(resultJson["success"].get<bool>());
        
        // Check that the error is a nlohmann::json parse error
        std::string errorMsg = resultJson["error"].get<std::string>();
        EXPECT_TRUE(errorMsg.find("[json.exception.parse_error") != std::string::npos);
    }

    // --- Test 2: Invalid Schema (Missing Required Key) ---
    // "teamMembers" key is missing
    const char* MISSING_KEY_JSON = R"({
    "availability": {
        "Niki": { "1973-06-09T14:00:00.000Z": "Available" }
    },
    "durationHours": 0.5,
    "raceStartUTC": "1973-06-09T14:37:00.000Z",
    "avgLapTimeInSeconds": 220.5,
    "fuelTankSize": 120,
    "fuelUsePerLap": 9.2,
    "pitTimeInSeconds": 150
    })";
}

TEST(ErrorTest, MissingSchemaKey) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(MISSING_KEY_JSON, options, &resultJsonCStr);

    // Check for failure code
    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);

    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    // Check the error message
    ASSERT_FALSE(resultJson["success"].get<bool>());
    
    // Check that the error is a nlohmann::json key error
    std::string errorMsg = resultJson["error"].get<std::string>();
    EXPECT_TRUE(errorMsg.find("key 'teamMembers' not found") != std::string::npos);
}

// --- Test 3: Logical Error (No Drivers) ---
const char* NO_DRIVERS_JSON = R"({
  "availability": {
    "Alain": { "1973-06-09T14:00:00.000Z": "Available" }
  },
  "teamMembers": [
    { "name": "Alain", "isDriver": false, "isSpotter": true }
  ],
  "durationHours": 0.5,
  "raceStartUTC": "1973-06-09T14:37:00.000Z",
  "avgLapTimeInSeconds": 220.5, "fuelTankSize": 120, "fuelUsePerLap": 9.2, "pitTimeInSeconds": 150
})";

TEST(ErrorTest, NoDrivers) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(NO_DRIVERS_JSON, options, &resultJsonCStr);

    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    ASSERT_FALSE(resultJson["success"].get<bool>());
    EXPECT_EQ(resultJson["error"].get<std::string>(), "No drivers available for this race.");
}

// --- Test 4: Logical Error (No Spotters, Required) ---
const char* NO_SPOTTERS_JSON = R"({
  "availability": {
    "Ayrton": { "1973-06-09T14:00:00.000Z": "Available" }
  },
  "teamMembers": [
    { "name": "Ayrton", "isDriver": true, "isSpotter": false }
  ],
  "durationHours": 0.5,
  "raceStartUTC": "1973-06-09T14:37:00.000Z",
  "avgLapTimeInSeconds": 220.5, "fuelTankSize": 120, "fuelUsePerLap": 9.2, "pitTimeInSeconds": 150
})";

TEST(ErrorTest, NoSpottersRequired) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    options.allowNoSpotter = false; // <-- This makes it an error

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(NO_SPOTTERS_JSON, options, &resultJsonCStr);

    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    ASSERT_FALSE(resultJson["success"].get<bool>());
    EXPECT_EQ(resultJson["error"].get<std::string>(), "Spotter mode is 'integrated' but no spotters are available and 'allow-no-spotter' is false.");
}

// --- Test 5: Logical Error (Zero Duration) ---
const char* ZERO_DURATION_JSON = R"({
  "availability": {
    "Ayrton": { "1973-06-09T14:00:00.000Z": "Available" }
  },
  "teamMembers": [
    { "name": "Ayrton", "isDriver": true }
  ],
  "durationHours": 0.0,
  "raceStartUTC": "1973-06-09T14:37:00.000Z",
  "avgLapTimeInSeconds": 220.5, "fuelTankSize": 120, "fuelUsePerLap": 9.2, "pitTimeInSeconds": 150
})";

TEST(ErrorTest, ZeroDuration) {
    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;

    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(ZERO_DURATION_JSON, options, &resultJsonCStr);

    ASSERT_EQ(resultCode, -1);
    ASSERT_NE(resultJsonCStr, nullptr);
    std::string resultJsonString(resultJsonCStr);
    free_solver_result(resultJsonCStr);
    json resultJson = json::parse(resultJsonString);

    ASSERT_FALSE(resultJson["success"].get<bool>());
    EXPECT_EQ(resultJson["error"].get<std::string>(), "Invalid race parameters: totalStints must be > 0.");
}
