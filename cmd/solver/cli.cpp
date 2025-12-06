/**
 * @author popmonkey+jres@gmail.com
 * @file cmd/solver/cli.cpp
 * @brief Command-line interface for the JRES Solver library.
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

#include "version.h"

using json = nlohmann::json;

/**
 * @brief Main entry point for the CLI client.
 */
int main(int argc, char **argv)
{
    // --- Parse Command-Line Arguments ---
    cxxopts::Options options("solver", "JRES endurance race solver.");
    options.add_options()
        ("i,input", "Path to the race data .json file. Reads from stdin if not provided.", cxxopts::value<std::string>())
        ("o,output", "Optional. Path to save the schedule as a JSON file.", cxxopts::value<std::string>())
        ("t,time-limit", "Maximum time in seconds to let the solver run.", cxxopts::value<int>()->default_value("5"))
        ("q,quiet", "Suppress INFO logs and final schedule print-out.", cxxopts::value<bool>()->default_value("false"))
        ("s,spotter-mode", "Method for scheduling spotters (none, integrated, sequential).", cxxopts::value<std::string>()->default_value("none"))
        ("allow-no-spotter", "Allow stints to have no spotter assigned.", cxxopts::value<bool>()->default_value("false"))
        ("g,optimality-gap", "Solver stops when the gap to optimal is less than this (e.g., 0.2 for 20%).", cxxopts::value<double>()->default_value("0.2"))
        ("d,diagnose", "Run diagnostics to explain why a schedule is infeasible.", cxxopts::value<bool>()->default_value("false"))
        ("v,version", "Print version information and exit.")
        ("h,help", "Print usage.");

    auto result = options.parse(argc, argv);

    if (result.count("version"))
    {
        std::cout << "JRES Solver Version: " << JRES_VERSION_STRING << std::endl;
        return 0;
    }

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool quiet = result["quiet"].as<bool>();
    bool runDiagnostics = result["diagnose"].as<bool>();

    if (!quiet) {
        std::cout << "[App] JRES Solver " << JRES_VERSION_STRING << std::endl;
    }

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
    JresSolverInput* solverInput = jres_input_from_json(raceDataJsonString.c_str());
    if (solverInput == nullptr) {
        std::cerr << "[App] Critical Error: Could not parse input JSON." << std::endl;
        return 1;
    }

    JresSolverOutput* solverOutput = nullptr;
    if (runDiagnostics) {
        if (!quiet) std::cout << "[App] Running in DIAGNOSTIC mode..." << std::endl;
        solverOutput = diagnose_race_schedule(solverInput, &solverOptions);
    } else {
        solverOutput = solve_race_schedule(solverInput, &solverOptions);
    }

    if (solverOutput == nullptr) {
        std::cerr << "[App] Critical Error: Solver returned no data." << std::endl;
        free_jres_solver_input(solverInput);
        return 1;
    }

    if (solverOutput->diagnosis_len > 0) {
        if (runDiagnostics) {
             if (!quiet) {
                std::cout << "\n--- Infeasibility Diagnosis ---" << std::endl;
                for (int i = 0; i < solverOutput->diagnosis_len; ++i) {
                    std::cout << " - " << solverOutput->diagnosis[i] << std::endl;
                }
            }
        } else {
            std::cerr << "[Solver] Error: Model is infeasible." << std::endl;
            if (!quiet) {
                std::cerr << "\nTip: Try running with --diagnose to find out why." << std::endl;
            }
        }
    } else if (!quiet) {
        if (solverOutput->stats) {
            std::cout << "\n--- Complexity ---" << std::endl;
            std::cout << "Rows: " << solverOutput->stats->modelRows << " | "
                        << "Cols: " << solverOutput->stats->modelColumns << " | "
                        << "Nodes: " << solverOutput->stats->searchNodes << std::endl;
            if (solverOutput->stats->finalGap > 0) {
                std::cout << "Final Gap: " << solverOutput->stats->finalGap << std::endl;
            }

            std::cout << "\n--- Timing Performance ---" << std::endl;
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "Setup/Model Build : " << std::setw(8) << solverOutput->stats->setupDurationMs << " ms" << std::endl;
            std::cout << "Driver Solve      : " << std::setw(8) << solverOutput->stats->driverSolveDurationMs << " ms" << std::endl;
            if (solverOutput->stats->spotterSolveDurationMs > 0) {
                std::cout << "Spotter Solve     : " << std::setw(8) << solverOutput->stats->spotterSolveDurationMs << " ms" << std::endl;
            }
        }
        // Print the schedule
        std::cout << "\n--- Race Schedule ---" << std::endl;
        bool hasSpotters = (solverOptions.spotterMode != JRES_SPOTTER_MODE_NONE);
        if (solverOutput->schedule_len > 0) {
            for (int i = 0; i < solverOutput->schedule_len; ++i) {
                std::stringstream ss;
                ss << "Stint " << std::setw(3) << solverOutput->schedule[i].stintId
                    << ": Driver: " << std::setw(15) << std::left << solverOutput->schedule[i].driver;
                
                if (hasSpotters) {
                    ss << " | Spotter: " << std::setw(15) << std::left << solverOutput->schedule[i].spotter;
                }
                std::cout << ss.str() << std::endl;
            }
        }
    }

    char* resultJsonCStr = jres_output_to_json(solverOutput);
    std::string outputPath = result.count("output") ? result["output"].as<std::string>() : "";

    if (!outputPath.empty()) {
        std::ofstream o(outputPath);
        o << resultJsonCStr << std::endl;
        if (!quiet) {
            std::cout << "\n[App] Result saved to " << outputPath << std::endl;
        }
    }
    
    // Clean up
    int returnCode = solverOutput->diagnosis_len > 0 ? 1 : 0;
    free_jres_solver_input(solverInput);
    free_jres_solver_output(solverOutput);
    free_json_string(resultJsonCStr);

    if (!quiet)
    {
        std::cout << "[App] Solver finished." << std::endl;
    }

    return returnCode;
}