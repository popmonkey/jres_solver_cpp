# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(JresSolver CXX)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the public include directory for the library
set(JRES_SOLVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Find Dependencies ---

# Cbc (COIN-OR Branch and Cut)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_FIND_ROOT_PATH})

find_path(CLP_INCLUDE_DIR NAMES OsiClpSolverInterface.hpp PATH_SUFFIXES include/clp/coin REQUIRED)
find_path(COINUTILS_INCLUDE_DIR NAMES CoinPragma.hpp PATH_SUFFIXES include/coinutils/coin REQUIRED)
find_path(OSI_INCLUDE_DIR NAMES OsiSolverInterface.hpp PATH_SUFFIXES include/osi/coin REQUIRED)
find_path(CBC_INCLUDE_DIR_MAIN NAMES CbcModel.hpp PATH_SUFFIXES include/cbc/coin REQUIRED)

find_library(CBC_LIBRARY NAMES Cbc PATH_SUFFIXES lib REQUIRED)
find_library(OSICLP_LIBRARY NAMES OsiClp PATH_SUFFIXES lib REQUIRED)
find_library(OSI_LIBRARY NAMES Osi PATH_SUFFIXES lib REQUIRED)
find_library(CLP_LIBRARY NAMES Clp PATH_SUFFIXES lib REQUIRED)
find_library(COINUTILS_LIBRARY NAMES CoinUtils PATH_SUFFIXES lib REQUIRED)

set(CBC_LIBRARIES
  ${CBC_LIBRARY} ${OSICLP_LIBRARY} ${OSI_LIBRARY} ${CLP_LIBRARY} ${COINUTILS_LIBRARY}
)
set(CBC_INCLUDE_DIRS
  ${CLP_INCLUDE_DIR} ${COINUTILS_INCLUDE_DIR} ${OSI_INCLUDE_DIR} ${CBC_INCLUDE_DIR_MAIN}
)

# lohmann/json (Header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include
)

# cxxopts (Header-only)
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)


# --- Define Our Library ---

add_library(jres_solver SHARED
  src/jres_solver.cpp
)

# Add the 'include' directory so we can find <jres_solver/jres_solver.hpp>
target_include_directories(jres_solver PUBLIC
  ${JRES_SOLVER_INCLUDE_DIR}
)

# Cbc headers are only needed internally by jres_solver.cpp
target_include_directories(jres_solver PRIVATE
  ${CBC_INCLUDE_DIRS}
)

# Link all dependencies to the LIBRARY
target_link_libraries(jres_solver
  PUBLIC
    nlohmann_json

  PRIVATE
    ${CBC_LIBRARIES}
)

# --- Define Our Executable (The Client) ---

add_executable(solver
  # CHANGED: This now points to its new home in cmd/
  cmd/solver/cli.cpp
)

# Link the EXECUTABLE against our library and its own dependencies
target_link_libraries(solver PRIVATE
  # This links our library
  jres_solver

  # This is needed by cli.cpp for argument parsing
  cxxopts
)
# Note: We removed nlohmann_json because `jres_solver`
# provides it to us automatically (it's a PUBLIC dependency).


# --- Enable Testing ---
# These lines are correct. They tell CMake to look for a
# 'CMakeLists.txt' file inside the 'test' directory and run it.
enable_testing()
add_subdirectory(test)