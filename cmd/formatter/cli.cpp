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
#include <algorithm>

#include "formatter/formatter_core.hpp"
#include "version.h"

using json = nlohmann::json;

// Helper to lowercase a string
std::string to_lower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

// Helper to get extension
std::string get_extension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos) return "";
    return to_lower(filename.substr(pos));
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("formatter", "JRES Schedule Formatter");

    options.add_options()
        ("i,input", "Input JSON file (solved schedule)", cxxopts::value<std::string>())
        ("o,output", "Output file path", cxxopts::value<std::string>())
        ("f,format", "Output format (zip, csv, txt). Auto-detected from output filename if omitted.", cxxopts::value<std::string>()) 
        ("v,version", "Print version information and exit.")
        ("h,help", "Print usage")
    ;

    try {
        auto result = options.parse(argc, argv);

        if (result.count("version")) {
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
        std::string format;

        // --- Logic: Determine Format ---
        if (result.count("format")) {
            // 1. Explicitly provided by user
            format = result["format"].as<std::string>();
        } else {
            // 2. Auto-detect from extension
            std::string ext = get_extension(output_path);
            if (ext == ".csv") {
                format = "csv";
            } else if (ext == ".txt") {
                format = "txt";
            } else if (ext == ".zip") {
                format = "zip";
            } else {
                // 3. Unknown extension and no flag -> Error
                std::cerr << "Error: Could not determine output format from filename extension (" 
                          << ext << ")." << std::endl;
                std::cerr << "Please explicitly specify format using -f <zip|csv|txt> or use a standard file extension." << std::endl;
                return 1;
            }
            std::cout << "Auto-detected format: " << format << std::endl;
        }

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
