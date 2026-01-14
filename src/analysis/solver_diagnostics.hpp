#pragma once

#include "../jres_internal_types.hpp"
#include <vector>
#include <map>
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

} // namespace jres::analysis
