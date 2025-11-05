# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(RaceSolver CXX)

# Use C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Find Dependencies ---

# 1. Cbc (COIN-OR Branch and Cut) - MANUAL FIND
# We are doing this manually because Homebrew's cbc formula
# does not provide a CbcConfig.cmake file.

# We tell CMake to use the hint provided from the command line
# (e.g., -DCMAKE_PREFIX_PATH=/opt/homebrew)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_FIND_ROOT_PATH})

# 1a. Find the include directories
# We need to find the paths for all of Cbc's dependencies

# Find CLP path (for OsiClpSolverInterface.hpp)
find_path(CLP_INCLUDE_DIR
  NAMES OsiClpSolverInterface.hpp
  PATH_SUFFIXES include/clp/coin
  DOC "Path to Clp include directory"
  REQUIRED
)

# Find COINUTILS path (for CoinPragma.hpp)
find_path(COINUTILS_INCLUDE_DIR
  NAMES CoinPragma.hpp
  PATH_SUFFIXES include/coinutils/coin
  DOC "Path to CoinUtils include directory"
  REQUIRED
)

# Find OSI path (for OsiSolverInterface.hpp)
find_path(OSI_INCLUDE_DIR
  NAMES OsiSolverInterface.hpp
  PATH_SUFFIXES include/osi/coin
  DOC "Path to Osi include directory"
  REQUIRED
)

# Find CBC path (for CbcModel.hpp)
find_path(CBC_INCLUDE_DIR_MAIN
  NAMES CbcModel.hpp
  PATH_SUFFIXES include/cbc/coin
  DOC "Path to Cbc include directory"
  REQUIRED
)

# 1b. Find the libraries
find_library(CBC_LIBRARY NAMES Cbc PATH_SUFFIXES lib REQUIRED)
find_library(OSICLP_LIBRARY NAMES OsiClp PATH_SUFFIXES lib REQUIRED) # <-- THIS IS THE NEW LINE
find_library(OSI_LIBRARY NAMES Osi PATH_SUFFIXES lib REQUIRED)
find_library(CLP_LIBRARY NAMES Clp PATH_SUFFIXES lib REQUIRED)
find_library(COINUTILS_LIBRARY NAMES CoinUtils PATH_SUFFIXES lib REQUIRED)

# 1c. Bundle them into variables (and set link order)
# Order matters: High-level libraries first, dependencies last.
set(CBC_LIBRARIES
  ${CBC_LIBRARY}        # High-level Branch & Cut
  ${OSICLP_LIBRARY}     # The Osi-to-Clp glue (WE WERE MISSING THIS)
  ${OSI_LIBRARY}        # The Open Solver Interface
  ${CLP_LIBRARY}        # The Clp Solver
  ${COINUTILS_LIBRARY}  # Core utilities
)
set(CBC_INCLUDE_DIRS
  ${CLP_INCLUDE_DIR}
  ${COINUTILS_INCLUDE_DIR}
  ${OSI_INCLUDE_DIR}
  ${CBC_INCLUDE_DIR_MAIN}
)

# 2. nlohmann/json (Header-only)
# We just need to tell CMake where to find the headers we downloaded.
# We make this "INTERFACE" so it's private to our project.
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include
)

# 3. cxxopts (Header-only)
# Same as above.
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)


# --- Define Our Executable ---

# This creates an executable named "solver" from our main.cpp file
add_executable(solver
  src/main.cpp
)

# Tell our solver where to find ALL the Cbc headers
target_include_directories(solver PRIVATE
  ${CBC_INCLUDE_DIRS}
)

# --- Link Dependencies to Our Executable ---

# This is the most important part!
# It tells CMake to "link" our "solver" executable against
# the libraries we found or defined.
target_link_libraries(solver PRIVATE
  # This links Cbc and all its dependencies
  ${CBC_LIBRARIES}

  # This adds the include paths for our header-only libs
  nlohmann_json
  cxxopts
)

