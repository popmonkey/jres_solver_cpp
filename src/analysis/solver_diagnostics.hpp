/**
 * @author popmonkey+jres@gmail.com
 * @file src/analysis/solver_diagnostics.hpp
 * @brief Header for solver diagnostics.
 */
#pragma once

#include "../jres_internal_types.hpp"
#include <vector>
#include <map>
#include <string>
#include <set>

namespace jres::analysis {

/**
 * @brief Analyzes why a specific driver was not assigned to a stint, analyzing availability,
 * constraints, and other restrictions.
 * 
 * @param stintIndex The index of the stint to analyze.
 * @param violationDriver The driver who was invalidly assigned (slack violation).
 * @param input The solver input configuration and data.
 * @param driverPool The list of available drivers.
 * @param driverWorkVars Map of (DriverName, StintIndex) to column index in the MIP model.
 * @param colValues The solution column values from the solver.
 * @return A detailed string explanation of why other drivers were not suitable candidates.
 */
std::string explain_assignment_failure(
    int stintIndex,
    jres::internal::ID violationDriverId,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverWorkVars,
    const std::vector<double>& colValues
);

/**
 * @brief Formats raw solver slack and violation information into a human-readable report.
 */
std::vector<std::string> formatHumanDiagnostic(
    const std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::set<int>& unavailableVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverWorkVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& spotterWorkVars,
    const std::vector<double>& colValues,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::vector<jres::internal::TeamMember>& spotterPool
);

} // namespace jres::analysis
