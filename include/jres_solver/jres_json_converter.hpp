#ifndef JRES_JSON_CONVERTER_HPP
#define JRES_JSON_CONVERTER_HPP

#include "jres_solver.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Converts a JSON string to a JresSolverInput struct.
 *
 * The caller is responsible for freeing the memory allocated for the
 * JresSolverInput struct and its members by calling free_jres_solver_input().
 *
 * @param jsonData The JSON data as a string.
 * @return A pointer to the converted JresSolverInput struct, or nullptr on failure.
 */
JresSolverInput* jres_input_from_json(const char* jsonData);

/**
 * @brief Converts a JresSolverOutput struct to a JSON string.
 *
 * The caller is responsible for freeing the memory allocated for the
 * returned JSON string.
 *
 * @param output The JresSolverOutput struct to convert.
 * @return A JSON string representation of the output, or nullptr on failure.
 */
char* jres_output_to_json(const JresSolverOutput* output);

/**
 * @brief Frees the memory allocated for a JresSolverInput struct.
 *
 * @param input The JresSolverInput struct to free.
 */
void free_jres_solver_input(JresSolverInput* input);

/**
 * @brief Frees the memory allocated for a JresSolverOutput struct.
 *
 * @param output The JresSolverOutput struct to free.
 */
void free_jres_solver_output(JresSolverOutput* output);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // JRES_JSON_CONVERTER_HPP
