/**
 * @file cli.cpp
 * @brief An example CLI client for the JRES Solver Library.
 *
 * This application is an example `JresSolver` library client.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>

#include "cxxopts.hpp"
#include "nlohmann/json.hpp" 

#include "jres_solver/jres_solver.hpp"

using json = nlohmann::json;

/**
 * @brief Main entry point for the CLI client.
 */
int main(int argc, char **argv)
{
    // --- 1. Parse Command-Line Arguments ---
    cxxopts::Options options("solver", "JRES endurance race solver.");
    options.add_options()
        ("i,input", "Path to the race data .json file. Reads from stdin if not provided.", cxxopts::value<std::string>())
        ("o,output", "Optional. Path to save the schedule as a JSON file.", cxxopts::value<std::string>())
        ("t,time-limit", "Maximum time in seconds to let the solver run.", cxxopts::value<int>()->default_value("30"))
        ("q,quiet", "Suppress INFO logs and final schedule print-out.", cxxopts::value<bool>()->default_value("false"))
        ("s,spotter-mode", "Method for scheduling spotters (none, integrated, sequential).", cxxopts::value<std::string>()->default_value("none"))
        ("allow-no-spotter", "Allow stints to have no spotter assigned.", cxxopts::value<bool>()->default_value("false"))
        ("g,optimality-gap", "Solver stops when the gap to optimal is less than this (e.g., 0.01 for 1%).", cxxopts::value<double>()->default_value("0.0"))
        ("d,diagnose", "Run diagnostics to explain why a schedule is infeasible.", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage.");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool quiet = result["quiet"].as<bool>();
    bool runDiagnostics = result["diagnose"].as<bool>();

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
    JresSolverOptions solverOptions;
    solverOptions.quiet = quiet;
    solverOptions.timeLimit = result["time-limit"].as<int>();
    
    // --- Translate spotter-mode string to enum ---
    std::string spotterModeStr = result["spotter-mode"].as<std::string>();
    if (spotterModeStr == "none") {
        solverOptions.spotterMode = JRES_SPOTTER_MODE_NONE;
    } else if (spotterModeStr == "integrated") {
        solverOptions.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
    } else if (spotterModeStr == "sequential") {
        solverOptions.spotterMode = JRES_SPOTTER_MODE_SEQUENTIAL;
    } else {
        std::cerr << "[App] Error: Invalid spotter mode '" << spotterModeStr << "'. Must be 'none', 'integrated', or 'sequential'." << std::endl;
        return 1;
    }
    // --- End translation ---

    solverOptions.allowNoSpotter = result["allow-no-spotter"].as<bool>();
    solverOptions.optimalityGap = result["optimality-gap"].as<double>();

    // Call the Solver Library
    char* resultJsonCStr = nullptr;
    int resultCode = 0;

    if (runDiagnostics) {
        if (!quiet) std::cout << "[App] Running in DIAGNOSTIC mode..." << std::endl;
        resultCode = diagnose_race_schedule(raceDataJsonString.c_str(), solverOptions, &resultJsonCStr);
    } else {
        resultCode = solve_race_schedule(raceDataJsonString.c_str(), solverOptions, &resultJsonCStr);
    }

    // Process Results
    if (resultJsonCStr == nullptr) {
        std::cerr << "[App] Critical Error: Solver returned no data." << std::endl;
        return 1;
    }

    std::string resultJsonString(resultJsonCStr);
    std::string outputPath = result.count("output") ? result["output"].as<std::string>() : "";
    
    try {
        json resultJson = json::parse(resultJsonString);

        if (runDiagnostics) {
            // --- Diagnostic Output Handling ---
            if (resultCode == 0) {
                // Diagnosis ran successfully (even if constraints were violated)
                if (!quiet) {
                    std::cout << "\n--- ⚠️  Infeasibility Diagnosis ---" << std::endl;
                    if (resultJson.contains("diagnosis")) {
                        auto issues = resultJson["diagnosis"];
                        if (issues.empty()) {
                            std::cout << "The diagnostic solver found a solution, but no specific constraints were identified as the cause. This implies the schedule might actually be feasible in a relaxed context." << std::endl;
                        } else {
                            std::cout << "The solver identified the following blockers:" << std::endl;
                            for (const auto& issue : issues) {
                                std::cout << " - " << issue.get<std::string>() << std::endl;
                            }
                        }
                    } else {
                        std::cout << "Diagnosis completed but no 'diagnosis' key found in result." << std::endl;
                    }
                }
            } else {
                 // Engine failure
                 std::cerr << "[Solver] Diagnostic Engine Error: " << resultJson.value("error", "Unknown error") << std::endl;
            }
        } 
        else {
            // --- Standard Schedule Output Handling ---
            if (resultCode == 0) {
                // Success
                if (!quiet) {
                    // Print the schedule
                    std::cout << "\n--- 🏁 Race Schedule ---" << std::endl;
                    bool hasSpotters = (solverOptions.spotterMode != JRES_SPOTTER_MODE_NONE);
                    if (resultJson.contains("schedule")) {
                        for (const auto& entry : resultJson["schedule"]) {
                            std::stringstream ss;
                            ss << "Stint " << std::setw(3) << entry["stint"].get<int>()
                               << ": Driver: " << std::setw(15) << std::left << entry["driver"].get<std::string>();
                            
                            if (hasSpotters && entry.contains("spotter")) {
                                ss << " | Spotter: " << std::setw(15) << std::left << entry["spotter"].get<std::string>();
                            }
                            std::cout << ss.str() << std::endl;
                        }
                    }
                }
            } else {
                // Failure
                std::cerr << "[Solver] Error: " << resultJson.value("error", "Unknown error") << std::endl;
                if (!quiet) {
                    std::cout << "\nTip: Try running with --diagnose to find out why." << std::endl;
                }
            }
        }

        // Save to file (Common for both modes)
        if (!outputPath.empty()) {
            std::ofstream o(outputPath);
            o << resultJsonString << std::endl;
            if (!quiet) {
                std::cout << "\n[App] Result saved to " << outputPath << std::endl;
            }
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
