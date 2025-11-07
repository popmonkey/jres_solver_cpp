# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(RaceSolver CXX)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the public include directory for the library
set(RACESOLVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Find Dependencies ---

# 1. Cbc (COIN-OR Branch and Cut) - MANUAL FIND
# We tell CMake to use the hint provided from the command line
# (e.g., -DCMAKE_PREFIX_PATH=/opt/homebrew)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_FIND_ROOT_PATH})

# 1a. Find the include directories
find_path(CLP_INCLUDE_DIR NAMES OsiClpSolverInterface.hpp PATH_SUFFIXES include/clp/coin REQUIRED)
find_path(COINUTILS_INCLUDE_DIR NAMES CoinPragma.hpp PATH_SUFFIXES include/coinutils/coin REQUIRED)
find_path(OSI_INCLUDE_DIR NAMES OsiSolverInterface.hpp PATH_SUFFIXES include/osi/coin REQUIRED)
find_path(CBC_INCLUDE_DIR_MAIN NAMES CbcModel.hpp PATH_SUFFIXES include/cbc/coin REQUIRED)

# 1b. Find the libraries
find_library(CBC_LIBRARY NAMES Cbc PATH_SUFFIXES lib REQUIRED)
find_library(OSICLP_LIBRARY NAMES OsiClp PATH_SUFFIXES lib REQUIRED)
find_library(OSI_LIBRARY NAMES Osi PATH_SUFFIXES lib REQUIRED)
find_library(CLP_LIBRARY NAMES Clp PATH_SUFFIXES lib REQUIRED)
find_library(COINUTILS_LIBRARY NAMES CoinUtils PATH_SUFFIXES lib REQUIRED)

# 1c. Bundle them into variables
set(CBC_LIBRARIES
  ${CBC_LIBRARY} ${OSICLP_LIBRARY} ${OSI_LIBRARY} ${CLP_LIBRARY} ${COINUTILS_LIBRARY}
)
set(CBC_INCLUDE_DIRS
  ${CLP_INCLUDE_DIR} ${COINUTILS_INCLUDE_DIR} ${OSI_INCLUDE_DIR} ${CBC_INCLUDE_DIR_MAIN}
)

# 2. nlohmann/json (Header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include
)

# 3. cxxopts (Header-only)
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)


# --- Define Our Library ---

# This creates a shared library named "racesolver"
add_library(racesolver SHARED
  src/racesolver.cpp
)

# Tell the library where to find its own public headers
target_include_directories(racesolver PUBLIC
  ${RACESOLVER_INCLUDE_DIR}
)

# Tell the library where to find all the Cbc headers
target_include_directories(racesolver PRIVATE
  ${CBC_INCLUDE_DIRS}
)

# Link all dependencies to the LIBRARY
target_link_libraries(racesolver PRIVATE
  ${CBC_LIBRARIES}
  nlohmann_json
  cxxopts
)

# --- Define Our Executable (The Client) ---

# This creates an executable named "solver" from our main.cpp file
add_executable(solver
  src/main.cpp
)

# Link the EXECUTABLE against our new library
target_link_libraries(solver PRIVATE
  racesolver
  nlohmann_json
  cxxopts
)

# --- Enable Testing ---
# These lines add the 'test' subdirectory, which will download
# GoogleTest and build our 'solver_tests' executable.
enable_testing()
add_subdirectory(test)
