# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(JresSolver CXX)

# Use C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Default to Shared libraries (Windows preference), but allow override
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# ==============================================================================
#  VERSION STRATEGY
# ==============================================================================
set(JRES_VERSION_STRING "0.0.0-dev") # Default fallback

if(DEFINED JRES_VERSION)
    set(JRES_VERSION_STRING "${JRES_VERSION}")
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

# Parse Major.Minor.Patch
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

set(JRES_SOLVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ==============================================================================
#  DEPENDENCIES (HiGHS Hybrid Strategy)
# ==============================================================================

message(STATUS "Configuring HiGHS Solver...")

if(APPLE)
    if(EXISTS "/opt/homebrew")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()
    if(EXISTS "/usr/local")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local")
    endif()
endif()

# STRATEGY 1: Look for pre-built/installed package
find_package(highs CONFIG QUIET)

if(TARGET highs::highs)
    message(STATUS "Found HiGHS via Config: highs::highs")
    set(HIGHS_LIBRARIES highs::highs)
else()
    # STRATEGY 2: Build from Source (FetchContent)
    message(STATUS "HiGHS not found. Downloading and building from source (v1.12.0)...")
    
    include(FetchContent)
    FetchContent_Declare(
        highs
        GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
        GIT_TAG        v1.12.0
    )

    # HiGHS Build Options
    set(FAST_BUILD ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE) # Always build HiGHS static
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(HIGHS_GPL OFF CACHE BOOL "" FORCE)
    
    # --- OPENMP CONFIGURATION (GLOBAL DISABLE) ---
    # Disabled everywhere to ensure zero external dependencies (libomp)
    # and to ensure single-threaded behavior for server scalability.
    message(STATUS "Disabling OpenMP for HiGHS (Global)")
    set(OPENMP OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENMP OFF CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(highs)

    # Alias the target so we can use highs::highs consistently
    if(NOT TARGET highs::highs)
        if(TARGET libhighs)
            add_library(highs::highs ALIAS libhighs)
        elseif(TARGET highs)
            add_library(highs::highs ALIAS highs)
        endif()
    endif()

    set(HIGHS_LIBRARIES highs::highs)
endif()

# ==============================================================================
#  RPATH & RUNTIME SETTINGS
# ==============================================================================
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# Ensure we use Static Runtime on Windows (Matches HiGHS static builds)
if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Helper libraries
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include)

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include)

# --- JRES Utils (Static) ---
add_library(jres_utils STATIC src/utils/date_utils.cpp src/utils/zip_writer.cpp)
target_include_directories(jres_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- JRES Solver Core (Shared or Static) ---
add_library(jres_solver_lib 
  src/jres_solver.cpp
  src/jres_json_converter.cpp
  src/jres_internal_types.cpp
  src/jres_solver_base.cpp
  src/jres_standard_solver.cpp
  src/analysis/capacity_analyzer.cpp
  src/constraints/balancing.cpp
  src/constraints/minimum_rest.cpp
  src/constraints/max_busy_time.cpp
)
set_target_properties(jres_solver_lib PROPERTIES OUTPUT_NAME "jres_solver")

if(WIN32)
    target_sources(jres_solver_lib PRIVATE ${VERSION_RC})
    
    if(BUILD_SHARED_LIBS)
        target_compile_definitions(jres_solver_lib PRIVATE JRES_SOLVER_EXPORTS)
    else()
        target_compile_definitions(jres_solver_lib PUBLIC JRES_STATIC)
    endif()
endif()

target_include_directories(jres_solver_lib PUBLIC 
    ${JRES_SOLVER_INCLUDE_DIR} 
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(jres_solver_lib 
    PRIVATE nlohmann_json 
    PUBLIC ${HIGHS_LIBRARIES}
)

# --- Solver CLI ---
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

# --- JRES Formatter Logic ---
add_library(jres_formatter_lib STATIC src/formatter/formatter_core.cpp)
target_link_libraries(jres_formatter_lib PUBLIC jres_utils nlohmann_json)
target_include_directories(jres_formatter_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Formatter CLI ---
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

# ==============================================================================
#  LINUX PORTABILITY
# ==============================================================================
if(UNIX AND NOT APPLE)
    # Statically link C++ runtime to avoid "GLIBCXX_..." version errors on older distros
    target_link_options(solver PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(formatter PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(jres_solver_lib PRIVATE -static-libgcc -static-libstdc++)
endif()

# ==============================================================================
#  INSTALLATION TARGETS
# ==============================================================================
include(GNUInstallDirs)

# Install Libraries
install(TARGETS jres_solver_lib
    EXPORT JresTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install Executables
install(TARGETS solver formatter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install Headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install Tests
enable_testing()
add_subdirectory(test)
