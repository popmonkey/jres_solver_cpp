/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.hpp
 * @brief Standard solver for the JRES Solver library.
 */
#pragma once

#include "jres_internal_types.hpp"
#include "jres_solver/jres_solver.hpp"
#include <memory>
#include <set>
#include <map>

// Forward declaration
class Highs;

class JresStandardSolver
{
public:
    JresStandardSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options);
    ~JresStandardSolver();

    jres::internal::SolverOutput solve();

private:
    const jres::internal::SolverInput& m_input;
    const JresSolverOptions& m_options;

    // Filtered Participant Pools
    std::vector<jres::internal::TeamMember> m_driverPool;
    std::vector<jres::internal::TeamMember> m_spotterPool;

    // Helper to build the complex variable model for drivers/spotters
    void add_participant_model(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        std::map<std::pair<jres::internal::ID, int>, int>& workVars
    );

    std::unique_ptr<Highs> m_highs;
    std::map<std::pair<jres::internal::ID, int>, int> m_driverWorkVars;
    std::map<std::pair<jres::internal::ID, int>, int> m_spotterWorkVars;
    std::map<std::pair<jres::internal::ID, int>, int> m_switchVars;

    // Elastic Solver State
    std::map<int, jres::internal::SlackInfo> m_slackInfo;
    std::set<int> m_unavailableVars;
};
