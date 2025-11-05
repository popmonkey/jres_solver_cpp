/*
 * Phase 1: "Hello, C++ Solver"
 *
 * This file is a simple test to prove our environment is working.
 * It does NOT solve the race problem yet.
 *
 * It tests:
 * 1. C++ compilation
 * 2. cxxopts - for argument parsing
 * 3. nlohmann/json - for JSON parsing
 * 4. Cbc/Osi - for linking and solving a simple problem
 */

#include <iostream>
#include <string>
#include <vector>

// --- Dependency Includes ---

// 1. cxxopts (needs to be in lib/cxxopts/include)
#include "cxxopts.hpp"

// 2. nlohmann/json (needs to be in lib/json/single_include)
#include "nlohmann/json.hpp"

// 3. Cbc/Osi (COIN-OR Solver Interface)
// These headers come from the Homebrew installation
#include "OsiClpSolverInterface.hpp" // The solver implementation (CLP)
#include "CbcModel.hpp"              // The Cbc Model builder
#include "CoinPackedVector.hpp"      // Efficiently stores sparse vectors
#include "CoinBuild.hpp"             // For building constraints

// Use the nlohmann::json namespace for convenience
using json = nlohmann::json;

// Simple logger prefix
#define LOG(msg) std::cout << "[Solver] " << msg << std::endl

/**
 * @brief Tests the Cbc solver by solving a simple LP problem.
 *
 * Problem:
 * max:  1x
 * s.t.: 1x <= 5
 * 0 <= x <= 10
 *
 * Expected Result: x = 5
 */
void runCbcTest()
{
    LOG("--- Cbc Solver Test ---");
    LOG("Solving: max x, s.t. x <= 5");

    // 1. Create a solver interface. We use CLP (Coin Linear Programming)
    //    which is the linear solver Cbc is built on.
    OsiClpSolverInterface solver;
    LOG("Model created.");

    // 2. Set objective sense: -1 for maximize, 1 for minimize
    solver.setObjSense(-1.0);
    LOG("Solver set to maximize.");

    // 3. Add one variable, 'x'
    //    - col vector: empty (no constraints yet)
    //    - bounds: 0.0 to 10.0
    //    - obj coeff: 1.0 (this is the '1x' in 'max: 1x')
    solver.addCol(CoinPackedVector(), 0.0, 10.0, 1.0);
    LOG("Added var 'x' with bounds [0, 10] and obj coeff 1.");

    // 4. Add one constraint: 1x <= 5
    //    - We build a 'row' vector representing '1x'
    CoinPackedVector row;
    int varIndex = 0; // The first variable we added
    double coefficient = 1.0;
    row.insert(varIndex, coefficient);

    //    - Add the row to the solver
    //    - bounds: -infinity to 5.0
    solver.addRow(row, -solver.getInfinity(), 5.0);
    LOG("Added constraint: x <= 5");

    // 5. Solve the problem
    LOG("Solving...");
    solver.initialSolve();
    LOG("Solver finished.");

    // 6. Print the result
    if (solver.isProvenOptimal())
    {
        // Get the solution value for our variable (index 0)
        double solution = solver.getColSolution()[0];
        LOG("Solution: x = " << solution);
    }
    else
    {
        LOG("Solver did not find an optimal solution.");
    }
    LOG("--- Test Complete ---");
}

int main(int argc, char **argv)
{
    LOG("Hello, C++ Race Solver!");

    // --- cxxopts Test ---
    cxxopts::Options options("RaceSolver", "Solves endurance race schedules");
    options.add_options()
        ("f,file", "Input JSON file", cxxopts::value<std::string>())
        ("h,help", "Print usage");
    
    // This will just prove that it links and runs
    auto result = options.parse(argc, argv);
    LOG("Found cxxopts.");

    // --- nlohmann/json Test ---
    // Create a dummy JSON and parse it
    json testJson = {
        {"pi", 3.141},
        {"happy", true},
        {"name", "nlohmann"}};
    
    // This will prove it links and runs
    std::string s = testJson.dump();
    LOG("Found nlohmann/json.");

    // --- Cbc/Osi Test ---
    runCbcTest();

    return 0;
}
