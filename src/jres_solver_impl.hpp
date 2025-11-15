#pragma once

#include "jres_solver_types.hpp"
#include <memory>
#include <vector>
#include <string>

// --- Forward Declarations ---
class CbcModel;
class OsiClpSolverInterface;
struct ParticipantModel; // Our internal struct

/**
 * @brief C++ internal solver implementation.
 *
 * This class contains all the core logic for
 * building and solving the optimization model.
 */
class JresSolverImpl
{
public:
    /**
     * @brief Construct a new solver instance.
     * @param ctx The solver context containing all race data.
     */
    JresSolverImpl(const SolverContext& ctx);

    /**
     * @brief Solves the schedule.
     * @return A JSON object with the results.
     */
    json solve();

    /**
     * @brief Destructor.
     * Required for std::unique_ptr to forward-declared types.
     */
    ~JresSolverImpl();

private:
    // --- Helper Methods ---
    ParticipantModel add_participant_model(
        CbcModel &model,
        const std::vector<TeamMember> &participants,
        const std::string &prefix,
        double stintWithPitSeconds,
        int stintLaps);

    // --- Member Variables ---
    SolverContext m_ctx; // Copy of the context
    double m_stintWithPitSeconds;
    int m_totalStints;
    int m_stintLaps;

    std::vector<TeamMember> m_driverPool;
    std::vector<TeamMember> m_spotterPool;

    // Use pointers to hide Cbc details from the header
    std::unique_ptr<OsiClpSolverInterface> m_mainSolver;
    std::unique_ptr<CbcModel> m_mainModel;
};
