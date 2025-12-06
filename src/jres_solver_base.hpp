/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver_base.hpp
 * @brief Base class for the JRES Solver.
 */
#pragma once

#include "jres_internal_types.hpp"
#include "jres_solver/jres_solver.hpp"

class JresSolverBase
{
public:
    JresSolverBase(const jres::internal::SolverInput& input, const JresSolverOptions& options);
    virtual ~JresSolverBase() = default;

protected:
    const jres::internal::SolverInput& m_input;
    const JresSolverOptions& m_options;

    // Filtered Participant Pools
    std::vector<jres::internal::TeamMember> m_driverPool;
    std::vector<jres::internal::TeamMember> m_spotterPool;
};