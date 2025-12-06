#pragma once

#include "jres_solver_base.hpp"

class JresDiagnosticSolver : public JresSolverBase
{
public:
    JresDiagnosticSolver(const jres::internal::SolverInput& input, const JresSolverOptions& options);
    ~JresDiagnosticSolver();

    jres::internal::SolverOutput diagnose();
};
