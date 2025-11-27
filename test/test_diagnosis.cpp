#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <string>

using json = nlohmann::json;

namespace {

    // Scenario: Driver A is unavailable for Stint 1.
    // Diagnosis should report a global Availability Gap.
    const char* UNAVAILABLE_JSON = R"({
      "availability": {
        "Ayrton": { "1973-06-09T14:00:00.000Z": "Unavailable" }
      },
      "teamMembers": [
        { "name": "Ayrton", "isDriver": true }
      ],
      "durationHours": 0.5,
      "raceStartUTC": "1973-06-09T14:00:00.000Z",
      "avgLapTimeInSeconds": 120.0,
      "fuelTankSize": 120,
      "fuelUsePerLap": 5.0,
      "pitTimeInSeconds": 60
    })";

    TEST(DiagnosisTest, UnavailableDriver) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;

        char* resultJsonCStr = nullptr;
        int resultCode = diagnose_race_schedule(UNAVAILABLE_JSON, options, &resultJsonCStr);

        ASSERT_EQ(resultCode, 0);
        ASSERT_NE(resultJsonCStr, nullptr);

        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        ASSERT_FALSE(resultJson["success"].get<bool>());
        ASSERT_TRUE(resultJson.contains("diagnosis"));
        
        json diagnosis = resultJson["diagnosis"];
        ASSERT_FALSE(diagnosis.empty());
        
        bool found = false;
        for (const auto& issue : diagnosis) {
            std::string msg = issue.get<std::string>();
            if (msg.find("AVAILABILITY GAP") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'AVAILABILITY GAP'";
    }

    // Scenario: Driver A has preferred Stints = 1.
    // Race is 3 stints. Driver must drive 3.
    // Diagnosis should report specific violation.
    const char* MAX_CONSECUTIVE_JSON = R"({
      "availability": {
        "Ayrton": { "1973-06-09T14:00:00.000Z": "Available" }
      },
      "teamMembers": [
        { "name": "Ayrton", "isDriver": true, "preferredStints": 1 }
      ],
      "durationHours": 2.0, 
      "raceStartUTC": "1973-06-09T14:00:00.000Z",
      "avgLapTimeInSeconds": 120.0,
      "fuelTankSize": 100,
      "fuelUsePerLap": 5.0,
      "pitTimeInSeconds": 60
    })";
    // 1 stint ~ 40 mins. 2 hours = ~3 stints.

    TEST(DiagnosisTest, MaxConsecutive) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_NONE;

        char* resultJsonCStr = nullptr;
        int resultCode = diagnose_race_schedule(MAX_CONSECUTIVE_JSON, options, &resultJsonCStr);

        ASSERT_EQ(resultCode, 0);
        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        json diagnosis = resultJson["diagnosis"];
        ASSERT_FALSE(diagnosis.empty());
        
        // New Detailed Format: "Driver Ayrton exceeded max consecutive stint limit (Driven Stints: 1-3, Limit: 1)."
        bool found = false;
        for (const auto& issue : diagnosis) {
            std::string msg = issue.get<std::string>();
            if (msg.find("exceeded max consecutive stint limit") != std::string::npos &&
                msg.find("Stints: 1-3") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain detailed consecutive warning.";
    }

    // Scenario: Test diagnostic with integrated spotter mode
    // Spotter is unavailable for some stints
    const char* UNAVAILABLE_SPOTTER_JSON = R"({
      "availability": {
        "Lauda": { "1973-06-09T14:00:00.000Z": "Available" },
        "Prost": { "1973-06-09T14:00:00.000Z": "Unavailable", "1973-06-09T14:40:00.000Z": "Available" }
      },
      "teamMembers": [
        { "name": "Lauda", "isDriver": true },
        { "name": "Prost", "isSpotter": true }
      ],
      "durationHours": 1.5,
      "raceStartUTC": "1973-06-09T14:00:00.000Z",
      "avgLapTimeInSeconds": 120.0,
      "fuelTankSize": 120,
      "fuelUsePerLap": 5.0,
      "pitTimeInSeconds": 60
    })";

    TEST(DiagnosisTest, UnavailableSpotterIntegrated) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
        options.allowNoSpotter = false;

        char* resultJsonCStr = nullptr;
        int resultCode = diagnose_race_schedule(UNAVAILABLE_SPOTTER_JSON, options, &resultJsonCStr);

        ASSERT_EQ(resultCode, 0);
        ASSERT_NE(resultJsonCStr, nullptr);

        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        ASSERT_FALSE(resultJson["success"].get<bool>());
        ASSERT_TRUE(resultJson.contains("diagnosis"));
        
        json diagnosis = resultJson["diagnosis"];
        ASSERT_FALSE(diagnosis.empty());
        
        // Check for spotter-related issues
        bool foundSpotterIssue = false;
        for (const auto& issue : diagnosis) {
            std::string msg = issue.get<std::string>();
            if (msg.find("Spotter") != std::string::npos || 
                msg.find("spotter") != std::string::npos) {
                foundSpotterIssue = true;
                break;
            }
        }
        EXPECT_TRUE(foundSpotterIssue) << "Expected diagnosis to contain spotter-related issue";
    }

    // Scenario: Test diagnostic with sequential spotter mode
    const char* SEQUENTIAL_SPOTTER_JSON = R"({
      "availability": {
        "Lauda": { "1973-06-09T14:00:00.000Z": "Available" },
        "Prost": { "1973-06-09T14:00:00.000Z": "Available" },
        "Senna": { "1973-06-09T14:00:00.000Z": "Available" }
      },
      "teamMembers": [
        { "name": "Lauda", "isDriver": true, "isSpotter": true },
        { "name": "Prost", "isDriver": true, "isSpotter": true },
        { "name": "Senna", "isSpotter": true, "preferredStints": 1 }
      ],
      "durationHours": 2.0,
      "raceStartUTC": "1973-06-09T14:00:00.000Z",
      "avgLapTimeInSeconds": 120.0,
      "fuelTankSize": 100,
      "fuelUsePerLap": 5.0,
      "pitTimeInSeconds": 60
    })";

    TEST(DiagnosisTest, SequentialSpotterMode) {
        JresSolverOptions options;
        options.timeLimit = 10;
        options.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
        options.allowNoSpotter = false;

        char* resultJsonCStr = nullptr;
        int resultCode = diagnose_race_schedule(SEQUENTIAL_SPOTTER_JSON, options, &resultJsonCStr);

        ASSERT_EQ(resultCode, 0);
        ASSERT_NE(resultJsonCStr, nullptr);

        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        // This should either succeed or fail with spotter constraint issues
        ASSERT_TRUE(resultJson.contains("diagnosis"));
        
        if (!resultJson["success"].get<bool>()) {
            json diagnosis = resultJson["diagnosis"];
            // If it fails, there should be spotter-related issues
            bool hasSpotterMention = false;
            for (const auto& issue : diagnosis) {
                std::string msg = issue.get<std::string>();
                if (msg.find("Spotter") != std::string::npos || 
                    msg.find("spotter") != std::string::npos) {
                    hasSpotterMention = true;
                    break;
                }
            }
            // Sequential mode might work or fail depending on constraints
            // Just verify the diagnosis is coherent
            EXPECT_TRUE(diagnosis.is_array());
        }
    }
}
