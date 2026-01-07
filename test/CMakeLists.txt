# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# --- Find Dependencies ---
# GoogleTest
include(FetchContent)

# FORCE STATIC GOOGLETEST
# We force Shared Libs OFF for GTest so we don't need to chase down gtest.dll
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Force static GTest" FORCE)
# We force Shared CRT ON so it links correctly with the rest of the MSVC project (/MD)
set(gtest_force_shared_crt ON CACHE BOOL "Force shared CRT" FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# =============================================================================
# Solver Tests
# =============================================================================

add_executable(solver_tests
  test_spotter_modes.cpp
  test_constraints.cpp
  test_errors.cpp
  test_diagnosis.cpp
  test_balancing.cpp
  test_optimization.cpp
  test_minimum_rest.cpp
  test_fair_share.cpp
)

add_dependencies(solver_tests jres_solver_lib)

if (WIN32)
  target_link_directories(solver_tests PRIVATE 
    ${CMAKE_BINARY_DIR}/Release
  )
endif()

target_link_libraries(solver_tests PRIVATE
  jres_solver_lib
  gtest_main
  nlohmann_json
)

target_include_directories(solver_tests PRIVATE
  ${JRES_SOLVER_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(solver_tests PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

set_target_properties(solver_tests PROPERTIES
  INSTALL_RPATH "${CMAKE_BINARY_DIR}"
  BUILD_WITH_INSTALL_RPATH TRUE
)

gtest_discover_tests(solver_tests)


# =============================================================================
# Formatter Tests
# =============================================================================

add_executable(formatter_tests 
  test_formatter_csv.cpp
  test_formatter_summary.cpp
  test_formatter_itinerary.cpp
)

# Link against our static formatter library AND gtest_main
target_link_libraries(formatter_tests PRIVATE
  jres_formatter_lib
  gtest_main
  nlohmann_json
)

# Register tests with CTest using GoogleTest discovery
gtest_discover_tests(formatter_tests)
