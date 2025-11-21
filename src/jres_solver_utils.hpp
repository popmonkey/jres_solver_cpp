#pragma once

#include "jres_solver_types.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>

// --- Helper Structures ---

// Tracks variable indices for a specific participant in the CBC model
struct ParticipantModel
{
    std::string prefix;
    std::map<std::pair<std::string, int>, int> workVars;
    std::map<std::pair<std::string, int>, int> switchVars;
    int maxWorkStintsVar = -1;
    int minWorkStintsVar = -1;

    ParticipantModel(std::string p) : prefix(p) {}
};

// --- Time Helpers ---
namespace TimeHelpers
{
    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string);
    std::string timePointToString(std::chrono::system_clock::time_point tp);
    std::string timePointToKey(std::chrono::system_clock::time_point tp);
}

// --- Base Solver Class ---
/**
 * @brief Shared logic for both Standard and Diagnostic solvers.
 * Handles configuration, math, and participant filtering.
 */
class JresSolverBase
{
public:
    JresSolverBase(const SolverContext& ctx);
    virtual ~JresSolverBase() = default;

protected:
    SolverContext m_ctx;
    
    // Calculated Race Parameters
    double m_stintWithPitSeconds;
    int m_totalStints;
    int m_stintLaps;

    // Filtered Participant Pools
    std::vector<TeamMember> m_driverPool;
    std::vector<TeamMember> m_spotterPool;
};
