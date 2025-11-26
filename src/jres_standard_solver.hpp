/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.hpp
 * @brief Standard solver implementation for JRES endurance race scheduling.
 */

#pragma once

#include "jres_solver_utils.hpp"
#include <memory>

// Forward declaration
class Highs;

class JresStandardSolver : public JresSolverBase
{
public:
    JresStandardSolver(const SolverContext& ctx);
    ~JresStandardSolver();

    json solve();

private:
    // Helper to build the complex variable model for drivers/spotters
    ParticipantModel add_participant_model(
        Highs &highs,
        const std::vector<TeamMember> &participants,
        const std::string &prefix,
        double stintWithPitSeconds,
        int stintLaps);

    std::unique_ptr<Highs> m_highs;
};
