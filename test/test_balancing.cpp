#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <numeric>
#include <cmath>
#include <map>

// Use the nlohmann::json namespace
using json = nlohmann::json;

double calculate_stddev(const std::vector<int>& values) {
    if (values.size() < 2) {
        return 0.0;
    }

    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();

    double sq_sum = std::inner_product(values.begin(), values.end(), values.begin(), 0.0);
    double stddev = std::sqrt(sq_sum / values.size() - mean * mean);

    return stddev;
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

TEST(BalancingTest, FairBalance) {
    std::string data_dir = TOSTRING(TEST_DATA_DIR);
    data_dir.erase(std::remove(data_dir.begin(), data_dir.end(), '\"'), data_dir.end());
    std::ifstream f(data_dir + "/24h_race.json");
    ASSERT_TRUE(f.good());
    json data = json::parse(f);
    std::string json_str = data.dump();

    JresSolverOptions options;
    options.timeLimit = 10;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = false;
    options.optimalityGap = 0.0;

    JresSolverInput* input = jres_input_from_json(json_str.c_str());
    ASSERT_NE(input, nullptr);

    JresSolverOutput* output = solve_race_schedule(input, &options);
    ASSERT_NE(output, nullptr);
    ASSERT_GT(output->schedule_len, 0);

    std::map<std::string, int> stints_per_driver;
    for (int i = 0; i < output->schedule_len; ++i) {
        stints_per_driver[output->schedule[i].driver]++;
    }

    std::vector<int> stint_counts;
    for (const auto& pair : stints_per_driver) {
        stint_counts.push_back(pair.second);
    }

    double stddev = calculate_stddev(stint_counts);
    EXPECT_LT(stddev, 2.0);

    free_jres_solver_input(input);
    free_jres_solver_output(output);
}
