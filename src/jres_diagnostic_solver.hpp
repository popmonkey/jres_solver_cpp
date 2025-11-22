/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_diagnostic_solver.hpp
 * @brief Diagnostic solver header for JRES endurance race scheduling.
 */

#pragma once

#include "jres_solver_utils.hpp"

class JresDiagnosticSolver : public JresSolverBase
{
public:
    JresDiagnosticSolver(const SolverContext& ctx);
    ~JresDiagnosticSolver();

    json diagnose();
};
