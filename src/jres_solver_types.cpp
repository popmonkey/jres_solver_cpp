/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver_types.cpp
 * @brief Data types and JSON serialization/deserialization for JRES endurance race scheduling.
 */

 #include "jres_solver_types.hpp"

// --- JSON Deserialization ---
void from_json(const json &j, TeamMember &member)
{
    j.at("name").get_to(member.name);
    member.isDriver = j.value("isDriver", false);
    member.isSpotter = j.value("isSpotter", false);
    member.preferredStints = j.value("preferredStints", 3);
    member.minimumRestHours = j.value("minimumRestHours", 0);
}

void from_json(const json &j, RaceData &data)
{
    j.at("avgLapTimeInSeconds").get_to(data.avgLapTimeInSeconds);
    j.at("pitTimeInSeconds").get_to(data.pitTimeInSeconds);
    j.at("fuelTankSize").get_to(data.fuelTankSize);
    j.at("fuelUsePerLap").get_to(data.fuelUsePerLap);
    j.at("durationHours").get_to(data.durationHours);
    j.at("raceStartUTC").get_to(data.raceStartUTC);
    j.at("teamMembers").get_to(data.teamMembers);
    j.at("availability").get_to(data.availability);
    data.firstStintDriver = j.value("firstStintDriver", "");
}

// --- JSON Serialization ---
void to_json(json &j, const TeamMember &member)
{
    j = json{
        {"name", member.name},
        {"isDriver", member.isDriver},
        {"isSpotter", member.isSpotter},
        {"preferredStints", member.preferredStints},
        {"minimumRestHours", member.minimumRestHours}};
}

void to_json(json &j, const RaceData &data)
{
    j = json{
        {"avgLapTimeInSeconds", data.avgLapTimeInSeconds},
        {"pitTimeInSeconds", data.pitTimeInSeconds},
        {"fuelTankSize", data.fuelTankSize},
        {"fuelUsePerLap", data.fuelUsePerLap},
        {"durationHours", data.durationHours},
        {"raceStartUTC", data.raceStartUTC},
        {"teamMembers", data.teamMembers},
        {"availability", data.availability},
        {"firstStintDriver", data.firstStintDriver}};
}

void to_json(json &j, const SolverContext &ctx)
{
    // We intentionally do NOT include 'raceData' here to avoid 
    // duplication in the final output, as this is used for the "metadata" block.
    j = json{
        {"quiet", ctx.quiet},
        {"timeLimit", ctx.timeLimit},
        {"spotterMode", ctx.spotterMode}, // Serializes to string via enum mapping
        {"allowNoSpotter", ctx.allowNoSpotter},
        {"optimalityGap", ctx.optimalityGap}
    };
}

void to_json(json &j, const ComplexityMetrics &metrics)
{
    j = json{
        {"modelColumns", metrics.modelColumns},
        {"modelRows", metrics.modelRows},
        {"numRestConstraints", metrics.numRestConstraints},
        {"searchNodes", metrics.searchNodes},
        {"finalGap", metrics.finalGap}
    };
}
