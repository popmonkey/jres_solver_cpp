#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include "nlohmann/json.hpp"
#include <vector>

using json = nlohmann::json;

TEST(FormatterItineraryTest, ConsolidationAndTimezones) {
    // Mock Data: Race starts at 12:00 UTC
    // DriverA does Stint 1 (12:00-13:00) and Stint 2 (13:05-14:05) (5 min pit).
    // DriverA is in Timezone +2.
    json race_data = {
        {"raceStartUTC", "2023-01-01 12:00:00"},
        {"durationHours", 6},
        {"pitTimeInSeconds", 300}, // 5 mins
        {"teamMembers", {
            { {"name", "DriverA"}, {"isDriver", true}, {"timezone", 2} }
        }}
    };

    std::vector<json> schedule;
    // Stint 1
    schedule.push_back({
        {"stint", 1},
        {"startTimeUTC", "2023-01-01 12:00:00"},
        {"endTimeUTC", "2023-01-01 13:00:00"},
        {"driver", "DriverA"},
        {"spotter", "N/A"}
    });
    // Stint 2 (Starts 5 mins later due to pit)
    schedule.push_back({
        {"stint", 2},
        {"startTimeUTC", "2023-01-01 13:05:00"},
        {"endTimeUTC", "2023-01-01 14:05:00"},
        {"driver", "DriverA"},
        {"spotter", "N/A"}
    });

    auto itineraries = jres::generate_member_itineraries(schedule, race_data, 300, false);

    // Assertions for DriverA
    ASSERT_TRUE(itineraries.count("DriverA"));
    const auto& items = itineraries["DriverA"];
    
    // Expecting: 1. Consolidated Driving, 2. Resting
    ASSERT_GE(items.size(), 1);
    
    // Check Item 1: Driving
    const auto& drive_block = items[0];
    
    // Check logic that consolidates adjacent stints
    // Note: "Driving Stints #1" might be followed by "-2" depending on exact formatting implementation
    EXPECT_NE(drive_block.activity.find("Driving Stints"), std::string::npos);
    
    // Check Timezone math: 12:00 UTC + 2h = 14:00 Local
    EXPECT_EQ(drive_block.start_local.time_string(), "14:00");
    // End: 14:05 UTC + 2h = 16:05 Local
    EXPECT_EQ(drive_block.end_local.time_string(), "16:05");

    // Check Item 2: Resting (if present in logic)
    if (items.size() > 1) {
        const auto& rest_block = items[1];
        EXPECT_EQ(rest_block.activity, "Resting");
        EXPECT_EQ(rest_block.start_local.time_string(), "16:05");
    }
}