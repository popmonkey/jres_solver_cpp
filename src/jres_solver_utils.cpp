#include "jres_solver_utils.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <ctime>

// --- Time Helpers Implementation ---

namespace TimeHelpers
{
    // Portable timegm replacement
    std::time_t timegm_portable(std::tm *tm)
    {
#if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(tm);
#else
        return timegm(tm);
#endif
    }

    std::chrono::system_clock::time_point stringToTimePoint(const std::string &utc_string)
    {
        std::tm tm = {};
        std::stringstream ss(utc_string);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return std::chrono::system_clock::from_time_t(timegm_portable(&tm));
    }

    std::string timePointToString(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string timePointToKey(std::chrono::system_clock::time_point tp)
    {
        std::time_t time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:00:00.000Z");
        return ss.str();
    }
}

// --- Base Class Implementation ---

JresSolverBase::JresSolverBase(const SolverContext& ctx)
    : m_ctx(ctx),
      m_stintWithPitSeconds(0.0),
      m_totalStints(0),
      m_stintLaps(0)
{
    // 1. Calculate Race Parameters
    double lapTimeSeconds = m_ctx.raceData.avgLapTimeInSeconds;
    double pitTimeSeconds = m_ctx.raceData.pitTimeInSeconds;
    
    // Determine laps per stint based on fuel
    m_stintLaps = (m_ctx.raceData.fuelUsePerLap > 0) 
        ? static_cast<int>(m_ctx.raceData.fuelTankSize / m_ctx.raceData.fuelUsePerLap) 
        : 0;
        
    // Calculate total time for one stint cycle (Driving + Pit)
    m_stintWithPitSeconds = (m_stintLaps * lapTimeSeconds) + pitTimeSeconds;
    
    // Calculate total number of stints needed
    double raceDurationSeconds = m_ctx.raceData.durationHours * 3600.0;
    m_totalStints = (m_stintWithPitSeconds > 0) 
        ? static_cast<int>(std::ceil(raceDurationSeconds / m_stintWithPitSeconds)) 
        : 0;

    if (m_totalStints <= 0)
    {
        throw std::runtime_error("Invalid race parameters: totalStints must be > 0.");
    }

    // 2. Filter Participant Pools
    for (const auto& member : m_ctx.raceData.teamMembers) {
        if (member.isDriver) m_driverPool.push_back(member);
        if (member.isSpotter) m_spotterPool.push_back(member);
    }

    if (m_driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }
}
