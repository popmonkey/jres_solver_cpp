/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.hpp
 * @brief Standard solver for the JRES Solver library.
 */
#pragma once

#include "jres_solver_base.hpp"
#include <memory>
#include <set>
#include <map>

// Forward declaration
class Highs;

class JresStandardSolver : public JresSolverBase
{
public:
    JresStandardSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options);
    ~JresStandardSolver();

    jres::internal::SolverOutput solve();

private:
    // Helper to build the complex variable model for drivers/spotters
    void add_participant_model(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        std::map<std::pair<std::string, int>, int>& workVars
    );

    // Incentivize balanced participation (Soft Constraint)
    void add_balancing_constraints(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        const std::map<std::pair<std::string, int>, int>& workVars,
        double avgStints
    );

    // Enforce minimum rest constraints (potentially across both roles)
    void apply_minimum_rest_constraints(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        const std::map<std::pair<std::string, int>, int>& driverVars,
        const std::map<std::pair<std::string, int>, int>& spotterVars,
        bool enforceCombined
    );

    struct CapacityAnalysis {
        int totalCapacity;
        std::string details;
    };

    // Helper for pre-flight feasibility check
    CapacityAnalysis calculate_max_potential_capacity(const std::vector<jres::internal::TeamMember>& participants);

    struct SlackInfo {
        std::string type;
        std::string memberName;
        int stintIndex;
        double limit = 0.0;
        double actual = 0.0;
    };

    std::unique_ptr<Highs> m_highs;
    std::map<std::pair<std::string, int>, int> m_driverWorkVars;
    std::map<std::pair<std::string, int>, int> m_spotterWorkVars;
    std::map<std::pair<std::string, int>, int> m_switchVars;

    // Elastic Solver State
    std::map<int, SlackInfo> m_slackInfo;
    std::set<int> m_unavailableVars;
};