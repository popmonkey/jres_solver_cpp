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
        options.quiet = true;

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
            // Updated to match new solver output: "AVAILABILITY: Drivers forced..."
            if (msg.find("AVAILABILITY:") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain 'AVAILABILITY:'";
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
        options.quiet = true;

        char* resultJsonCStr = nullptr;
        int resultCode = diagnose_race_schedule(MAX_CONSECUTIVE_JSON, options, &resultJsonCStr);

        ASSERT_EQ(resultCode, 0);
        std::string resultJsonString(resultJsonCStr);
        free_solver_result(resultJsonCStr);
        json resultJson = json::parse(resultJsonString);

        json diagnosis = resultJson["diagnosis"];
        ASSERT_FALSE(diagnosis.empty());
        
        // Updated to match new solver output: 
        // "Driver Ayrton exceeded max consecutive limit (Stints 1-3)."
        bool found = false;
        for (const auto& issue : diagnosis) {
            std::string msg = issue.get<std::string>();
            if (msg.find("exceeded max consecutive limit") != std::string::npos &&
                msg.find("Stints 1-3") != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected diagnosis to contain detailed consecutive warning.";
    }
}
