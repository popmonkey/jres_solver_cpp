/**
 * @file test/test_formatter_summary.cpp
 * @brief Tests for summary report generation.
 */

#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include <map>
#include <string>

TEST(FormatterSummaryTest, BasicStats) {
    std::map<std::string, int> driver_stats;
    driver_stats["DriverA"] = 2; // 2 stints

    std::map<std::string, int> spotter_stats;
    spotter_stats["SpotterB"] = 3;

    std::string result = jres::generate_summary_csv_string(driver_stats, spotter_stats, true);

    EXPECT_NE(result.find("Driver,DriverA,2"), std::string::npos);
    EXPECT_NE(result.find("Spotter,SpotterB,3"), std::string::npos);
    EXPECT_EQ(result.find("Total Laps"), std::string::npos); // Make sure "Laps" is gone
}
