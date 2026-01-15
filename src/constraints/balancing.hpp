/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/balancing.hpp
 * @brief Header for load balancing constraints.
 */
#pragma once
#include "../jres_internal_types.hpp"
#include <map>
#include <vector>

class Highs;

namespace jres::constraints {

    void add_role_coupling_incentive(
        Highs* highs,
        const std::vector<jres::internal::TeamMember>& pool,
        const std::map<std::pair<std::string, int>, int>& driverVars,
        const std::map<std::pair<std::string, int>, int>& spotterVars,
        size_t numStints,
        double weight);

    void add_balancing_constraints(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        const jres::internal::SolverInput& input,
        const std::map<std::pair<std::string, int>, int>& workVars,
        double avgStints);

}
