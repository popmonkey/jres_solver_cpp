/**
 * @author popmonkey+jres@gmail.com
 * @file src/jres_solver.cpp
 * @brief C-API implementation for the JRES Solver library.
 */
#include "jres_solver/jres_solver.hpp"
#include "jres_internal_types.hpp"
#include "jres_standard_solver.hpp"
#include "jres_diagnostic_solver.hpp"
#include "jres_solver/jres_json_converter.hpp"

JresSolverOutput* solve_race_schedule(const JresSolverInput* input, const JresSolverOptions* options) {
    try {
        jres::internal::SolverInput internal_input = jres::internal::from_c_input(input);
        JresStandardSolver solver(internal_input, *options);
        jres::internal::SolverOutput internal_output = solver.solve();
        return jres::internal::to_c_output(internal_output, *options);
    } catch (const std::exception& e) {
        JresSolverOutput* error_output = new JresSolverOutput();
        error_output->schedule_len = 0;
        error_output->schedule = nullptr;
        error_output->diagnosis_len = 1;
        error_output->diagnosis = new const char*[1];
        error_output->diagnosis[0] = jres::internal::allocate_and_copy(e.what());
        return error_output;
    }
}

JresSolverOutput* diagnose_race_schedule(const JresSolverInput* input, const JresSolverOptions* options) {
    try {
        jres::internal::SolverInput internal_input = jres::internal::from_c_input(input);
        JresDiagnosticSolver solver(internal_input, *options);
        jres::internal::SolverOutput internal_output = solver.diagnose();
        return jres::internal::to_c_output(internal_output, *options);
    } catch (const std::exception& e) {
        JresSolverOutput* error_output = new JresSolverOutput();
        error_output->schedule_len = 0;
        error_output->schedule = nullptr;
        error_output->diagnosis_len = 1;
        error_output->diagnosis = new const char*[1];
        error_output->diagnosis[0] = jres::internal::allocate_and_copy(e.what());
        return error_output;
    }
}

void free_json_string(char* json_string) {
    delete[] json_string;
}
