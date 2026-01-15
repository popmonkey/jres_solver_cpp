/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/max_busy_time.hpp
 * @brief Header for maximum busy time constraints.
 */
#pragma once
#include "../jres_internal_types.hpp"
#include <map>
#include <vector>

class Highs;

namespace jres::constraints {

void apply_max_busy_time_constraints(
    Highs &highs,
    const jres::internal::SolverInput& input,
    const std::vector<jres::internal::TeamMember> &participants,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& spotterVars,
    bool enforceCombined,
    std::map<int, jres::internal::SlackInfo>& slackInfo,
    const std::vector<jres::internal::ScheduleEntry>* fixedSchedule = nullptr
);

}
