/**
 * @author popmonkey+jres@gmail.com
 * @file src/constraints/minimum_rest.cpp
 * @brief Implementation of minimum rest constraints.
 */
#include "minimum_rest.hpp"
#include "Highs.h"
#include <algorithm>
#include <chrono>

namespace jres::constraints {

static const double kPenaltySlack = 100000.0;

void apply_minimum_rest_constraints(
        Highs &highs,
        const jres::internal::SolverInput& input,
        const std::vector<jres::internal::TeamMember> &participants,
        const std::map<std::pair<std::string, int>, int>& driverVars,
        const std::map<std::pair<std::string, int>, int>& spotterVars,
        bool enforceCombined,
        std::map<int, jres::internal::SlackInfo>& slackInfo
    )
{
    using namespace jres::internal;

    // Pre-parse stint times
    std::vector<std::chrono::system_clock::time_point> startTimes;
    std::vector<std::chrono::system_clock::time_point> endTimes;
    startTimes.reserve(input.stints.size());
    endTimes.reserve(input.stints.size());

    // Find race start and end
    std::chrono::system_clock::time_point raceStart;
    std::chrono::system_clock::time_point raceEnd;
    bool raceTimesInit = false;

    for (const auto& stint : input.stints) {
        auto s = TimeHelpers::stringToTimePoint(stint.startTime);
        auto e = TimeHelpers::stringToTimePoint(stint.endTime);
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
        if (input.minimumRestHours <= 0) continue;
        auto minRestDuration = std::chrono::hours(input.minimumRestHours);
            
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
            for(size_t s=0; s<input.stints.size(); ++s) {
                // Overlap check
                if (startTimes[s] < tEnd && endTimes[s] > tStart) {
                    // Check Driver
                    if (driverVars.count({p.name, (int)s})) {
                        blocked.insert(driverVars.at({p.name, (int)s}));
                    }
                    // Check Spotter (if combined)
                    if (enforceCombined && spotterVars.count({p.name, (int)s})) {
                        blocked.insert(spotterVars.at({p.name, (int)s}));
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
                slackInfo[slackVar] = info;

                std::vector<int> idx = restOptionVars;
                std::vector<double> val(idx.size(), 1.0);
                idx.push_back(slackVar);
                val.push_back(1.0);
                
                highs.addRow(1.0, kHighsInf, (int)idx.size(), idx.data(), val.data());
            }
        }
    }
}

} // namespace jres::constraints
