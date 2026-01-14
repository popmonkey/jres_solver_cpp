#pragma once

#include "../jres_internal_types.hpp"
#include <vector>
#include <map>
#include <set>
#include <string>

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
    const std::string& violationDriver,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::map<std::pair<std::string, int>, int>& driverWorkVars,
    const std::vector<double>& colValues
);

/**
 * @brief Generates a human-friendly summary of solver diagnostics.
 * 
 * Follows these rules:
 * 1. Temporal Grouping: Groups consecutive violations.
 * 2. Bottleneck Summary: Checks total man-hours needed vs provided.
 * 3. Priority Root Cause: Unavailable > Max Busy > Minimum Rest.
 * 4. Actionable Advice: Suggests fixes for severe blocks.
 * 
 * @param slackInfo The map of slack variables and their metadata.
 * @param unavailableVars Set of variable indices that represent unavailable assignments.
 * @param driverWorkVars Map of (DriverName, StintIndex) to column index.
 * @param spotterWorkVars Map of (SpotterName, StintIndex) to column index.
 * @param colValues The solution column values.
 * @param input The solver input configuration.
 * @param driverPool The list of drivers.
 * @param spotterPool The list of spotters.
 * @return A list of diagnostic strings.
 */
std::vector<std::string> formatHumanDiagnostic(
    const std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::set<int>& unavailableVars,
    const std::map<std::pair<std::string, int>, int>& driverWorkVars,
    const std::map<std::pair<std::string, int>, int>& spotterWorkVars,
    const std::vector<double>& colValues,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember>& driverPool,
    const std::vector<jres::internal::TeamMember>& spotterPool
);

} // namespace jres::analysis
