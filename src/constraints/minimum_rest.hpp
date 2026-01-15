/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/minimum_rest.hpp
 * @brief Header for minimum rest constraints.
 */
#pragma once
#include "../jres_internal_types.hpp"
#include <map>
#include <vector>
#include <set>

class Highs;

namespace jres::constraints {

    void apply_minimum_rest_constraints(
        Highs &highs,
        const jres::internal::SolverInput& input,
        const std::vector<jres::internal::TeamMember> &participants,
        const std::map<std::pair<jres::internal::ID, int>, int>& driverVars,
        const std::map<std::pair<jres::internal::ID, int>, int>& spotterVars,
        bool enforceCombined,
        std::map<int, jres::internal::SlackInfo>& slackInfo
    );

}
