/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.cpp
 * @brief Standard solver for the JRES Solver library.
 */
#include "jres_standard_solver.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

#include "Highs.h"

JresStandardSolver::JresStandardSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options)
    : JresSolverBase(input, options)
{
    m_highs = std::make_unique<Highs>();
    
    // Set HiGHS Options
    m_highs->setOptionValue("output_flag", false);
    m_highs->setOptionValue("presolve", "on");
    
    if (m_options.timeLimit > 0) {
        m_highs->setOptionValue("time_limit", static_cast<double>(m_options.timeLimit));
    }

    m_highs->setOptionValue("mip_rel_gap", m_options.optimalityGap);
}

JresStandardSolver::~JresStandardSolver() = default;

void JresStandardSolver::add_participant_model(
    Highs &highs,
    const std::vector<jres::internal::TeamMember> &participants,
    std::map<std::pair<std::string, int>, int>& workVars)
{
    if (participants.empty()) return;

    for (const auto &p : participants)
    {
        for (size_t s = 0; s < m_input.stints.size(); ++s)
        {
            auto stintStartTime = jres::internal::TimeHelpers::stringToTimePoint(m_input.stints[s].startTime);
            std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(stintStartTime);

            bool isUnavailable = false;
            auto member_availability_it = m_input.availability.find(p.name);
            if (member_availability_it != m_input.availability.end()) {
                auto time_availability_it = member_availability_it->second.find(availabilityKey);
                if (time_availability_it != member_availability_it->second.end()) {
                    if (time_availability_it->second == jres::internal::Availability::Unavailable) {
                        isUnavailable = true;
                    }
                }
            }

            if (isUnavailable) {
                continue; // Do not create a variable for unavailable participants
            }

            int workVarIdx = highs.getNumCol();
            highs.addVar(0.0, 1.0); // Add binary variable
            workVars[{p.name, s}] = workVarIdx;
            
            double cost = 0.0;
            if (member_availability_it != m_input.availability.end()) {
                auto time_availability_it = member_availability_it->second.find(availabilityKey);
                if (time_availability_it != member_availability_it->second.end()) {
                    if (time_availability_it->second == jres::internal::Availability::Preferred) {
                        cost = -1.0;
                    }
                }
            }

            highs.changeColCost(workVarIdx, cost);
            highs.changeColIntegrality(workVarIdx, HighsVarType::kInteger);
        }

        // --- Hard Constraint: Max Consecutive Stints ---
        int maxConsecutive = p.maxStints;
        if (maxConsecutive == 0 || m_input.stints.size() < static_cast<size_t>(maxConsecutive + 1)) continue; // No limit if maxStints is 0 (or less)

        for (size_t s = 0; s <= m_input.stints.size() - (maxConsecutive + 1); ++s) {
            std::vector<int> consIdx;
            std::vector<double> consVal;
            for (size_t i = 0; i < maxConsecutive + 1; ++i) { // Window of maxConsecutive + 1
                if (workVars.count({p.name, s + i})) {
                    consIdx.push_back(workVars.at({p.name, s + i}));
                    consVal.push_back(1.0);
                }
            }
            if (consIdx.empty()) continue; // No variables in this window for this driver

            // Sum of window must be <= maxConsecutive
            // This formulation is actually for "at most X stints in a window of X+1"
            // So if maxConsecutive = 1, then at most 1 stint in a window of 2 (stints s and s+1)
            // This means one stint can be driven, then there must be a break.
            highs.addRow(-kHighsInf, maxConsecutive, (int)consIdx.size(), consIdx.data(), consVal.data());
        }
    }
}

