#pragma once

#include "jres_solver_utils.hpp"
#include <memory>

// Forward declarations to keep headers clean
class CbcModel;
class OsiClpSolverInterface;

class JresStandardSolver : public JresSolverBase
{
public:
    JresStandardSolver(const SolverContext& ctx);
    ~JresStandardSolver();

    json solve();

private:
    // Helper to build the complex variable model for drivers/spotters
    ParticipantModel add_participant_model(
        CbcModel &model,
        const std::vector<TeamMember> &participants,
        const std::string &prefix,
        double stintWithPitSeconds,
        int stintLaps);

    std::unique_ptr<OsiClpSolverInterface> m_mainSolver;
    std::unique_ptr<CbcModel> m_mainModel;
};
