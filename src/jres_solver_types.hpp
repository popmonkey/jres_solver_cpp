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

struct SolverContext
{
    bool quiet;
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

// --- JSON Deserialization Declarations ---
void from_json(const json &j, TeamMember &member);
void from_json(const json &j, RaceData &data);

// --- JSON Serialization Declarations ---
void to_json(json &j, const TeamMember &member);
void to_json(json &j, const RaceData &data);
