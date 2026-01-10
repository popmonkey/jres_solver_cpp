/**
 * @file test/test_fair_share.cpp
 * @brief Tests for the "Fair Share" rule enforcement.
 */

#include "gtest/gtest.h"
#include "jres_solver/jres_solver.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstring>

// Helper to create a simple stint
JresStint create_stint(int id, const char* start, const char* end) {
    JresStint s;
    s.id = id;
    s.startTime = start;
    s.endTime = end;
    return s;
}

// Helper to create a driver
JresTeamMember create_driver(const char* name) {
    JresTeamMember m;
    m.name = name;
    m.isDriver = 1;
    m.isSpotter = 0;
    m.tzOffset = 0.0;
    return m;
}

TEST(FairShareTest, EnforcesMinimumRequirement) {
    // Scenario: 4 stints of 1 hour each. Total = 4 hours.
    // 2 Drivers.
    // Equal Share = 2 hours.
    // Fair Share = 2 / 4 = 0.5 hours.
    // Each driver must drive at least 0.5 hours.
    // Stint length = 1 hour.
    // So 1 stint is enough. This should pass easily.
    
    // Scenario 2: 
    // 8 stints of 1 hour. Total = 8 hours.
    // 2 Drivers.
    // Equal Share = 4 hours.
    // Fair Share = 1 hour.
    // Driver A is restricted to only 0 stints? Impossible.
    // Driver A is restricted to 1 stint. 1 >= 1. OK.
    
    // Scenario 3 (The tricky one):
    // 20 stints (20 hours). 2 Drivers.
    // Equal = 10 hours.
    // Fair = 2.5 hours.
    // Driver A is restricted to only 2 stints (Indices 0 and 1) via Availability.
    // Max potential for A = 2 hours.
    // Requirement = 2.5 hours.
    // Violation!
    
    std::vector<JresStint> stints;
    // 20 x 1-hour stints
    std::vector<std::string> startTimes;
    std::vector<std::string> endTimes;
    startTimes.reserve(20);
    endTimes.reserve(20);
    stints.reserve(20);
    
    for(int i=0; i<20; ++i) {
        char bufS[32], bufE[32];
        snprintf(bufS, sizeof(bufS), "2024-01-01T%02d:00:00Z", i);
        snprintf(bufE, sizeof(bufE), "2024-01-01T%02d:00:00Z", i+1);
        startTimes.push_back(bufS);
        endTimes.push_back(bufE);
        stints.push_back(create_stint(i, startTimes.back().c_str(), endTimes.back().c_str()));
    }

    std::vector<JresTeamMember> members;
    members.push_back(create_driver("DriverA")); 
    members.push_back(create_driver("DriverB"));

    // Create Availability for DriverA
    // Available for 0 and 1. Unavailable for 2..19.
    std::vector<JresAvailabilityEntry> availEntriesA;
    availEntriesA.reserve(20);
    
    for(int i=0; i<20; ++i) {
        JresAvailabilityEntry e;
        e.time = startTimes[i].c_str();
        if (i < 2) {
            e.availability = JRES_AVAILABILITY_AVAILABLE;
        } else {
            e.availability = JRES_AVAILABILITY_UNAVAILABLE;
        }
        availEntriesA.push_back(e);
    }

    JresMemberAvailability mavA;
    mavA.name = "DriverA";
    mavA.availability = availEntriesA.data();
    mavA.availability_len = (int)availEntriesA.size();

    std::vector<JresMemberAvailability> availabilities;
    availabilities.push_back(mavA);

    JresSolverInput input = {};
    input.consecutiveStints = 20;
    input.minimumRestHours = 0;
    input.stints = stints.data();
    input.stints_len = (int)stints.size();
    input.teamMembers = members.data();
    input.teamMembers_len = (int)members.size();
    input.availability = availabilities.data();
    input.availability_len = (int)availabilities.size();

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    // This should fail (or return diagnosis) because DriverA capacity=2h < FairShare (2.5h)
    JresSolverOutput* output = diagnose_race_schedule(&input, &options);
    
    ASSERT_NE(output, nullptr);
    
    bool foundFairShareViolation = false;
    for(int i=0; i<output->diagnosis_len; ++i) {
        std::string diag = output->diagnosis[i];
        if (diag.find("Fair Share") != std::string::npos) {
            foundFairShareViolation = true;
            break;
        }
    }
    
    // Debug output if not found
    if (!foundFairShareViolation) {
        for(int i=0; i<output->diagnosis_len; ++i) {
            printf("Diag: %s\n", output->diagnosis[i]);
        }
    }
    
    EXPECT_TRUE(foundFairShareViolation) << "Should have detected Fair Share violation for DriverA";

    free_jres_solver_output(output);
}

TEST(FairShareTest, SuccessScenario) {
    // 20 stints. 2 drivers. Fair share = 2.5h.
    // Driver A max = 3. 3 >= 2.5. OK.
    
    std::vector<JresStint> stints;
    std::vector<std::string> startTimes, endTimes;
    startTimes.reserve(20);
    endTimes.reserve(20);
    stints.reserve(20);

    for(int i=0; i<20; ++i) {
        char bufS[32], bufE[32];
        snprintf(bufS, sizeof(bufS), "2024-01-01T%02d:00:00Z", i);
        snprintf(bufE, sizeof(bufE), "2024-01-01T%02d:00:00Z", i+1);
        startTimes.push_back(bufS);
        endTimes.push_back(bufE);
        stints.push_back(create_stint(i, startTimes.back().c_str(), endTimes.back().c_str()));
    }

    std::vector<JresTeamMember> members;
    members.push_back(create_driver("DriverA")); 
    members.push_back(create_driver("DriverB"));

    JresSolverInput input = {};
    input.consecutiveStints = 1;
    input.minimumRestHours = 0;
    input.stints = stints.data();
    input.stints_len = (int)stints.size();
    input.teamMembers = members.data();
    input.teamMembers_len = (int)members.size();
    input.availability = nullptr;
    input.availability_len = 0;

    JresSolverOptions options = {};
    options.timeLimit = 5;
    options.spotterMode = JRES_SPOTTER_MODE_NONE;
    options.allowNoSpotter = true;
    options.optimalityGap = 0.0;

    JresSolverOutput* output = solve_race_schedule(&input, &options);
    ASSERT_NE(output, nullptr);
    
    // Should be feasible
    EXPECT_EQ(output->diagnosis_len, 0);
    EXPECT_EQ(output->schedule_len, 20);
    
    // Check Driver A assignments
    int driverAStints = 0;
    for(int i=0; i<output->schedule_len; ++i) {
        if (std::string(output->schedule[i].driver) == "DriverA") {
            driverAStints++;
        }
    }
    // Optimization might give A more or less, but must be >= 3? No, >= 2.5. But A can only do 3 max.
    // And optimization (balancing) will try to give A more if B has 17.
    // Actually, balancing tries to equalize. Average is 10.
    // A is capped at 3. B takes rest.
    // So A should have exactly 3 probably (to be closer to 10).
    EXPECT_GE(driverAStints, 3); 

    free_jres_solver_output(output);
}
