/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_standard_solver.cpp
 * @brief Standard solver for the JRES Solver library (Elastic/Diagnostic enabled).
 */
#include "jres_standard_solver.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>

#include "Highs.h"

// Penalty Constants
static const double kPenaltySlack = 1000000.0;
static const double kPenaltyUnavailable = 10000000.0;
static const double kRewardPreferred = -1.0;
static const double kRewardProximity = -0.5; // Incentive for spotting adjacent to driving
static const double kCostFairness = 10.0; 

static void add_role_coupling_incentive(
    Highs* highs,
    const std::vector<jres::internal::TeamMember>& pool,
    const std::map<std::pair<std::string, int>, int>& driverVars,
    const std::map<std::pair<std::string, int>, int>& spotterVars,
    size_t numStints,
    double weight)
{
    if (std::abs(weight) < 1e-6) return;

    for (const auto &p : pool) {
        for (size_t s = 0; s < numStints - 1; ++s) {
            bool hasDriver = driverVars.count({p.name, s});
            bool hasSpotter = spotterVars.count({p.name, s + 1});

            if (hasDriver && hasSpotter) {
                int d_var = driverVars.at({p.name, s});
                int s_var = spotterVars.at({p.name, s + 1});

                // If there is a transition from driving (stint s) to spotting (stint s+1), reward it.
                // If s and s+1 are in the same block, d_var == d_var_next (conceptually).
                // Actually, d_var is the variable for stint s.
                
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

JresStandardSolver::CapacityAnalysis JresStandardSolver::calculate_max_potential_capacity(const std::vector<jres::internal::TeamMember>& participants)
{
    // Parse stint times once
    std::vector<std::chrono::system_clock::time_point> startTimes;
    std::vector<std::chrono::system_clock::time_point> endTimes;
    startTimes.reserve(m_input.stints.size());
    endTimes.reserve(m_input.stints.size());

    std::chrono::system_clock::time_point raceStart;
    std::chrono::system_clock::time_point raceEnd;
    bool raceTimesInit = false;

    for (const auto& stint : m_input.stints) {
        auto s = jres::internal::TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = jres::internal::TimeHelpers::stringToTimePoint(stint.endTime);
        startTimes.push_back(s);
        endTimes.push_back(e);

        if(!raceTimesInit) {
            raceStart = s;
            raceEnd = e;
            raceTimesInit = true;
        } else {
            if(s < raceStart) raceStart = s;
            if(e > raceEnd) raceEnd = e;
        }
    }

    CapacityAnalysis analysis;
    analysis.totalCapacity = 0;
    std::ostringstream ss;

    for (const auto& p : participants) {
        // Build Availability
        std::vector<bool> is_available(m_input.stints.size(), true);
        auto member_availability_it = m_input.availability.find(p.name);
        if (member_availability_it != m_input.availability.end()) {
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                std::string key = jres::internal::TimeHelpers::timePointToKey(startTimes[s]);
                auto time_it = member_availability_it->second.find(key);
                if (time_it != member_availability_it->second.end() && 
                    time_it->second == jres::internal::Availability::Unavailable) {
                    is_available[s] = false;
                }
            }
        }

        std::vector<bool> planned_drive(m_input.stints.size(), false);
        int base_capacity = 0;
        double driver_total_hours = 0.0;

        for(size_t s=0; s<m_input.stints.size(); ++s) {
            if (is_available[s]) {
                planned_drive[s] = true;
                base_capacity++;
                
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTimes[s] - startTimes[s]).count();
                driver_total_hours += static_cast<double>(duration_ms) / 3600000.0;
            }
        }
        
        // Adjust for Global Minimum Rest (One Instance)
        int final_capacity = base_capacity;
        if (m_input.minimumRestHours > 0) {
             auto minRestDuration = std::chrono::hours(m_input.minimumRestHours);
             int min_loss = base_capacity; 
             bool found_valid_window = false;

             std::vector<std::chrono::system_clock::time_point> candidateStarts;
             candidateStarts.push_back(raceStart);
             for(const auto& t : endTimes) candidateStarts.push_back(t);

             for(const auto& tStart : candidateStarts) {
                 auto tEnd = tStart + minRestDuration;
                 if (tEnd > raceEnd) continue;
                 found_valid_window = true;

                 int current_loss = 0;
                 for(size_t s=0; s<m_input.stints.size(); ++s) {
                     if (planned_drive[s]) {
                         if (startTimes[s] < tEnd && endTimes[s] > tStart) {
                             current_loss++;
                         }
                     }
                 }
                 if (current_loss < min_loss) min_loss = current_loss;
             }
             
             if (!found_valid_window) {
                 final_capacity = 0; // Impossible to satisfy rest
             } else {
                 final_capacity -= min_loss;
             }
        }
        
        analysis.totalCapacity += final_capacity;
        
        ss << "\n- " << p.name << ": " << final_capacity 
           << " stints (approx " << std::fixed << std::setprecision(1) << driver_total_hours 
           << "h, MinRest=" << m_input.minimumRestHours << "h)";
    }
    analysis.details = ss.str();
    return analysis;
}

void JresStandardSolver::add_participant_model(
    Highs &highs,
    const std::vector<jres::internal::TeamMember> &participants,
    std::map<std::pair<std::string, int>, int>& workVars)
{
    if (participants.empty()) return;

    // Pre-parse stint times
    std::vector<std::chrono::system_clock::time_point> startTimes;
    startTimes.reserve(m_input.stints.size());

    for (const auto& stint : m_input.stints) {
        startTimes.push_back(jres::internal::TimeHelpers::stringToTimePoint(stint.startTime));
    }

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
                workVars[{p.name, s_idx}] = workVarIdx;

                std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(startTimes[s_idx]);
                auto member_availability_it = m_input.availability.find(p.name);
                if (member_availability_it != m_input.availability.end()) {
                    auto time_availability_it = member_availability_it->second.find(availabilityKey);
                    if (time_availability_it != member_availability_it->second.end()) {
                        if (time_availability_it->second == jres::internal::Availability::Unavailable) {
                            total_cost += kPenaltyUnavailable;
                            any_unavailable = true;
                        } else if (time_availability_it->second == jres::internal::Availability::Preferred) {
                            total_cost += kRewardPreferred;
                        }
                    }
                }
            }

            if (any_unavailable) {
                m_unavailableVars.insert(workVarIdx);
            }
            
            highs.changeColCost(workVarIdx, total_cost);
        }
    }
}

