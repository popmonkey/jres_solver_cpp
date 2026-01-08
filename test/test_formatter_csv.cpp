/**
 * @file test/test_formatter_csv.cpp
 * @brief Tests for CSV output formatting.
 */

#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include "nlohmann/json.hpp"
#include <string>

using json = nlohmann::json;

TEST(FormatterCSVTest, ScheduleGeneration) {
    // Create Mock Data
    std::vector<json> schedule;
    schedule.push_back({
        {"id", 1},
        {"startTime", "2023-01-01T12:00:00Z"},
        {"endTime", "2023-01-01T13:00:00Z"},
        {"driver", "DriverA"},
        {"spotter", "SpotterB"}
    });

    // Run Function
    std::string result = jres::generate_schedule_csv_string(schedule, true);

    // Assertions
    EXPECT_NE(result.find("Stint,Start Time (UTC),End Time (UTC)"), std::string::npos) << "Header is incorrect";
    EXPECT_NE(result.find("Assigned Spotter"), std::string::npos) << "Spotter column missing";
    EXPECT_EQ(result.find("Laps"), std::string::npos) << "Laps column should not exist";
    
    EXPECT_NE(result.find("1,2023-01-01T12:00:00Z,2023-01-01T13:00:00Z,DriverA,SpotterB"), std::string::npos) << "CSV data row is incorrect";
}
