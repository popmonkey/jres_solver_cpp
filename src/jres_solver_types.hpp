/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver_types.hpp
 * @brief Data types and JSON serialization/deserialization for JRES endurance race scheduling.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include "nlohmann/json.hpp"

// Use the nlohmann::json namespace
using json = nlohmann::json;

// --- Data Structures ---

struct TeamMember
{
    std::string name;
    bool isDriver = false;
    bool isSpotter = false;
    int preferredStints = 3;
    int minimumRestHours = 0;
};

struct RaceData
{
    double avgLapTimeInSeconds;
    double pitTimeInSeconds;
    double fuelTankSize;
    double fuelUsePerLap;
    double durationHours;
    std::string raceStartUTC;
    std::string firstStintDriver;
    std::vector<TeamMember> teamMembers;
    json availability;
};

/**
 * @brief Internal C++ enum for spotter scheduling mode.
 */
enum class SpotterMode {
    None,
    Integrated,
    Sequential
};

// Map Enum to Strings for JSON
NLOHMANN_JSON_SERIALIZE_ENUM( SpotterMode, {
    {SpotterMode::None, "none"},
    {SpotterMode::Integrated, "integrated"},
    {SpotterMode::Sequential, "sequential"}
})

struct SolverContext
{
    int timeLimit;
    SpotterMode spotterMode;
    bool allowNoSpotter;
    double optimalityGap;
    RaceData raceData;
};

struct ScheduleEntry {
    int stint;
    std::string driver;
    std::string spotter;
    std::string startTimeUTC;
    std::string endTimeUTC;
    int laps;
};

struct ComplexityMetrics {
    int modelColumns = 0;
    int modelRows = 0;
    int numRestConstraints = 0; // The "Big-M" metric
    int searchNodes = 0;
    double finalGap = 0.0;
};

/**
 * @brief Custom exception that carries solver metadata for better error reporting.
 */
class SolverException : public std::runtime_error {
public:
    json metadata;
    
    SolverException(const std::string& message, const json& meta = json::object())
        : std::runtime_error(message), metadata(meta) {}
};

// --- JSON Deserialization Declarations ---
void from_json(const json &j, TeamMember &member);
void from_json(const json &j, RaceData &data);

// --- JSON Serialization Declarations ---
void to_json(json &j, const TeamMember &member);
void to_json(json &j, const RaceData &data);
void to_json(json &j, const SolverContext &ctx);
void to_json(json &j, const ComplexityMetrics &metrics);
