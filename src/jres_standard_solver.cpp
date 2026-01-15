/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.cpp
 * @brief Standard solver for the JRES Solver library (Elastic/Diagnostic enabled).
 */
#include "jres_standard_solver.hpp"
#include "analysis/capacity_analyzer.hpp"
#include "analysis/solver_diagnostics.hpp"
#include "constraints/balancing.hpp"
#include "constraints/minimum_rest.hpp"
#include "constraints/max_busy_time.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <set>

#include "Highs.h"

// Penalty Constants
static const double kPenaltySlack = 100000.0;       // Soft constraint (Rest, Fair Share)
static const double kPenaltyUnavailable = 10000000.0;    // Original value, high enough
static const double kRewardPreferred = -1.0;
static const double kRewardProximity = -0.5; // Incentive for spotting adjacent to driving

JresStandardSolver::JresStandardSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options)
    : m_input(input), m_options(options)
{
    // Filter Participant Pools
    std::set<jres::internal::ID> seenNames;
    for (const auto& member : m_input.teamMembers) {
        if (seenNames.count(member.nameId)) {
            std::string name = m_input.strings.get_string(member.nameId);
            throw std::runtime_error("Duplicate team member name: " + name);
        }
        seenNames.insert(member.nameId);

        if (member.isDriver) m_driverPool.push_back(member);
        if (member.isSpotter) m_spotterPool.push_back(member);
    }

    if (m_driverPool.empty()) {
        throw std::runtime_error("No drivers available for this race.");
    }

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
    std::map<std::pair<jres::internal::ID, int>, int>& workVars)
{
    if (participants.empty()) return;

    // Determine Block Structure
    int consecutive = m_input.consecutiveStints;
    if (consecutive < 1) consecutive = 1;
    
    std::vector<std::vector<int>> blocks;
    for(size_t s=0; s<m_input.stints.size(); ) {
        std::vector<int> block;
        for(int k=0; k<consecutive && s < m_input.stints.size(); ++k) {
            block.push_back((int)s);
            s++;
        }
        blocks.push_back(block);
    }

    for (const auto &p : participants)
    {
        int prevWorkVarIdx = -1;
        for (const auto& block : blocks) {
            int workVarIdx = highs.getNumCol();
            highs.addVar(0.0, 1.0); // Binary variable
            highs.changeColIntegrality(workVarIdx, HighsVarType::kInteger);

            if (prevWorkVarIdx != -1) {
                 // Constraint: Cannot drive adjacent blocks (Strict consecutiveStints limit)
                 // x_{block_i} + x_{block_{i+1}} <= 1
                 std::vector<int> idx = {prevWorkVarIdx, workVarIdx};
                 std::vector<double> val = {1.0, 1.0};
                 highs.addRow(-kHighsInf, 1.0, 2, idx.data(), val.data());
            }
            prevWorkVarIdx = workVarIdx;

            double total_cost = 0.0;
            bool any_unavailable = false;

            // Map all stints in this block to this variable and accumulate cost
            for (int s_idx : block) {
                workVars[{p.nameId, s_idx}] = workVarIdx;

                auto s_time = m_input.stints[s_idx].startTime;
                auto e_time = m_input.stints[s_idx].endTime;

                // Start checking from the hour bucket where the stint starts
                auto t_cursor = jres::internal::TimeHelpers::roundToHour(s_time);

                while (t_cursor < e_time) {
                    auto member_availability_it = m_input.availability.find(p.nameId);
                    if (member_availability_it != m_input.availability.end()) {
                        auto time_availability_it = member_availability_it->second.find(t_cursor);
                        if (time_availability_it != member_availability_it->second.end()) {
                            if (time_availability_it->second == jres::internal::Availability::Unavailable) {
                                total_cost += kPenaltyUnavailable;
                                any_unavailable = true;
                            } else if (time_availability_it->second == jres::internal::Availability::Preferred) {
                                total_cost += kRewardPreferred;
                            }
                        }
                    }
                    t_cursor += 3600;
                }
            }

            if (any_unavailable) {
                m_unavailableVars.insert(workVarIdx);
            }
            
            highs.changeColCost(workVarIdx, total_cost);
        }
    }
}

