# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.15)

# Set the project name and language
project(JresSolver CXX)

# Use C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set the public include directory for the library
set(JRES_SOLVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Find Dependencies ---

# Windows uses the prebuilt static CBC artifact
if(WIN32)
    message(STATUS "Configuring CBC for Windows (prebuilt static)")

    if(NOT DEFINED CBC_DIR)
        message(FATAL_ERROR "CBC_DIR was not provided by the workflow")
    endif()
    
    # These must be absolute paths
    set(CBC_OVERRIDE_INCLUDE "${CBC_DIR}/x64-windows-static/include/coin-or")
    set(CBC_OVERRIDE_LIB     "${CBC_DIR}/x64-windows-static/lib")

    set(CBC_INCLUDE_DIRS "${CBC_OVERRIDE_INCLUDE}")

    set(CLP_LIBRARY        "${CBC_OVERRIDE_LIB}/Clp.lib")
    set(COINUTILS_LIBRARY  "${CBC_OVERRIDE_LIB}/CoinUtils.lib")
    set(OSI_LIBRARY        "${CBC_OVERRIDE_LIB}/Osi.lib")
    set(OSICLP_LIBRARY     "${CBC_OVERRIDE_LIB}/OsiClp.lib")
    set(CBC_LIBRARY        "${CBC_OVERRIDE_LIB}/Cbc.lib")

# macOS + Linux
else()
    message(STATUS "Configuring CBC using system installation")

    find_path(CLP_INCLUDE_DIR 
        NAMES OsiClpSolverInterface.hpp 
        PATH_SUFFIXES clp/coin coin
    )

    find_path(COINUTILS_INCLUDE_DIR 
        NAMES CoinPragma.hpp 
        PATH_SUFFIXES coinutils/coin coin
    )

    find_path(OSI_INCLUDE_DIR 
        NAMES OsiSolverInterface.hpp 
        PATH_SUFFIXES osi/coin coin
    )

    find_path(CBC_INCLUDE_DIR_MAIN 
        NAMES CbcModel.hpp 
        PATH_SUFFIXES cbc/coin coin
    )

    find_library(CLP_LIBRARY         NAMES Clp)
    find_library(COINUTILS_LIBRARY   NAMES CoinUtils)
    find_library(OSI_LIBRARY         NAMES Osi)
    find_library(OSICLP_LIBRARY      NAMES OsiClp)
    find_library(CBC_LIBRARY         NAMES Cbc)

endif()

# Merge sets (Linux/macOS only)
if(NOT WIN32)
    set(CBC_INCLUDE_DIRS 
        ${CLP_INCLUDE_DIR} 
        ${COINUTILS_INCLUDE_DIR} 
        ${OSI_INCLUDE_DIR} 
        ${CBC_INCLUDE_DIR_MAIN}
    )
endif()

set(CBC_LIBRARIES 
    ${CBC_LIBRARY} 
    ${OSICLP_LIBRARY} 
    ${OSI_LIBRARY} 
    ${CLP_LIBRARY} 
    ${COINUTILS_LIBRARY} 
    ${CGL_LIBRARY}
)

if (WIN32)
  # Append all transitive dependencies for Windows static link (Fixes LNK2019)
  list(APPEND CBC_LIBRARIES 
    "${CBC_OVERRIDE_LIB}/Cgl.lib" 
    "${CBC_OVERRIDE_LIB}/OsiCbc.lib" 
    "${CBC_OVERRIDE_LIB}/OpenBLAS.lib" 
    "${CBC_OVERRIDE_LIB}/lapack.lib" 
    "${CBC_OVERRIDE_LIB}/libf2c.lib" 
    "${CBC_OVERRIDE_LIB}/bz2.lib" 
    "${CBC_OVERRIDE_LIB}/zlib.lib"
  )
endif()

if (WIN32)
  add_definitions(
    -DCLP_STATIC 
    -DCBC_STATIC 
    -DOSI_STATIC 
    -DCOINUTILS_STATIC
  )
endif()

if (MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Helper libraries
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include
)

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)


# --- 1. JRES Utils (Static) ---
# Contains DateUtils and ZipWriter
add_library(jres_utils STATIC
    src/utils/date_utils.cpp
    src/utils/zip_writer.cpp
)
target_include_directories(jres_utils PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# --- 2. JRES Solver Core (Shared/DLL) ---
add_library(jres_solver SHARED
  src/jres_solver.cpp
  src/jres_solver_impl.cpp
  src/jres_solver_types.cpp
)
if (WIN32)
  target_compile_definitions(jres_solver PRIVATE JRES_SOLVER_EXPORTS)
endif()

target_include_directories(jres_solver PUBLIC 
    ${JRES_SOLVER_INCLUDE_DIR} 
    ${CBC_INCLUDE_DIRS}
)

target_link_libraries(jres_solver PRIVATE 
    nlohmann_json 
    ${CBC_LIBRARIES}
)


# --- 3. Solver CLI ---
add_executable(solver cmd/solver/cli.cpp)
target_include_directories(solver PRIVATE 
    ${CBC_INCLUDE_DIRS} 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)

add_dependencies(solver jres_solver)

if (WIN32)
  target_link_directories(solver PRIVATE 
    ${CMAKE_BINARY_DIR}/Release
  )
endif()

target_link_libraries(solver PRIVATE 
    jres_solver 
    cxxopts 
    nlohmann_json
)


# --- 4. JRES Formatter Logic (Static Lib) ---
# We split this out so we can link it to both the CLI and the Tests
add_library(jres_formatter_lib STATIC
    src/formatter/formatter_core.cpp
)
# Links to utils (for Zip/Date) and Json
target_link_libraries(jres_formatter_lib PUBLIC 
    jres_utils 
    nlohmann_json
)

target_include_directories(jres_formatter_lib PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# --- 5. Formatter CLI ---
add_executable(formatter cmd/formatter/cli.cpp)
target_include_directories(formatter PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)

target_link_libraries(formatter PRIVATE 
    jres_formatter_lib 
    cxxopts
)


# --- 6. Tests ---
enable_testing()
add_subdirectory(test)