void JresStandardSolver::add_balancing_constraints(
    Highs &highs,
    const std::vector<jres::internal::TeamMember> &participants,
    const std::map<std::pair<std::string, int>, int>& workVars,
    double avgStints)
{
    for (const auto &p : participants) {
        std::vector<int> stint_indices;
        std::vector<double> stint_values;
        
        std::map<int, double> varCounts;
        for (size_t s = 0; s < m_input.stints.size(); ++s) {
            if (workVars.count({p.name, (int)s})) {
                int v = workVars.at({p.name, (int)s});
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

void JresStandardSolver::apply_minimum_rest_constraints(
        Highs &highs,
        const std::vector<jres::internal::TeamMember> &participants,
        const std::map<std::pair<std::string, int>, int>& driverVars,
        const std::map<std::pair<std::string, int>, int>& spotterVars,
        bool enforceCombined
    )
{
    // Pre-parse stint times
    std::vector<std::chrono::system_clock::time_point> startTimes;
    std::vector<std::chrono::system_clock::time_point> endTimes;
    startTimes.reserve(m_input.stints.size());
    endTimes.reserve(m_input.stints.size());

    // Find race start and end
    std::chrono::system_clock::time_point raceStart;
    std::chrono::system_clock::time_point raceEnd;
    bool raceTimesInit = false;

    for (const auto& stint : m_input.stints) {
        auto s = jres::internal::TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = jres::internal::TimeHelpers::stringToTimePoint(stint.endTime);
        startTimes.push_back(s);
        endTimes.push_back(e);

        if(!raceTimesInit) {
            raceStart = s;
            raceEnd = e;
            raceTimesInit = true;
        } else {
            if(s < raceStart) raceStart = s;
            if(e > raceEnd) raceEnd = e;
        }
    }

    for (const auto &p : participants)
    {
        if (m_input.minimumRestHours <= 0) continue;
        auto minRestDuration = std::chrono::hours(m_input.minimumRestHours);
            
        // Generate Candidates
        std::vector<std::chrono::system_clock::time_point> candidateStarts;
        candidateStarts.push_back(raceStart);
        for(const auto& t : endTimes) candidateStarts.push_back(t);

        // Build Block Sets
        std::vector<std::set<int>> blockSets;
        blockSets.reserve(candidateStarts.size());
        
        for(const auto& tStart : candidateStarts) {
            auto tEnd = tStart + minRestDuration;
            if (tEnd > raceEnd) continue;

            std::set<int> blocked;
            for(size_t s=0; s<m_input.stints.size(); ++s) {
                // Overlap check
                if (startTimes[s] < tEnd && endTimes[s] > tStart) {
                    // Check Driver
                    if (driverVars.count({p.name, s})) {
                        blocked.insert(driverVars.at({p.name, s}));
                    }
                    // Check Spotter (if combined)
                    if (enforceCombined && spotterVars.count({p.name, s})) {
                        blocked.insert(spotterVars.at({p.name, s}));
                    }
                }
            }
            blockSets.push_back(blocked);
        }

        // Prune Supersets
        std::vector<bool> keep(blockSets.size(), true);
        bool anyEmpty = false;

        for(size_t i=0; i<blockSets.size(); ++i) {
            if (blockSets[i].empty()) {
                anyEmpty = true;
                break;
            }
        }

        if (!anyEmpty) {
            for(size_t i=0; i<blockSets.size(); ++i) {
                if (!keep[i]) continue;
                for(size_t j=0; j<blockSets.size(); ++j) {
                    if (i == j || !keep[j]) continue;
                    
                    // Check if blockSets[j] is subset of blockSets[i]
                    if (std::includes(blockSets[i].begin(), blockSets[i].end(), 
                                        blockSets[j].begin(), blockSets[j].end())) {
                        keep[i] = false;
                        break; 
                    }
                }
            }

            // Create Variables
            std::vector<int> restOptionVars;
            for(size_t i=0; i<blockSets.size(); ++i) {
                if(!keep[i]) continue;

                int yVar = highs.getNumCol();
                highs.addVar(0.0, 1.0);
                highs.changeColIntegrality(yVar, HighsVarType::kInteger);
                restOptionVars.push_back(yVar);
                
                // y + x <= 1
                for(int stintVar : blockSets[i]) {
                    highs.addRow(-kHighsInf, 1.0, 2, 
                        std::vector<int>{yVar, stintVar}.data(),
                        std::vector<double>{1.0, 1.0}.data());
                }
            }

            if (!restOptionVars.empty()) {
                // sum(y) + slack >= 1
                int slackVar = highs.getNumCol();
                highs.addVar(0.0, 1.0);
                highs.changeColCost(slackVar, kPenaltySlack);
                
                SlackInfo info;
                info.type = "Minimum Rest (One Instance)";
                info.memberName = p.name;
                info.stintIndex = -1; 
                info.limit = 1.0;
                m_slackInfo[slackVar] = info;

                std::vector<int> idx = restOptionVars;
                std::vector<double> val(idx.size(), 1.0);
                idx.push_back(slackVar);
                val.push_back(1.0);
                
                highs.addRow(1.0, kHighsInf, (int)idx.size(), idx.data(), val.data());
            }
        }
    }
}

jres::internal::SolverOutput JresStandardSolver::solve()
{
    using namespace std::chrono;
    auto startTotal = high_resolution_clock::now();
    jres::internal::SolverOutput output;

    // --- Arithmetic Pre-flight Check ---
    int totalStints = (int)m_input.stints.size();
    CapacityAnalysis capAnalysis = calculate_max_potential_capacity(m_driverPool);
    
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

    // --- Hard Constraint: iRacing Fair Share Rule ---
    // Rule: Fair Share = 1/4 of (Total Duration / Num Drivers)
    double total_duration_hours = 0.0;
    std::vector<double> stint_durations_hours;
    stint_durations_hours.reserve(m_input.stints.size());
    
    for (const auto& stint : m_input.stints) {
        auto s = jres::internal::TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = jres::internal::TimeHelpers::stringToTimePoint(stint.endTime);
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
        double h = static_cast<double>(ms) / 3600000.0;
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
                if (m_driverWorkVars.count({p.name, s})) {
                    int v = m_driverWorkVars.at({p.name, s});
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
            
            SlackInfo info;
            info.type = "Fair Share Rule (Minimum Time)";
            info.memberName = p.name;
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
        add_balancing_constraints(*m_highs, m_driverPool, m_driverWorkVars, avg_stints_per_driver);
    }

    // --- Coverage Constraints (One driver per stint) ---
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
            throw std::runtime_error("Model is infeasible (Stint " + std::to_string(s) + " has no candidates).");
        }
        m_highs->addRow(1.0, 1.0, (int)indices.size(), indices.data(), values.data());
    }

    // --- Switching Penalty ---
    if (m_options.switchingPenalty > 0.0) {
        for (size_t s = 1; s < m_input.stints.size(); ++s) {
            int switchVar = m_highs->getNumCol();
            m_highs->addVar(0.0, 1.0);
            m_highs->changeColIntegrality(switchVar, HighsVarType::kInteger);
            m_highs->changeColCost(switchVar, m_options.switchingPenalty);

            // Constraint: switchVar >= driver_s - driver_{s-1} for each driver
            // switchVar - driver_s + driver_{s-1} >= 0
            for (const auto& p : m_driverPool) {
                if (m_driverWorkVars.count({p.name, s}) && m_driverWorkVars.count({p.name, s - 1})) {
                    int var_s = m_driverWorkVars.at({p.name, s});
                    int var_prev = m_driverWorkVars.at({p.name, s - 1});
                    
                    if (var_s != var_prev) { // Only add if different variables (blocks changed)
                        std::vector<int> idx = {switchVar, var_s, var_prev};
                        std::vector<double> val = {1.0, -1.0, 1.0};
                        m_highs->addRow(0.0, kHighsInf, 3, idx.data(), val.data());
                    }
                }
            }
        }
    }

    // --- Rotation Beat (Rhythm) Incentive ---
    if (m_options.rotationBeatWeight > 1e-6) {
        const size_t N = m_driverPool.size();
        if (N > 0 && m_input.stints.size() > N) {
            for (size_t s = N; s < m_input.stints.size(); ++s) {
                size_t target_s = s % N;
                
                for (const auto& p : m_driverPool) {
                    if (m_driverWorkVars.count({p.name, s}) && m_driverWorkVars.count({p.name, target_s})) {
                        int var_current = m_driverWorkVars.at({p.name, s});
                        int var_target = m_driverWorkVars.at({p.name, target_s});

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

        add_role_coupling_incentive(m_highs.get(), m_driverPool, m_driverWorkVars, m_spotterWorkVars, m_input.stints.size(), m_options.roleCouplingWeight);

        // Spotter Balancing
        if (!m_spotterPool.empty()) {
            double avg_stints_per_spotter = static_cast<double>(m_input.stints.size()) / m_spotterPool.size();
            add_balancing_constraints(*m_highs, m_spotterPool, m_spotterWorkVars, avg_stints_per_spotter);
        }

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
                double lower = m_options.allowNoSpotter ? 0.0 : 1.0;
                m_highs->addRow(lower, 1.0, (int)indices.size(), indices.data(), values.data());
            }
        }

        // Driver != Spotter
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
    }
    
    // Apply Rest Constraints
    bool enforceCombinedRest = (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED);
    apply_minimum_rest_constraints(*m_highs, m_input.teamMembers, m_driverWorkVars, m_spotterWorkVars, enforceCombinedRest);


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

    // Check Slacks (Covers both Drivers and Spotters in Integrated mode)
    for (const auto& [varIdx, info] : m_slackInfo) {
        if (varIdx < colValues.size() && colValues[varIdx] > 0.001) {
            std::ostringstream ss;
            ss << "Violation: " << info.type << " for " << info.memberName;
            if (info.stintIndex >= 0) {
               ss << " at Stint " << info.stintIndex;
            }
            ss << " (Value: " << colValues[varIdx] << ")";
            output.diagnosis.push_back(ss.str());
        }
    }
    
    // Check Unavailable Assignments
    for (int varIdx : m_unavailableVars) {
         if (varIdx < colValues.size() && colValues[varIdx] > 0.5) {
             // We can defer detailed message generation to the loop below
         }
    }

    for (size_t s = 0; s < m_input.stints.size(); ++s) {
        jres::internal::ScheduleEntry entry;
        entry.id = m_input.stints[s].id;
        entry.startTime = m_input.stints[s].startTime;
        entry.endTime = m_input.stints[s].endTime;
        entry.driver = "N/A";
        entry.spotter = "N/A";
        
        // Extract Driver
        for (const auto& p : m_driverPool) {
            if (m_driverWorkVars.count({p.name, s})) {
                int idx = m_driverWorkVars.at({p.name, s});
                if (colValues[idx] > 0.5) {
                    entry.driver = p.name;
                    if (m_unavailableVars.count(idx)) {
                        output.diagnosis.push_back("Violation: Unavailable Driver " + p.name + " assigned to Stint " + std::to_string(s));
                    }
                    break;
                }
            }
        }

        // Extract Spotter (if Integrated)
        if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED) {
            for (const auto& p : m_spotterPool) {
                if (m_spotterWorkVars.count({p.name, s})) {
                    int idx = m_spotterWorkVars.at({p.name, s});
                    if (colValues[idx] > 0.5) {
                        entry.spotter = p.name;
                        if (m_unavailableVars.count(idx)) {
                            output.diagnosis.push_back("Violation: Unavailable Spotter " + p.name + " assigned to Stint " + std::to_string(s));
                        }
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

            Highs spotterSolver;
            spotterSolver.setOptionValue("output_flag", false);
            if (m_options.timeLimit > 0) spotterSolver.setOptionValue("time_limit", static_cast<double>(m_options.timeLimit));
            spotterSolver.setOptionValue("mip_rel_gap", m_options.optimalityGap);

            add_participant_model(spotterSolver, m_spotterPool, m_spotterWorkVars);

            // Spotter Balancing
            double avg_stints_per_spotter = static_cast<double>(m_input.stints.size()) / m_spotterPool.size();
            add_balancing_constraints(spotterSolver, m_spotterPool, m_spotterWorkVars, avg_stints_per_spotter);

            // Spotter Coverage Constraints
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
                    double lower = m_options.allowNoSpotter ? 0.0 : 1.0;
                    spotterSolver.addRow(lower, 1.0, (int)indices.size(), indices.data(), values.data());
                }
            }
            
            // Cannot spot if driving
            for (size_t s = 0; s < m_input.stints.size(); ++s) {
                const std::string& driverName = output.schedule[s].driver;
                if (driverName != "N/A" && m_spotterWorkVars.count({driverName, s})) {
                    spotterSolver.changeColBounds(m_spotterWorkVars.at({driverName, s}), 0.0, 0.0);
                }
            }

            // Incentivize Spotting Adjacent to Driving (Proximity & Role Coupling)
            // Calculate Rewards per Block Var
            std::map<int, double> spotterRewards;
            for (const auto& p : m_spotterPool) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                     if (!m_spotterWorkVars.count({p.name, s})) continue;
                     int varIdx = m_spotterWorkVars.at({p.name, s});
                     
                     double additionalReward = 0.0;
                     if (s > 0 && output.schedule[s-1].driver == p.name) {
                         additionalReward += (std::abs(m_options.roleCouplingWeight) > 1e-6) ? -m_options.roleCouplingWeight : kRewardProximity;
                     }
                     if (s < m_input.stints.size() - 1 && output.schedule[s+1].driver == p.name) {
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
                        ss << "Violation: " << info.type << " for Spotter " << info.memberName;
                        if (info.stintIndex >= 0) {
                            ss << " at Stint " << info.stintIndex;
                        }
                        output.diagnosis.push_back(ss.str());
                    }
                }

                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    for (const auto& p : m_spotterPool) {
                        if (m_spotterWorkVars.count({p.name, s})) {
                            int idx = m_spotterWorkVars.at({p.name, s});
                            if (sColValues[idx] > 0.5) {
                                output.schedule[s].spotter = p.name;
                                if (m_unavailableVars.count(idx)) {
                                    output.diagnosis.push_back("Violation: Unavailable Spotter " + p.name + " assigned to Stint " + std::to_string(s));
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
    
    output.teamMembers = m_input.teamMembers;
    return output;
}
