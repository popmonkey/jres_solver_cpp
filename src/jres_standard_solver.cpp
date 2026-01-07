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
static const double kPenaltyUnavailable = 1000000.0;
static const double kRewardPreferred = -1.0;
static const double kRewardConsecutive = -2.0;
static const double kRewardProximity = -0.5; // Incentive for spotting adjacent to driving
static const double kCostFairness = 10.0; // Reduced from implied infinity, but weighted higher than simple rewards

static void add_consecutive_incentive(
    Highs* highs,
    const std::vector<jres::internal::TeamMember>& pool,
    const std::map<std::pair<std::string, int>, int>& workVars,
    size_t numStints,
    double reward)
{
    for (const auto &p : pool) {
        if (p.maxStints <= 1) continue;

        for (size_t s = 0; s < numStints - 1; ++s) {
            if (workVars.count({p.name, s}) && workVars.count({p.name, s + 1})) {
                int var_s = workVars.at({p.name, s});
                int var_next = workVars.at({p.name, s + 1});

                int consecutive_var = highs->getNumCol();
                highs->addVar(0.0, 1.0);
                highs->changeColIntegrality(consecutive_var, HighsVarType::kInteger);
                highs->changeColCost(consecutive_var, reward);

                // z <= x_s
                highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{consecutive_var, var_s}.data(), std::vector<double>{1.0, -1.0}.data());
                // z <= x_{s+1}
                highs->addRow(-kHighsInf, 0.0, 2, std::vector<int>{consecutive_var, var_next}.data(), std::vector<double>{1.0, -1.0}.data());
                // z >= x_s + x_{s+1} - 1
                highs->addRow(-1.0, kHighsInf, 3, std::vector<int>{consecutive_var, var_s, var_next}.data(), std::vector<double>{1.0, -1.0, -1.0}.data());
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
        // 1. Build Availability & Greedy MaxConsecutive Pattern
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
        int current_consecutive = 0;
        int maxConsecutive = (p.maxStints > 0) ? p.maxStints : 1;

        double driver_total_hours = 0.0;

        for(size_t s=0; s<m_input.stints.size(); ++s) {
            if (is_available[s]) {
                if (current_consecutive < maxConsecutive) {
                    planned_drive[s] = true;
                    base_capacity++;
                    current_consecutive++;
                    
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(endTimes[s] - startTimes[s]).count();
                    driver_total_hours += static_cast<double>(duration_ms) / 3600000.0;
                } else {
                    current_consecutive = 0; // Forced rest
                }
            } else {
                current_consecutive = 0;
            }
        }
        
        // 2. Adjust for Minimum Rest (One Instance)
        int final_capacity = base_capacity;
        if (p.minimumRestHours > 0) {
             auto minRestDuration = std::chrono::hours(p.minimumRestHours);
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
           << "h, MaxConsecutive=" << p.maxStints 
           << ", MinRest=" << p.minimumRestHours << "h)";
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

    for (const auto &p : participants)
    {
        for (size_t s = 0; s < m_input.stints.size(); ++s)
        {
            std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(startTimes[s]);

            bool isUnavailable = false;
            bool isPreferred = false;

            auto member_availability_it = m_input.availability.find(p.name);
            if (member_availability_it != m_input.availability.end()) {
                auto time_availability_it = member_availability_it->second.find(availabilityKey);
                if (time_availability_it != member_availability_it->second.end()) {
                    if (time_availability_it->second == jres::internal::Availability::Unavailable) {
                        isUnavailable = true;
                    } else if (time_availability_it->second == jres::internal::Availability::Preferred) {
                        isPreferred = true;
                    }
                }
            }

            // Create variable for ALL slots, even if unavailable (Elastic)
            int workVarIdx = highs.getNumCol();
            highs.addVar(0.0, 1.0); // Binary variable
            workVars[{p.name, s}] = workVarIdx;
            highs.changeColIntegrality(workVarIdx, HighsVarType::kInteger);
            
            double cost = 0.0;
            if (isUnavailable) {
                cost = kPenaltyUnavailable;
                m_unavailableVars.insert(workVarIdx);
            } else if (isPreferred) {
                cost = kRewardPreferred;
            }
            highs.changeColCost(workVarIdx, cost);
        }

        // --- Elastic Constraint: Max Consecutive Stints ---
        int maxConsecutive = p.maxStints;
        if (maxConsecutive > 0 && m_input.stints.size() >= static_cast<size_t>(maxConsecutive + 1)) {
             for (size_t s = 0; s <= m_input.stints.size() - (maxConsecutive + 1); ++s) {
                std::vector<int> consIdx;
                std::vector<double> consVal;
                for (size_t i = 0; i < maxConsecutive + 1; ++i) { 
                    if (workVars.count({p.name, s + i})) {
                        consIdx.push_back(workVars.at({p.name, s + i}));
                        consVal.push_back(1.0);
                    }
                }
                if (consIdx.empty()) continue; 

                // Create slack variable s >= 0
                int slackVar = highs.getNumCol();
                highs.addVar(0.0, kHighsInf);
                highs.changeColCost(slackVar, kPenaltySlack);
                
                // Track slack
                SlackInfo info;
                info.type = "Max Consecutive Stints";
                info.memberName = p.name;
                info.stintIndex = (int)s; // Start of the window
                info.limit = (double)maxConsecutive;
                m_slackInfo[slackVar] = info;

                // sum(x) - slack <= maxConsecutive
                consIdx.push_back(slackVar);
                consVal.push_back(-1.0);
                
                highs.addRow(-kHighsInf, maxConsecutive, (int)consIdx.size(), consIdx.data(), consVal.data());
            }
        }
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
        if (p.minimumRestHours <= 0) continue;
        auto minRestDuration = std::chrono::hours(p.minimumRestHours);
            
        // 1. Generate Candidates
        std::vector<std::chrono::system_clock::time_point> candidateStarts;
        candidateStarts.push_back(raceStart);
        for(const auto& t : endTimes) candidateStarts.push_back(t);

        // 2. Build Block Sets
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

        // 3. Prune Supersets
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

            // 4. Create Variables
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

    // --- 1. Arithmetic Pre-flight Check ---
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

    // --- Hard Constraint: Fair Share (Relaxed with penalty) ---
    const double num_stints = m_input.stints.size();
    const double num_drivers = m_driverPool.size();
    const double avg_stints_per_driver = num_drivers > 0 ? num_stints / num_drivers : 0;

    if (num_drivers > 0) {
        // Balancing variables
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

            int total_stints_var = m_highs->getNumCol();
            m_highs->addVar(0.0, kHighsInf);
            driver_stint_indices.push_back(total_stints_var);
            driver_stint_values.push_back(-1.0);
            m_highs->addRow(0.0, 0.0, (int)driver_stint_indices.size(), driver_stint_indices.data(), driver_stint_values.data());

            int over_avg_var = m_highs->getNumCol();
            m_highs->addVar(0.0, kHighsInf);
            int under_avg_var = m_highs->getNumCol();
            m_highs->addVar(0.0, kHighsInf);
            
            m_highs->addRow(0.0, kHighsInf, 2, std::vector<int>{over_avg_var, total_stints_var}.data(), std::vector<double>{1.0, -1.0}.data());
            m_highs->changeRowBounds(m_highs->getNumRow() - 1, -avg_stints_per_driver, kHighsInf);
            
            m_highs->addRow(0.0, kHighsInf, 2, std::vector<int>{under_avg_var, total_stints_var}.data(), std::vector<double>{1.0, 1.0}.data());
            m_highs->changeRowBounds(m_highs->getNumRow() - 1, avg_stints_per_driver, kHighsInf);

            m_highs->changeColCost(over_avg_var, kCostFairness);
            m_highs->changeColCost(under_avg_var, kCostFairness);
        }
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

    // --- Incentivize Consecutive Stints ---
    add_consecutive_incentive(m_highs.get(), m_driverPool, m_driverWorkVars, m_input.stints.size(), kRewardConsecutive);


    // --- Spotter Model (Integrated or Sequential) ---
    if (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED) {
        if (m_spotterPool.empty() && !m_options.allowNoSpotter) {
             output.diagnosis.push_back("No spotters available for Integrated Mode.");
             // We can proceed, but coverage will fail (likely leading to infeasibility or slack usage if we made coverage elastic - which we didn't).
             // If coverage is hard constraint, this will throw "Model is infeasible".
             // Let's rely on the coverage constraint check later.
        }
        
        add_participant_model(*m_highs, m_spotterPool, m_spotterWorkVars);
        add_consecutive_incentive(m_highs.get(), m_spotterPool, m_spotterWorkVars, m_input.stints.size(), kRewardConsecutive);

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
            } else if (!m_options.allowNoSpotter) {
                 // No candidates for this stint
                 // Since coverage is hard, this will be infeasible.
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
    // Integrated: Enforce Combined (Drive + Spot)
    // Sequential (Driver Phase): Enforce Drive Only
    bool enforceCombinedRest = (m_options.spotterMode == JRES_SPOTTER_MODE_INTEGRATED);
    
    // For Sequential mode, we only have driver vars populated right now. Spotter vars are empty.
    // So passing m_spotterWorkVars is fine (it's empty).
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
        throw std::runtime_error("Model is infeasible (Status: " + std::to_string((int)status) + ")");
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
            add_consecutive_incentive(&spotterSolver, m_spotterPool, m_spotterWorkVars, m_input.stints.size(), kRewardConsecutive);

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

            // Incentivize Spotting Adjacent to Driving (Proximity Reward)
            for (const auto& p : m_spotterPool) {
                for (size_t s = 0; s < m_input.stints.size(); ++s) {
                    if (!m_spotterWorkVars.count({p.name, s})) continue;
                    
                    bool adjacentDrive = false;
                    // Check s-1
                    if (s > 0) {
                        if (output.schedule[s-1].driver == p.name) adjacentDrive = true;
                    }
                    // Check s+1
                    if (s < m_input.stints.size() - 1) {
                        if (output.schedule[s+1].driver == p.name) adjacentDrive = true;
                    }

                    if (adjacentDrive) {
                        int varIdx = m_spotterWorkVars.at({p.name, s});
                        // Get current cost
                        double currentCost = 0.0;
                        // Getting current cost is not directly supported by simple API in all versions, 
                        // but we know we set it based on preference/unavailability.
                        // We can just add a separate term or modify existing.
                        // HiGHS `changeColCost` sets the absolute cost.
                        // We should retrieve it first? Or assume base costs.
                        // Let's assume we can query it? No simple getColCost in the minimal binding I recall.
                        // However, we know the cost construction logic: 
                        // Unavailable(1e6) | Preferred(-1) | Neutral(0).
                        
                        // Let's just track it or blindly apply offset if we know it's not unavailable.
                        // We shouldn't incentivize unavailable slots.
                        
                        // Re-check availability
                        bool isUnavailable = false; 
                         // ... (Availability lookup logic duplicated or helper needed?)
                         // Actually, we can check if it's already "Unavailable" penalty.
                         // But we don't have easy access to the cost.
                         
                         // Safer: Re-evaluate availability or trust that if it's unavailable, the 1M penalty swamps this -0.5.
                         // Yes, 1,000,000 - 0.5 is still huge.
                         
                         // How to add? `changeColCost` replaces. 
                         // We don't want to overwrite "Preferred".
                         // We can look up availability again.
                         
                         std::string availabilityKey = jres::internal::TimeHelpers::timePointToKey(jres::internal::TimeHelpers::stringToTimePoint(m_input.stints[s].startTime));
                         bool isPreferred = false;
                         bool isUnavailableExplicit = false;
                         auto member_availability_it = m_input.availability.find(p.name);
                         if (member_availability_it != m_input.availability.end()) {
                            auto time_availability_it = member_availability_it->second.find(availabilityKey);
                            if (time_availability_it != member_availability_it->second.end()) {
                                if (time_availability_it->second == jres::internal::Availability::Preferred) isPreferred = true;
                                if (time_availability_it->second == jres::internal::Availability::Unavailable) isUnavailableExplicit = true;
                            }
                         }

                         double newCost = 0.0;
                         if (isUnavailableExplicit) newCost = kPenaltyUnavailable;
                         else if (isPreferred) newCost = kRewardPreferred;
                         
                         newCost += kRewardProximity; // Add the incentive
                         
                         spotterSolver.changeColCost(varIdx, newCost);
                    }
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