/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_diagnostic_solver.hpp
 * @brief Diagnostic solver for the JRES Solver library.
 */
#pragma once

#include "jres_solver_base.hpp"

class JresDiagnosticSolver : public JresSolverBase
{
public:
    JresDiagnosticSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options);
    ~JresDiagnosticSolver();

    jres::internal::SolverOutput diagnose();
};