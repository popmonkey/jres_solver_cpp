# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# --- Find Dependencies ---
# GoogleTest
# This downloads and builds gtest for us at configure time.
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  # This line fixes the timestamp warning
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
# Make gtest available
FetchContent_MakeAvailable(googletest)


# --- Define Our Test Executable ---
add_executable(solver_tests
  test_spotter_modes.cpp
  test_constraints.cpp
  test_errors.cpp
)

# --- Link Dependencies ---
target_link_libraries(solver_tests PRIVATE
  jres_solver
  gtest_main
  nlohmann_json
)

# Tell the test runner where to find our library's public header
target_include_directories(solver_tests PRIVATE
  ${JRES_SOLVER_INCLUDE_DIR}
)

# This tells the 'solver_tests' executable where to find
# 'libjres_solver.dylib' at runtime.
# ${CMAKE_BINARY_DIR} is the 'build/' directory.
set_target_properties(solver_tests PROPERTIES
  INSTALL_RPATH "${CMAKE_BINARY_DIR}"
  BUILD_WITH_INSTALL_RPATH TRUE
)

# Add the test to CTest
include(GoogleTest)
gtest_discover_tests(solver_tests)