jres::internal::SolverOutput JresStandardSolver::solve()
{
    using namespace std::chrono;
    auto startTotal = high_resolution_clock::now();

    // --- Build Driver Model ---
    add_participant_model(*m_highs, m_driverPool, m_driverWorkVars);

    // --- Hard Constraint: Fair Share ---
    const double num_stints = m_input.stints.size();
    const double num_drivers = m_driverPool.size();
    if (num_drivers > 0) {
        const double min_stints_per_driver = std::floor((num_stints / num_drivers) / 4.0);
        for (const auto &p : m_driverPool) {
            std::vector<int> driver_stint_indices;
            std::vector<double> driver_stint_values;
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                if (m_driverWorkVars.count({p.name, s})) {
                    driver_stint_indices.push_back(m_driverWorkVars.at({p.name, s}));
                    driver_stint_values.push_back(1.0);
                }
            }
            if (!driver_stint_indices.empty()) {
                m_highs->addRow(min_stints_per_driver, kHighsInf, (int)driver_stint_indices.size(), driver_stint_indices.data(), driver_stint_values.data());
            }
        }
    }

    // --- Add Coverage Constraints (One driver per stint) ---
    for (size_t s = 0; s < m_input.stints.size(); ++s)
    {
        std::vector<int> indices;
        std::vector<double> values;
        for (const auto &p : m_driverPool)
        {
            if (m_driverWorkVars.count({p.name, s})) {
                indices.push_back(m_driverWorkVars.at({p.name, s}));
                values.push_back(1.0);
            }
        }
        if (indices.empty()) {
            throw std::runtime_error("Model is infeasible.");
        }
        m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
    }

    // --- Add balancing variables and objective ---
    const double avg_stints_per_driver = num_stints / num_drivers;
    for (const auto &p : m_driverPool) {
        std::vector<int> driver_stint_indices;
        std::vector<double> driver_stint_values;
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            if (m_driverWorkVars.count({p.name, s})) {
                driver_stint_indices.push_back(m_driverWorkVars.at({p.name, s}));
                driver_stint_values.push_back(1.0);
            }
        }

        if (driver_stint_indices.empty()) continue;

        // Variable for total stints for this driver
        int total_stints_var = m_highs->getNumCol();
        m_highs->addVar(0.0, kHighsInf);
        driver_stint_indices.push_back(total_stints_var);
        driver_stint_values.push_back(-1.0);
        m_highs->addRow(0.0, 0.0, (int)driver_stint_indices.size(), driver_stint_indices.data(), driver_stint_values.data());

        // Deviation variables
        int over_avg_var = m_highs->getNumCol();
        m_highs->addVar(0.0, kHighsInf);
        int under_avg_var = m_highs->getNumCol();
        m_highs->addVar(0.0, kHighsInf);
        
        // over_avg >= total_stints - avg
        m_highs->addRow(0.0, kHighsInf, 2, std::vector<int>{over_avg_var, total_stints_var}.data(), std::vector<double>{1.0, -1.0}.data());
        m_highs->changeRowBounds(m_highs->getNumRow() - 1, -avg_stints_per_driver, kHighsInf);
        
        // under_avg >= avg - total_stints
        m_highs->addRow(0.0, kHighsInf, 2, std::vector<int>{under_avg_var, total_stints_var}.data(), std::vector<double>{1.0, 1.0}.data());
        m_highs->changeRowBounds(m_highs->getNumRow() - 1, avg_stints_per_driver, kHighsInf);

        // Add to objective
        m_highs->changeColCost(over_avg_var, 1.0);
        m_highs->changeColCost(under_avg_var, 1.0);
    }

    // --- Incentivize Consecutive Stints ---
    for (const auto &p : m_driverPool) {
        if (p.maxStints <= 1) continue; // No incentive if they can't do consecutive stints

        for (size_t s = 0; s < m_input.stints.size() - 1; ++s) {
            if (m_driverWorkVars.count({p.name, s}) && m_driverWorkVars.count({p.name, s + 1})) {
                int var_s = m_driverWorkVars.at({p.name, s});
                int var_next = m_driverWorkVars.at({p.name, s + 1});

                int consecutive_var = m_highs->getNumCol();
                m_highs->addVar(0.0, 1.0);
                m_highs->changeColIntegrality(consecutive_var, HighsVarType::kInteger);
                m_highs->changeColCost(consecutive_var, -1.5); // Reward for consecutive stints

                // z <= x_s
                m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{consecutive_var, var_s}.data(), std::vector<double>{1.0, -1.0}.data());
                // z <= x_{s+1}
                m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{consecutive_var, var_next}.data(), std::vector<double>{1.0, -1.0}.data());
                // z >= x_s + x_{s+1} - 1  => z - x_s - x_{s+1} >= -1
                m_highs->addRow(-1.0, kHighsInf, 3, std::vector<int>{consecutive_var, var_s, var_next}.data(), std::vector<double>{1.0, -1.0, -1.0}.data());
            }
        }
    }


    // --- Add Spotter Model (Integrated Mode) ---
    if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED)
    {
        if (m_spotterPool.empty() && !m_options.allowNoSpotter) {
            throw std::runtime_error("Model is infeasible.");
        }
        add_participant_model(*m_highs, m_spotterPool, m_spotterWorkVars);

        // Spotter Coverage
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            std::vector<int> indices;
            std::vector<double> values;
            for (const auto& p : m_spotterPool) {
                if (m_spotterWorkVars.count({p.name, s})) {
                    indices.push_back(m_spotterWorkVars.at({p.name, s}));
                    values.push_back(1.0);
                }
            }
            if (!indices.empty()) {
                if (m_options.allowNoSpotter) {
                    m_highs->addRow(0.0, 1.0, (int)indices.size(), indices.data(), values.data());
                } else {
                    m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
                }
            } else if (!m_options.allowNoSpotter) {
                throw std::runtime_error("Model is infeasible.");
            }
        }

        for (const auto& p : m_input.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    if (m_driverWorkVars.count({p.name, s}) && m_spotterWorkVars.count({p.name, s})) {
                        std::vector<int> idx = { m_driverWorkVars.at({p.name, s}), m_spotterWorkVars.at({p.name, s}) };
                        std::vector<double> val = {1.0, 1.0};
                        m_highs->addRow(0.0, 1.0, 2, idx.data(), val.data());
                    }
                }
            }
        }

        // --- Soft Constraint: Adjacency of spotter/driver duties ---
        for (const auto& p : m_input.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    if (m_spotterWorkVars.count({p.name, s}) == 0) continue;

                    auto spotterVar = m_spotterWorkVars.at({p.name, s});

                    // Adjacency with drive stint BEFORE
                    if (s > 0 && m_driverWorkVars.count({p.name, s - 1})) {
                        auto driverBeforeVar = m_driverWorkVars.at({p.name, s - 1});
                        
                        int adjBeforeVar = m_highs->getNumCol();
                        m_highs->addVar(0.0, 1.0);
                        m_highs->changeColIntegrality(adjBeforeVar, HighsVarType::kInteger);
                        m_highs->changeColCost(adjBeforeVar, -0.5); // Reward for adjacency

                        // z <= x (spotter)
                        m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{adjBeforeVar, spotterVar}.data(), std::vector<double>{1.0, -1.0}.data());
                        // z <= y (driver before)
                        m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{adjBeforeVar, driverBeforeVar}.data(), std::vector<double>{1.0, -1.0}.data());
                        // z >= x + y - 1  => z - x - y >= -1
                        m_highs->addRow(-1.0, kHighsInf, 3, std::vector<int>{adjBeforeVar, spotterVar, driverBeforeVar}.data(), std::vector<double>{1.0, -1.0, -1.0}.data());
                    }

                    // Adjacency with drive stint AFTER
                    if (s < m_input.stints.size() - 1 && m_driverWorkVars.count({p.name, s + 1})) {
                        auto driverAfterVar = m_driverWorkVars.at({p.name, s + 1});
                        
                        int adjAfterVar = m_highs->getNumCol();
                        m_highs->addVar(0.0, 1.0);
                        m_highs->changeColIntegrality(adjAfterVar, HighsVarType::kInteger);
                        m_highs->changeColCost(adjAfterVar, -0.5); // Reward for adjacency

                        // z <= x (spotter)
                        m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{adjAfterVar, spotterVar}.data(), std::vector<double>{1.0, -1.0}.data());
                        // z <= y (driver after)
                        m_highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{adjAfterVar, driverAfterVar}.data(), std::vector<double>{1.0, -1.0}.data());
                        // z >= x + y - 1 => z - x - y >= -1
                        m_highs->addRow(-1.0, kHighsInf, 3, std::vector<int>{adjAfterVar, spotterVar, driverAfterVar}.data(), std::vector<double>{1.0, -1.0, -1.0}.data());
                    }
                }
            }
        }
    }
    
    auto endSetup = high_resolution_clock::now();
    double setupDurationMs = duration<double, std::milli>(endSetup - startTotal).count();

    auto solveStart = high_resolution_clock::now();
    m_highs->run();
    auto solveEnd = high_resolution_clock::now();
    double driverSolveDurationMs = duration<double, std::milli>(solveEnd - solveStart).count();

    jres::internal::SolverOutput output;
    HighsModelStatus status = m_highs->getModelStatus();

    // Populate stats regardless of outcome
    const HighsInfo& info = m_highs->getInfo();
    output.stats.modelColumns = m_highs->getNumCol();
    output.stats.modelRows = m_highs->getNumRow();
    output.stats.searchNodes = (int)info.mip_node_count;
    output.stats.finalGap = info.mip_gap;
    output.stats.setupDurationMs = setupDurationMs;
    output.stats.driverSolveDurationMs = driverSolveDurationMs;
    output.stats.spotterSolveDurationMs = 0.0; // will be populated by sequential solver

    if (status == HighsModelStatus::kOptimal || status == HighsModelStatus::kTimeLimit) {
        const auto& solution = m_highs->getSolution();
        const std::vector<double>& colValues = solution.col_value;
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            jres::internal::ScheduleEntry entry;
            entry.id = m_input.stints[s].id;
            entry.startTime = m_input.stints[s].startTime;
            entry.endTime = m_input.stints[s].endTime;
            entry.driver = "N/A";
            entry.spotter = "N/A";
            for (const auto& p : m_driverPool) {
                if (m_driverWorkVars.count({p.name, s}) && colValues[m_driverWorkVars.at({p.name, s})] > 0.5) {
                    entry.driver = p.name;
                    break;
                }
            }
            output.schedule.push_back(entry);
        }
        if (m_options.spotterMode == JRES_SPOTTER_MODE_SEQUENTIAL && !m_spotterPool.empty()) {
            Highs spotterSolver;
            spotterSolver.setOptionValue("output_flag", false);
            if (m_options.timeLimit > 0) {
                spotterSolver.setOptionValue("time_limit", static_cast<double>(m_options.timeLimit));
            }
            spotterSolver.setOptionValue("mip_rel_gap", m_options.optimalityGap);
            add_participant_model(spotterSolver, m_spotterPool, m_spotterWorkVars);

            // --- Soft Constraint: Adjacency of spotter/driver duties ---
            for (const auto& p : m_spotterPool) {
                const auto member_it = std::find_if(m_input.teamMembers.begin(), m_input.teamMembers.end(), 
                    [&](const jres::internal::TeamMember& tm){ return tm.name == p.name; });
                if (member_it == m_input.teamMembers.end() || !member_it->isDriver) continue;

                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    if (m_spotterWorkVars.count({p.name, s})) {
                        double cost = 0.0;
                        
                        auto stintStartTime = jres::internal::TimeHelpers::stringToTimePoint(m_input.stints[s].startTime);
                        std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(stintStartTime);
                        auto member_availability_it = m_input.availability.find(p.name);
                        if (member_availability_it != m_input.availability.end()) {
                            auto time_availability_it = member_availability_it->second.find(availabilityKey);
                            if (time_availability_it != member_availability_it->second.end()) {
                                if (time_availability_it->second == jres::internal::Availability::Preferred) {
                                    cost = -1.0;
                                }
                            }
                        }

                        // Adjacency reward
                        if (s > 0 && output.schedule[s - 1].driver == p.name) {
                            cost -= 0.5;
                        }
                        if (s < m_input.stints.size() - 1 && output.schedule[s + 1].driver == p.name) {
                            cost -= 0.5;
                        }
                        
                        if (cost != 0.0) {
                            spotterSolver.changeColCost(m_spotterWorkVars.at({p.name, s}), cost);
                        }
                    }
                }
            }
            
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                std::vector<int> indices;
                std::vector<double> values;
                for (const auto& p : m_spotterPool) {
                    if (m_spotterWorkVars.count({p.name, s})) {
                        indices.push_back(m_spotterWorkVars.at({p.name, s}));
                        values.push_back(1.0);
                    }
                }
                if (!indices.empty()) {
                    if (m_options.allowNoSpotter) {
                        spotterSolver.addRow(0.0, 1.0, (int)indices.size(), indices.data(), values.data());
                    } else {
                        spotterSolver.addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
                    }
                } else if (!m_options.allowNoSpotter) {
                    // This path should not be taken in sequential mode, because the driver schedule is already solved.
                }
            }
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                const std::string& driverName = output.schedule[s].driver;
                if (driverName != "N/A" && m_spotterWorkVars.count({driverName, s})) {
                    spotterSolver.changeColBounds(m_spotterWorkVars.at({driverName, s}), 0.0, 0.0);
                }
            }
            auto spotterStart = high_resolution_clock::now();
            spotterSolver.run();
            auto spotterEnd = high_resolution_clock::now();
            output.stats.spotterSolveDurationMs = duration<double, std::milli>(spotterEnd - spotterStart).count();

            HighsModelStatus spotterStatus = spotterSolver.getModelStatus();
            if (spotterStatus == HighsModelStatus::kOptimal || spotterStatus == HighsModelStatus::kTimeLimit) {
                const auto& spotterSolution = spotterSolver.getSolution();
                const std::vector<double>& spotterColValues = spotterSolution.col_value;
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    for (const auto& p : m_spotterPool) {
                        if (m_spotterWorkVars.count({p.name, s}) && spotterColValues[m_spotterWorkVars.at({p.name, s})] > 0.5) {
                            output.schedule[s].spotter = p.name;
                            break;
                        }
                    }
                }
            }
        } else if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED) {
             for (size_t s = 0; s < m_input.stints.size(); ++s) {
                for (const auto& p : m_spotterPool) {
                    if (m_spotterWorkVars.count({p.name, s}) && colValues[m_spotterWorkVars.at({p.name, s})] > 0.5) {
                        output.schedule[s].spotter = p.name;
                        break;
                    }
                }
            }
        }
    } else if (status == HighsModelStatus::kInfeasible) {
        throw std::runtime_error("Model is infeasible.");
    }
    output.teamMembers = m_input.teamMembers;
    return output;
}