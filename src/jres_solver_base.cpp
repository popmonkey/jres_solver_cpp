/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver_base.cpp
 * @brief Base class for the JRES Solver.
 */
#include "jres_solver_base.hpp"

JresSolverBase::JresSolverBase(const jres::internal::SolverInput& input, const JresSolverOptions& options)
    : m_input(input), m_options(options)
{
    // Filter Participant Pools
    for (const auto& member : m_input.teamMembers) {
        if (member.isDriver) m_driverPool.push_back(member);
        if (member.isSpotter) m_spotterPool.push_back(member);
    }

    if (m_driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }
}