jres::internal::SolverOutput JresStandardSolver::solve()
{
    using namespace std::chrono;
    auto startTotal = high_resolution_clock::now();
    jres::internal::SolverOutput output;
    
    // Copy string table to output so we can resolve IDs later
    output.strings = m_input.strings;

    // Populate Config
    output.config.consecutiveStints = m_input.consecutiveStints;
    output.config.minimumRestHours = m_input.minimumRestHours;
    output.config.maximumBusyHours = m_input.maximumBusyHours;
    output.config.firstStintDriver = m_input.firstStintDriver;

    // --- Arithmetic Pre-flight Check ---
    int totalStints = (int)m_input.stints.size();
    auto capAnalysis = jres::internal::CapacityAnalyzer::calculate_max_potential_capacity(m_driverPool, m_input);
    
    if (capAnalysis.totalCapacity < totalStints) {
        // Build detailed error message
        std::string err = "Insufficient driver capacity: Total stints " + std::to_string(totalStints) + 
                          " > Max potential capacity " + std::to_string(capAnalysis.totalCapacity) + ". Breakdown:"
                          + capAnalysis.details;
        
        output.diagnosis.push_back(err);
        throw std::runtime_error(err);
    }

    // --- Build Driver Model ---
    add_participant_model(*m_highs, m_driverPool, m_driverWorkVars);

    // --- Hard Constraint: First Stint Driver ---
    if (m_input.firstStintDriver != -1) {
        bool found = false;
        if (m_driverWorkVars.count({m_input.firstStintDriver, 0})) {
            int varIdx = m_driverWorkVars.at({m_input.firstStintDriver, 0});
            m_highs->changeColBounds(varIdx, 1.0, 1.0);
            found = true;
        }

        if (!found) {
             std::string name = m_input.strings.get_string(m_input.firstStintDriver);
             throw std::runtime_error("First stint driver '" + name + "' is not a valid driver or is unavailable.");
        }
    }

    // --- Hard Constraint: iRacing Fair Share Rule ---
    // Rule: Fair Share = 1/4 of (Total Duration / Num Drivers)
    double total_duration_hours = 0.0;
    std::vector<double> stint_durations_hours;
    stint_durations_hours.reserve(m_input.stints.size());
    
    for (const auto& stint : m_input.stints) {
        double h = std::difftime(stint.endTime, stint.startTime) / 3600.0;
        stint_durations_hours.push_back(h);
        total_duration_hours += h;
    }

    const double num_drivers = m_driverPool.size();
    if (num_drivers > 0) {
        double min_fair_share_hours = (total_duration_hours / num_drivers) * 0.25;

        for (const auto &p : m_driverPool) {
            std::vector<int> idx;
            std::vector<double> val;
            
            // Map: varIdx -> totalDuration
            std::map<int, double> varDurations;

            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                if (m_driverWorkVars.count({p.nameId, (int)s})) {
                    int v = m_driverWorkVars.at({p.nameId, (int)s});
                    varDurations[v] += stint_durations_hours[s];
                }
            }

            for(auto const& [v, dur] : varDurations) {
                idx.push_back(v);
                val.push_back(dur);
            }

            if (idx.empty()) continue; 

            // Elastic Constraint: Sum(duration * x) + slack >= min_fair_share
            int slackVar = m_highs->getNumCol();
            m_highs->addVar(0.0, kHighsInf);
            m_highs->changeColCost(slackVar, kPenaltySlack);
            
            jres::internal::SlackInfo info;
            info.type = "Fair Share Rule (Minimum Time)";
            info.memberNameId = p.nameId;
            info.stintIndex = -1;
            info.limit = min_fair_share_hours;
            m_slackInfo[slackVar] = info;
            
            idx.push_back(slackVar);
            val.push_back(1.0);

            m_highs->addRow(min_fair_share_hours, kHighsInf, (int)idx.size(), idx.data(), val.data());
        }
    }

    // --- Incentivize Balanced Driving (Soft Constraint) ---
    const double num_stints = m_input.stints.size();
    const double avg_stints_per_driver = num_drivers > 0 ? num_stints / num_drivers : 0;

    if (num_drivers > 0) {
        jres::constraints::add_balancing_constraints(*m_highs, m_driverPool, m_input, m_driverWorkVars, avg_stints_per_driver);
    }

    // --- Coverage Constraints (One driver per stint) ---
    for (size_t s = 0; s < m_input.stints.size(); ++s)
    {
        std::vector<int> indices;
        std::vector<double> values;
        for (const auto &p : m_driverPool)
        {
            if (m_driverWorkVars.count({p.nameId, (int)s})) {
                indices.push_back(m_driverWorkVars.at({p.nameId, (int)s}));
                values.push_back(1.0);
            }
        }
        if (indices.empty()) {
            throw std::runtime_error("Model is infeasible (Stint " + std::to_string(s) + " has no candidates).");
        }
        m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
    }

    // --- Rotation Beat (Rhythm) Incentive ---
    if (m_options.rotationBeatWeight > 1e-6) {
        const size_t N = m_driverPool.size();
        if (N > 0 && m_input.stints.size() > N) {
            for (size_t s = N; s < m_input.stints.size(); ++s) {
                size_t target_s = s % N;
                
                for (const auto& p : m_driverPool) {
                    if (m_driverWorkVars.count({p.nameId, (int)s}) && m_driverWorkVars.count({p.nameId, (int)target_s})) {
                        int var_current = m_driverWorkVars.at({p.nameId, (int)s});
                        int var_target = m_driverWorkVars.at({p.nameId, (int)target_s});

                        // Create deviation variable d >= |current - target|
                        // We minimize d, so cost is +weight
                        int devVar = m_highs->getNumCol();
                        m_highs->addVar(0.0, 1.0);
                        // Relaxed to continuous is fine for deviation between binary vars
                        m_highs->changeColCost(devVar, m_options.rotationBeatWeight);

                        // d >= current - target  =>  d - current + target >= 0
                        m_highs->addRow(0.0, kHighsInf, 3, 
                            std::vector<int>{devVar, var_current, var_target}.data(), 
                            std::vector<double>{1.0, -1.0, 1.0}.data());

                        // d >= target - current  =>  d + current - target >= 0
                        m_highs->addRow(0.0, kHighsInf, 3, 
                            std::vector<int>{devVar, var_current, var_target}.data(), 
                            std::vector<double>{1.0, 1.0, -1.0}.data());
                    }
                }
            }
        }
    }

    // --- Spotter Model (Integrated or Sequential) ---
    if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED) {
        if (m_spotterPool.empty() && !m_options.allowNoSpotter) {
             output.diagnosis.push_back("No spotters available for Integrated Mode.");
        }
        
        add_participant_model(*m_highs, m_spotterPool, m_spotterWorkVars);

        jres::constraints::add_role_coupling_incentive(m_highs.get(), m_driverPool, m_driverWorkVars, m_spotterWorkVars, m_input.stints.size(), m_options.roleCouplingWeight);

        // Spotter Balancing
        if (!m_spotterPool.empty()) {
            double avg_stints_per_spotter = static_cast<double>(m_input.stints.size()) / m_spotterPool.size();
            jres::constraints::add_balancing_constraints(*m_highs, m_spotterPool, m_input, m_spotterWorkVars, avg_stints_per_spotter);
        }

        // Spotter Coverage
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            std::vector<int> indices;
            std::vector<double> values;
            for (const auto& p : m_spotterPool) {
                if (m_spotterWorkVars.count({p.nameId, (int)s})) {
                    indices.push_back(m_spotterWorkVars.at({p.nameId, (int)s}));
                    values.push_back(1.0);
                }
            }
            if (!indices.empty()) {
                double lower = m_options.allowNoSpotter ? 0.0 : 1.0;
                m_highs->addRow(lower, 1.0, (int)indices.size(), indices.data(), values.data());
            }
        }

        // Driver != Spotter
        for (const auto& p : m_input.teamMembers) {
            if (p.isDriver && p.isSpotter) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    if (m_driverWorkVars.count({p.nameId, (int)s}) && m_spotterWorkVars.count({p.nameId, (int)s})) {
                        std::vector<int> idx = { m_driverWorkVars.at({p.nameId, (int)s}), m_spotterWorkVars.at({p.nameId, (int)s}) };
                        std::vector<double> val = {1.0, 1.0};
                        m_highs->addRow(0.0, 1.0, 2, idx.data(), val.data());
                    }
                }
            }
        }
    }
    
    // Apply Rest Constraints
    bool enforceCombinedRest = (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED);
    jres::constraints::apply_minimum_rest_constraints(*m_highs, m_input, m_input.teamMembers, m_driverWorkVars, m_spotterWorkVars, enforceCombinedRest, m_slackInfo);
    jres::constraints::apply_max_busy_time_constraints(*m_highs, m_input, m_input.teamMembers, m_driverWorkVars, m_spotterWorkVars, enforceCombinedRest, m_slackInfo);


    // --- Solve Main Model (Drivers + Spotters if Integrated) ---
    auto endSetup = high_resolution_clock::now();
    double setupDurationMs = duration<double, std::milli>(endSetup - startTotal).count();

    auto solveStart = high_resolution_clock::now();
    m_highs->run();
    auto solveEnd = high_resolution_clock::now();
    double driverSolveDurationMs = duration<double, std::milli>(solveEnd - solveStart).count();

    // Populate stats
    const HighsInfo& info = m_highs->getInfo();
    output.stats.modelColumns = m_highs->getNumCol();
    output.stats.modelRows = m_highs->getNumRow();
    output.stats.searchNodes = (int)info.mip_node_count;
    output.stats.finalGap = info.mip_gap;
    output.stats.setupDurationMs = setupDurationMs;
    output.stats.driverSolveDurationMs = driverSolveDurationMs;
    output.stats.spotterSolveDurationMs = 0.0;

    HighsModelStatus status = m_highs->getModelStatus();

    // Check for infeasibility
    if (status != HighsModelStatus::kOptimal && status != HighsModelStatus::kTimeLimit) {
        output.diagnosis.push_back("Model is infeasible (Status: " + std::to_string((int)status) + ")");
        return output;
    }

    // --- Extract Solution and Diagnostics ---
    const auto& solution = m_highs->getSolution();
    const std::vector<double>& colValues = solution.col_value;

    // Use human-readable diagnostic formatter
    auto formattedDiags = jres::analysis::formatHumanDiagnostic(
        m_slackInfo,
        m_unavailableVars,
        m_driverWorkVars,
        m_spotterWorkVars,
        colValues,
        m_input,
        m_driverPool,
        m_spotterPool
    );
    output.diagnosis.insert(output.diagnosis.end(), formattedDiags.begin(), formattedDiags.end());

    for (size_t s = 0; s < m_input.stints.size(); ++s) {
        jres::internal::ScheduleEntry entry;
        entry.id = m_input.stints[s].id;
        entry.startTime = m_input.stints[s].startTime;
        entry.endTime = m_input.stints[s].endTime;
        
        // Extract Driver
        for (const auto& p : m_driverPool) {
            if (m_driverWorkVars.count({p.nameId, (int)s})) {
                int idx = m_driverWorkVars.at({p.nameId, (int)s});
                if (colValues[idx] > 0.5) {
                    entry.driverId = p.nameId;
                    break;
                }
            }
        }

        // Extract Spotter (if Integrated)
        if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED) {
            for (const auto& p : m_spotterPool) {
                if (m_spotterWorkVars.count({p.nameId, (int)s})) {
                    int idx = m_spotterWorkVars.at({p.nameId, (int)s});
                    if (colValues[idx] > 0.5) {
                        entry.spotterId = p.nameId;
                        break;
                    }
                }
            }
        }
        
        output.schedule.push_back(entry);
    }

    // --- Spotter Solver (Sequential) ---
    if (m_options.spotterMode == JRES_SPOTTER_MODE_SEQUENTIAL) {
        if (m_spotterPool.empty()) {
             if (!m_options.allowNoSpotter) {
                 output.diagnosis.push_back("No spotters available for Sequential Mode.");
             }
        } else {
            // Clear slack info for spotter run to avoid confusion (indices will reset)
            m_slackInfo.clear();
            m_unavailableVars.clear();
            m_spotterWorkVars.clear();

            Highs spotterSolver;
            spotterSolver.setOptionValue("output_flag", false);
            if (m_options.timeLimit > 0) spotterSolver.setOptionValue("time_limit", static_cast<double>(m_options.timeLimit));
            spotterSolver.setOptionValue("mip_rel_gap", m_options.optimalityGap);

            add_participant_model(spotterSolver, m_spotterPool, m_spotterWorkVars);

            // Spotter Balancing
            double avg_stints_per_spotter = static_cast<double>(m_input.stints.size()) / m_spotterPool.size();
            jres::constraints::add_balancing_constraints(spotterSolver, m_spotterPool, m_input, m_spotterWorkVars, avg_stints_per_spotter);

            // Spotter Coverage Constraints
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                std::vector<int> indices;
                std::vector<double> values;
                for (const auto& p : m_spotterPool) {
                    if (m_spotterWorkVars.count({p.nameId, (int)s})) {
                        indices.push_back(m_spotterWorkVars.at({p.nameId, (int)s}));
                        values.push_back(1.0);
                    }
                }
                if (!indices.empty()) {
                    double lower = m_options.allowNoSpotter ? 0.0 : 1.0;
                    spotterSolver.addRow(lower, 1.0, (int)indices.size(), indices.data(), values.data());
                }
            }
            
            // Cannot spot if driving
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                jres::internal::ID driverId = output.schedule[s].driverId;
                if (driverId != -1 && m_spotterWorkVars.count({driverId, (int)s})) {
                    spotterSolver.changeColBounds(m_spotterWorkVars.at({driverId, (int)s}), 0.0, 0.0);
                }
            }

            // Apply Max Busy Constraints (taking fixed drivers into account)
            jres::constraints::apply_max_busy_time_constraints(spotterSolver, m_input, m_input.teamMembers, m_driverWorkVars, m_spotterWorkVars, true, m_slackInfo, &output.schedule);

            // Incentivize Spotting Adjacent to Driving (Proximity & Role Coupling)
            // Calculate Rewards per Block Var
            std::map<int, double> spotterRewards;
            for (const auto& p : m_spotterPool) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                     if (!m_spotterWorkVars.count({p.nameId, (int)s})) continue;
                     int varIdx = m_spotterWorkVars.at({p.nameId, (int)s});
                     
                     double additionalReward = 0.0;
                     if (s > 0 && output.schedule[s-1].driverId == p.nameId) {
                         additionalReward += (std::abs(m_options.roleCouplingWeight) > 1e-6) ? -m_options.roleCouplingWeight : kRewardProximity;
                     }
                     if (s < m_input.stints.size() - 1 && output.schedule[s+1].driverId == p.nameId) {
                        additionalReward += kRewardProximity;
                     }
                     spotterRewards[varIdx] += additionalReward;
                }
            }
            
            // Retrieve base costs 
            const std::vector<double>& currentCosts = spotterSolver.getLp().col_cost_;
            
            // Apply
            for(const auto& [varIdx, reward] : spotterRewards) {
                if(varIdx < (int)currentCosts.size()) {
                    spotterSolver.changeColCost(varIdx, currentCosts[varIdx] + reward);
                }
            }

            auto spotterStart = high_resolution_clock::now();
            spotterSolver.run();
            auto spotterEnd = high_resolution_clock::now();
            output.stats.spotterSolveDurationMs = duration<double, std::milli>(spotterEnd - spotterStart).count();

            HighsModelStatus spotterStatus = spotterSolver.getModelStatus();
            if (spotterStatus == HighsModelStatus::kOptimal || spotterStatus == HighsModelStatus::kTimeLimit) {
                const auto& spotterSolution = spotterSolver.getSolution();
                const std::vector<double>& sColValues = spotterSolution.col_value;

                // Check Spotter Slacks
                for (const auto& [varIdx, info] : m_slackInfo) {
                    if (varIdx < sColValues.size() && sColValues[varIdx] > 0.001) {
                        std::ostringstream ss;
                        std::string memberName = m_input.strings.get_string(info.memberNameId);
                        ss << "Violation: " << info.type << " for Spotter " << memberName;
                        if (info.stintIndex >= 0) {
                            ss << " at Stint " << info.stintIndex;
                        }
                        output.diagnosis.push_back(ss.str());
                    }
                }

                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    for (const auto& p : m_spotterPool) {
                        if (m_spotterWorkVars.count({p.nameId, (int)s})) {
                            int idx = m_spotterWorkVars.at({p.nameId, (int)s});
                            if (sColValues[idx] > 0.5) {
                                output.schedule[s].spotterId = p.nameId;
                                if (m_unavailableVars.count(idx)) {
                                    std::string name = m_input.strings.get_string(p.nameId);
                                    output.diagnosis.push_back("Violation: Unavailable Spotter " + name + " assigned to Stint " + std::to_string(s));
                                }
                                break;
                            }
                        }
                    }
                }
            } else {
                output.diagnosis.push_back("Spotter assignment infeasible (Status: " + std::to_string((int)spotterStatus) + ")");
            }
        }
    } 
    
    // --- Final Validation ---
    for (size_t s = 0; s < output.schedule.size(); ++s) {
        if (output.schedule[s].driverId == -1) {
             std::string timeStr = jres::internal::TimeHelpers::timePointToString(std::chrono::system_clock::from_time_t(output.schedule[s].startTime));
             output.diagnosis.push_back("Stint " + std::to_string(s) + " (" + timeStr + ") has no assigned driver.");
        }
        
        bool spotterRequired = (m_options.spotterMode != JRES_SPOTTER_MODE_NONE && !m_options.allowNoSpotter);
        if (spotterRequired && output.schedule[s].spotterId == -1) {
             std::string timeStr = jres::internal::TimeHelpers::timePointToString(std::chrono::system_clock::from_time_t(output.schedule[s].startTime));
             output.diagnosis.push_back("Stint " + std::to_string(s) + " (" + timeStr + ") has no assigned spotter.");
        }
    }

    output.teamMembers = m_input.teamMembers;
    return output;
}
