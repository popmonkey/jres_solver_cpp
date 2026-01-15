/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/balancing.cpp
 * @brief Implementation of load balancing constraints.
 */
#include "balancing.hpp"
#include "Highs.h"
#include <cmath>

namespace jres::constraints {

static const double kCostFairness = 10.0; 

void add_role_coupling_incentive(
    Highs* highs,
    const std::vector<jres::internal::TeamMember>& pool,
    const std::map<std::pair<jres::internal::ID, int>, int>& driverVars,
    const std::map<std::pair<jres::internal::ID, int>, int>& spotterVars,
    size_t numStints,
    double weight)
{
    if (std::abs(weight) < 1e-6) return;

    for (const auto &p : pool) {
        for (size_t s = 0; s < numStints - 1; ++s) {
            bool hasDriver = driverVars.count({p.nameId, (int)s});
            bool hasSpotter = spotterVars.count({p.nameId, (int)s + 1});

            if (hasDriver && hasSpotter) {
                int d_var = driverVars.at({p.nameId, (int)s});
                int s_var = spotterVars.at({p.nameId, (int)s + 1});

                // If there is a transition from driving (stint s) to spotting (stint s+1), reward it.
                
                int coupling_var = highs->getNumCol();
                highs->addVar(0.0, 1.0);
                highs->changeColIntegrality(coupling_var, HighsVarType::kInteger);
                highs->changeColCost(coupling_var, -weight);

                // z <= d_var
                highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{coupling_var, d_var}.data(), std::vector<double>{1.0, -1.0}.data());
                // z <= s_var
                highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{coupling_var, s_var}.data(), std::vector<double>{1.0, -1.0}.data());
            }
        }
    }
}

void add_balancing_constraints(
    Highs &highs,
    const std::vector<jres::internal::TeamMember> &participants,
    const jres::internal::SolverInput& input,
    const std::map<std::pair<jres::internal::ID, int>, int>& workVars,
    double avgStints)
{
    for (const auto &p : participants) {
        std::vector<int> stint_indices;
        std::vector<double> stint_values;
        
        std::map<int, double> varCounts;
        for (size_t s = 0; s < input.stints.size(); ++s) {
            if (workVars.count({p.nameId, (int)s})) {
                int v = workVars.at({p.nameId, (int)s});
                varCounts[v] += 1.0;
            }
        }
        for(auto const& [v, count] : varCounts) {
            stint_indices.push_back(v);
            stint_values.push_back(count);
        }

        if (stint_indices.empty()) continue;

        int total_stints_var = highs.getNumCol();
        highs.addVar(0.0, kHighsInf);
        stint_indices.push_back(total_stints_var);
        stint_values.push_back(-1.0);
        highs.addRow(0.0, 0.0, (int)stint_indices.size(), stint_indices.data(), stint_values.data());

        int over_avg_var = highs.getNumCol();
        highs.addVar(0.0, kHighsInf);
        int under_avg_var = highs.getNumCol();
        highs.addVar(0.0, kHighsInf);
        
        std::vector<int> idx_over = {over_avg_var, total_stints_var};
        std::vector<double> val_over = {1.0, -1.0};
        highs.addRow(0.0, kHighsInf, 2, idx_over.data(), val_over.data());
        highs.changeRowBounds(highs.getNumRow() - 1, -avgStints, kHighsInf);
        
        std::vector<int> idx_under = {under_avg_var, total_stints_var};
        std::vector<double> val_under = {1.0, 1.0};
        highs.addRow(0.0, kHighsInf, 2, idx_under.data(), val_under.data());
        highs.changeRowBounds(highs.getNumRow() - 1, avgStints, kHighsInf);

        highs.changeColCost(over_avg_var, kCostFairness);
        highs.changeColCost(under_avg_var, kCostFairness);
    }
}

} // namespace jres::constraints
