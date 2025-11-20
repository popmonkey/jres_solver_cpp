#include "gtest/gtest.h"
#include "formatter/formatter_core.hpp"
#include <map>
#include <string>

TEST(FormatterSummaryTest, BasicStats) {
    std::map<std::string, std::pair<int, int>> driver_stats;
    driver_stats["DriverA"] = {2, 40}; // 2 stints, 40 laps

    std::map<std::string, int> spotter_stats;
    spotter_stats["SpotterB"] = 3;

    std::string result = jres::generate_summary_csv_string(driver_stats, spotter_stats, true);

    EXPECT_NE(result.find("Driver,DriverA,2,40"), std::string::npos);
    EXPECT_NE(result.find("Spotter,SpotterB,3,-"), std::string::npos);
}
