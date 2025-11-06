/**
 * @file main.cpp
 * @brief An example CLI client for the JRES Solver Library.
 *
 * This application is a simple wrapper around the `racesolver` library.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <sstream>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp" 

#include "racesolver.hpp"

using json = nlohmann::json;

/**
 * @brief Main entry point for the CLI client.
 */
int main(int argc, char **argv)
{
    // --- 1. Parse Command-Line Arguments ---
    cxxopts::Options options("solver", "JRES endurance race solver.");
    options.add_options()("i,input", "Path to the race data .json file. Reads from stdin if not provided.", cxxopts::value<std::string>())("o,output", "Optional. Path to save the schedule as a JSON file.", cxxopts::value<std::string>())("t,time-limit", "Maximum time in seconds to let the solver run.", cxxopts::value<int>()->default_value("30"))("q,quiet", "Suppress INFO logs and final schedule print-out.", cxxopts::value<bool>()->default_value("false"))("s,spotter-mode", "Method for scheduling spotters (none, integrated, sequential).", cxxopts::value<std::string>()->default_value("none"))("allow-no-spotter", "Allow stints to have no spotter assigned.", cxxopts::value<bool>()->default_value("false"))("g,optimality-gap", "Solver stops when the gap to optimal is less than this (e.g., 0.01 for 1%).", cxxopts::value<double>()->default_value("0.0"))("h,help", "Print usage.");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool quiet = result["quiet"].as<bool>();

    // Load Input JSON Data into a std::string
    std::string raceDataJsonString;
    try
    {
        if (result.count("input"))
        {
            std::string inputPath = result["input"].as<std::string>();
            if (!quiet)
                std::cout << "[App] Loading data from file: " << inputPath << std::endl;
            std::ifstream f(inputPath);
            if (!f.is_open())
            {
                throw std::runtime_error("Could not open input file: " + inputPath);
            }
            std::stringstream buffer;
            buffer << f.rdbuf();
            raceDataJsonString = buffer.str();
        }
        else
        {
            if (!quiet)
                std::cout << "[App] Loading data from stdin..." << std::endl;
            std::stringstream buffer;
            buffer << std::cin.rdbuf();
            raceDataJsonString = buffer.str();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[App] Error: " << e.what() << std::endl;
        return 1;
    }

    // Build Solver Options Struct
    RaceSolverOptions solverOptions;
    solverOptions.quiet = quiet;
    solverOptions.timeLimit = result["time-limit"].as<int>();
    // Need to get c_str() for the const char*
    std::string spotterModeStr = result["spotter-mode"].as<std::string>();
    solverOptions.spotterMode = spotterModeStr.c_str();
    solverOptions.allowNoSpotter = result["allow-no-spotter"].as<bool>();
    solverOptions.optimalityGap = result["optimality-gap"].as<double>();

    // Call the Solver Library
    char* resultJsonCStr = nullptr;
    int resultCode = solve_race_schedule(raceDataJsonString.c_str(), solverOptions, &resultJsonCStr);

    // Process Results
    if (resultJsonCStr == nullptr) {
        std::cerr << "[App] Critical Error: Solver returned no data." << std::endl;
        return 1;
    }

    std::string resultJsonString(resultJsonCStr);
    std::string outputPath = result.count("output") ? result["output"].as<std::string>() : "";
    
    try {
        json resultJson = json::parse(resultJsonString);

        if (resultCode == 0) {
            // Success
            if (!quiet) {
                // Print the schedule
                std::cout << "\n--- 🏁 Race Schedule ---" << std::endl;
                bool hasSpotters = (solverOptions.spotterMode != "none");
                for (const auto& entry : resultJson["schedule"]) {
                    std::stringstream ss;
                    ss << "Stint " << std::setw(3) << entry["stint"].get<int>()
                       << ": Driver: " << std::setw(15) << std::left << entry["driver"].get<std::string>();
                    
                    if (hasSpotters) {
                        ss << " | Spotter: " << std::setw(15) << std::left << entry["spotter"].get<std::string>();
                    }
                    std::cout << ss.str() << std::endl;
                }
            }
            // Save to file (if requested)
            if (!outputPath.empty()) {
                std::ofstream o(outputPath);
                o << resultJsonString << std::endl;
                if (!quiet) {
                    std::cout << "\n[App] Schedule saved to " << outputPath << std::endl;
                }
            }
        } else {
            // Failure
            std::cerr << "[Solver] Error: " << resultJson["error"].get<std::string>() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "[App] Error processing solver result: " << e.what() << std::endl;
        std::cerr << "Raw output: " << resultJsonString << std::endl;
    }


    // Clean up
    free_solver_result(resultJsonCStr);

    if (!quiet)
    {
        std::cout << "[App] Solver finished." << std::endl;
    }

    return (resultCode == 0) ? 0 : 1;
}
