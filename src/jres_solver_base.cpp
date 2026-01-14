/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver_base.cpp
 * @brief Base class for the JRES Solver.
 */
#include "jres_solver_base.hpp"
#include <set>
#include <stdexcept>

JresSolverBase::JresSolverBase(const jres::internal::SolverInput& input, const JresSolverOptions& options)
    : m_input(input), m_options(options)
{
    // Filter Participant Pools
    std::set<std::string> seenNames;
    for (const auto& member : m_input.teamMembers) {
        if (seenNames.count(member.name)) {
            throw std::runtime_error("Duplicate team member name: " + member.name);
        }
        seenNames.insert(member.name);

        if (member.isDriver) m_driverPool.push_back(member);
        if (member.isSpotter) m_spotterPool.push_back(member);
    }

    if (m_driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }
}