#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include "nlohmann/json.hpp"
#include <string>

using json = nlohmann::json;

TEST(FormatterCSVTest, ScheduleGeneration) {
    // 1. Create Mock Data
    std::vector<json> schedule;
    schedule.push_back({
        {"stint", 1},
        {"startTimeUTC", "2023-01-01 12:00:00"},
        {"endTimeUTC", "2023-01-01 13:00:00"},
        {"driver", "DriverA"},
        {"spotter", "SpotterB"},
        {"laps", 20}
    });

    // 2. Run Function
    std::string result = jres::generate_schedule_csv_string(schedule, true);

    // 3. Assertions
    EXPECT_NE(result.find("Stint,Start Time (UTC)"), std::string::npos) << "Header missing";
    EXPECT_NE(result.find("Assigned Spotter"), std::string::npos) << "Spotter column missing";
    
    EXPECT_NE(result.find("DriverA"), std::string::npos) << "Driver data missing";
    EXPECT_NE(result.find("SpotterB"), std::string::npos) << "Spotter data missing";
    EXPECT_NE(result.find("20"), std::string::npos) << "Laps data missing";
}
