/**
 * @author popmonkey+jres@gmail.com
 * @file test/test_formatter_itinerary.cpp
 * @brief Tests for itinerary generation logic.
 */

#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include "nlohmann/json.hpp"
#include <vector>

using json = nlohmann::json;

TEST(FormatterItineraryTest, ConsolidationAndTimezones) {
    // Mock Data: DriverA does Stint 1 (12:00-13:00 UTC) and Stint 2 (13:05-14:05 UTC).
    // DriverA is in Timezone +2 (tzOffset is in hours).
    json solved_data = {
        {"teamMembers", {{
            {"name", "DriverA"}, {"isDriver", true}, {"tzOffset", 2} 
        }}},
        {"schedule", json::array({
            {
                {"id", 1},
                {"startTime", "2023-01-01T12:00:00Z"},
                {"endTime", "2023-01-01T13:00:00Z"},
                {"driver", "DriverA"},
                {"spotter", "N/A"}
            },
            {
                {"id", 2},
                {"startTime", "2023-01-01T13:05:00Z"},
                {"endTime", "2023-01-01T14:05:00Z"},
                {"driver", "DriverA"},
                {"spotter", "N/A"}
            }
        })}
    };

    std::vector<json> schedule_vec;
    for(const auto& s : solved_data["schedule"]) {
        schedule_vec.push_back(s);
    }

    auto itineraries = jres::generate_member_itineraries(schedule_vec, solved_data, false);

    // Assertions for DriverA
    ASSERT_TRUE(itineraries.count("DriverA"));
    const auto& itinerary = itineraries.at("DriverA");
    
    // Check timezone offset
    EXPECT_EQ(itinerary.tz_offset, 2);

    // Expecting: Driving, Resting, Driving
    ASSERT_EQ(itinerary.items.size(), 3);
    
    // Check Item: Driving Stint #1
    const auto& drive_block1 = itinerary.items[0];
    EXPECT_EQ(drive_block1.activity, "Driving Stint #1");
    EXPECT_EQ(drive_block1.start_local.to_string(), "2023-01-01 14:00:00"); // 12:00 UTC + 2h
    EXPECT_EQ(drive_block1.end_local.to_string(), "2023-01-01 15:00:00");   // 13:00 UTC + 2h

    // Check Item: Resting block between stints
    const auto& rest_block = itinerary.items[1];
    EXPECT_EQ(rest_block.activity, "Resting");
    EXPECT_EQ(rest_block.start_local.to_string(), "2023-01-01 15:00:00");
    EXPECT_EQ(rest_block.end_local.to_string(), "2023-01-01 15:05:00"); // 13:05 UTC + 2h

    // Check Item: Driving Stint #2
    const auto& drive_block2 = itinerary.items[2];
    EXPECT_EQ(drive_block2.activity, "Driving Stint #2");
    EXPECT_EQ(drive_block2.start_local.to_string(), "2023-01-01 15:05:00");
    EXPECT_EQ(drive_block2.end_local.to_string(), "2023-01-01 16:05:00");   // 14:05 UTC + 2h
}

TEST(FormatterItineraryTest, NegativeTimezone) {
    // Mock Data: Driver with timezone -5
    json solved_data = {
        {"teamMembers", {{
            {"name", "DriverB"}, {"isDriver", true}, {"tzOffset", -5}
        }}},
        {"schedule", json::array({
            {
                {"id", 1},
                {"startTime", "2023-01-01T02:00:00Z"},
                {"endTime", "2023-01-01T03:00:00Z"},
                {"driver", "DriverB"},
                {"spotter", "N/A"}
            }
        })}
    };

    std::vector<json> schedule_vec;
    for(const auto& s : solved_data["schedule"]) {
        schedule_vec.push_back(s);
    }

    auto itineraries = jres::generate_member_itineraries(schedule_vec, solved_data, false);

    // Assertions for DriverB
    ASSERT_TRUE(itineraries.count("DriverB"));
    const auto& itinerary = itineraries.at("DriverB");

    // Check timezone offset
    EXPECT_EQ(itinerary.tz_offset, -5);

    ASSERT_EQ(itinerary.items.size(), 1);

    // Check Item: Driving Stint #1
    const auto& drive_block1 = itinerary.items[0];
    EXPECT_EQ(drive_block1.activity, "Driving Stint #1");
    EXPECT_EQ(drive_block1.start_local.to_string(), "2022-12-31 21:00:00"); // 02:00 UTC - 5h
    EXPECT_EQ(drive_block1.end_local.to_string(), "2022-12-31 22:00:00");   // 03:00 UTC - 5h
}