/**
 * @author popmonkey+jres@gmail.com
 * @file cmd/formatter/cli.cpp
 * @brief Command-line interface for JRES Schedule Formatter
 */
#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <string>

#include "formatter/formatter_core.hpp"

#include "version.h"

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    cxxopts::Options options("formatter", "JRES Schedule Formatter");

    options.add_options()
        ("i,input", "Input JSON file (solved schedule)", cxxopts::value<std::string>())
        ("o,output", "Output file path", cxxopts::value<std::string>())
        ("f,format", "Output format (zip, csv, txt)", cxxopts::value<std::string>()->default_value("zip"))
        ("v,version", "Print version information and exit.")
        ("h,help", "Print usage")
    ;

    try {
        auto result = options.parse(argc, argv);

        // --- Check for Version Flag ---
        if (result.count("version"))
        {
            std::cout << "JRES Solver Version: " << JRES_VERSION_STRING << std::endl;
            return 0;
        }

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        std::cout << "[App] JRES Solver " << JRES_VERSION_STRING << std::endl;

        if (!result.count("input")) {
            std::cerr << "Error: Input file is required (-i)" << std::endl;
            return 1;
        }

        if (!result.count("output")) {
            std::cerr << "Error: Output file is required (-o)" << std::endl;
            return 1;
        }

        std::string input_path = result["input"].as<std::string>();
        std::string output_path = result["output"].as<std::string>();
        std::string format = result["format"].as<std::string>();

        std::ifstream f(input_path);
        if (!f.is_open()) {
            std::cerr << "Error: Could not open input file: " << input_path << std::endl;
            return 1;
        }

        json solved_data;
        try {
            f >> solved_data;
        } catch (const json::parse_error& e) {
            std::cerr << "Error: Failed to parse JSON: " << e.what() << std::endl;
            return 1;
        }

        if (!solved_data.contains("schedule") || !solved_data.contains("raceData")) {
            std::cerr << "Error: Invalid JSON. Expected keys 'schedule' and 'raceData'." << std::endl;
            return 1;
        }

        // Call into our library
        jres::write_output(solved_data, output_path, format);
        std::cout << "Successfully generated " << format << " output: " << output_path << std::endl;

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
