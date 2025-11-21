#pragma once

#include "jres_solver_utils.hpp"

class JresDiagnosticSolver : public JresSolverBase
{
public:
    JresDiagnosticSolver(const SolverContext& ctx);
    ~JresDiagnosticSolver();

    json diagnose();
};
