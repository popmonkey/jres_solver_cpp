# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(JresSolver CXX)

# Use C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ==============================================================================
#  VERSION STRATEGY (Hierarchy of Truth)
# ==============================================================================
set(JRES_VERSION_STRING "0.0.0-dev") # Default fallback

# Override (Passed by CI via -DJRES_VERSION=...)
if(DEFINED JRES_VERSION)
    set(JRES_VERSION_STRING "${JRES_VERSION}")

# Git (Local Development)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(GIT_DESCRIBE_VERSION)
            set(JRES_VERSION_STRING "${GIT_DESCRIBE_VERSION}")
        endif()
    endif()
endif()

message(STATUS "Building JRES Solver Version: ${JRES_VERSION_STRING}")

# Parse Major.Minor.Patch for C++ Defines and Windows RC
set(JRES_VERSION_MAJOR 0)
set(JRES_VERSION_MINOR 0)
set(JRES_VERSION_PATCH 0)

if(JRES_VERSION_STRING MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(JRES_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(JRES_VERSION_MINOR ${CMAKE_MATCH_2})
    set(JRES_VERSION_PATCH ${CMAKE_MATCH_3})
endif()

# ==============================================================================
#  FILE GENERATION
# ==============================================================================

# Generate version.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
)

# Generate resource.rc (Windows Only)
set(VERSION_RC "")
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/resource.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/resource.rc
    )
    set(VERSION_RC ${CMAKE_CURRENT_BINARY_DIR}/generated/resource.rc)
endif()

# Set the public include directory for the library
set(JRES_SOLVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ==============================================================================
#  DEPENDENCIES (HiGHS)
# ==============================================================================

message(STATUS "Configuring for HiGHS Solver...")

# Attempt to find HiGHS via Config (Works for Homebrew & vcpkg)
find_package(highs CONFIG)

if(TARGET highs::highs)
    message(STATUS "Found HiGHS via Config")
    set(HIGHS_LIBRARIES highs::highs)
else()
    # Fallback: Manual discovery (Useful for manual builds on Linux)
    message(STATUS "HiGHS Config not found, attempting manual search...")
    
    find_path(HIGHS_INCLUDE_DIR NAMES Highs.h PATH_SUFFIXES highs)
    find_library(HIGHS_LIBRARY NAMES highs)
    
    if(HIGHS_INCLUDE_DIR AND HIGHS_LIBRARY)
        message(STATUS "Found HiGHS Library: ${HIGHS_LIBRARY}")
        message(STATUS "Found HiGHS Include: ${HIGHS_INCLUDE_DIR}")
        
        add_library(highs::highs UNKNOWN IMPORTED)
        set_target_properties(highs::highs PROPERTIES
            IMPORTED_LOCATION "${HIGHS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${HIGHS_INCLUDE_DIR}"
        )
        set(HIGHS_LIBRARIES highs::highs)
    else()
        message(FATAL_ERROR "Could not find HiGHS library. Please install it (e.g., 'brew install highs' or via vcpkg).")
    endif()
endif()

# ==============================================================================
#  RPATH SETTINGS
# ==============================================================================
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Helper libraries
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include)

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include)

# --- 1. JRES Utils (Static) ---
add_library(jres_utils STATIC src/utils/date_utils.cpp src/utils/zip_writer.cpp)
target_include_directories(jres_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- 2. JRES Solver Core (Shared/DLL) ---
add_library(jres_solver_lib SHARED
  src/jres_solver.cpp
  src/jres_solver_utils.cpp
  src/jres_standard_solver.cpp
  src/jres_diagnostic_solver.cpp
  src/jres_solver_types.cpp
)
set_target_properties(jres_solver_lib PROPERTIES OUTPUT_NAME "jres_solver")

if(WIN32)
    target_sources(jres_solver_lib PRIVATE ${VERSION_RC})
    target_compile_definitions(jres_solver_lib PRIVATE JRES_SOLVER_EXPORTS)
endif()

target_include_directories(jres_solver_lib PUBLIC 
    ${JRES_SOLVER_INCLUDE_DIR} 
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(jres_solver_lib PRIVATE nlohmann_json ${HIGHS_LIBRARIES})

# --- 3. Solver CLI ---
add_executable(solver cmd/solver/cli.cpp)
set_target_properties(solver PROPERTIES OUTPUT_NAME "jres_solver")

if(WIN32)
    target_sources(solver PRIVATE ${VERSION_RC})
endif()

target_include_directories(solver PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

add_dependencies(solver jres_solver_lib)

if (WIN32)
  target_link_directories(solver PRIVATE ${CMAKE_BINARY_DIR}/Release)
endif()

target_link_libraries(solver PRIVATE jres_solver_lib cxxopts nlohmann_json)

# --- 4. JRES Formatter Logic ---
add_library(jres_formatter_lib STATIC src/formatter/formatter_core.cpp)
target_link_libraries(jres_formatter_lib PUBLIC jres_utils nlohmann_json)
target_include_directories(jres_formatter_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- 5. Formatter CLI ---
add_executable(formatter cmd/formatter/cli.cpp)
set_target_properties(formatter PROPERTIES OUTPUT_NAME "jres_formatter")

if(WIN32)
    target_sources(formatter PRIVATE ${VERSION_RC})
endif()

target_include_directories(formatter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(formatter PRIVATE jres_formatter_lib cxxopts)

# --- 6. Tests ---
enable_testing()
add_subdirectory(test)
