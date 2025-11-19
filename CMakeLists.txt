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

# macOS + Linux keep your original behavior — do NOT use pkg-config
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

# The final list of required CBC libraries.
# On Linux/macOS, this list uses variables set by find_library.
# On Windows (static build), this list must include all transitive static dependencies.
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

# --- Windows static linking fix ---
if (WIN32)
  add_definitions(
    -DCLP_STATIC
    -DCBC_STATIC
    -DOSI_STATIC
    -DCOINUTILS_STATIC
  )
endif()

# On MSVC, use /MT(/MTd) so it matches the vcpkg static CBC build
if (MSVC)
  # Release: /MT, Debug: /MTd
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# nlohmann/json (Header-only)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/json/single_include
)

# cxxopts (Header-only)
add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)


# --- Define Our Library (DLL/SHARED) ---

add_library(jres_solver SHARED
  src/jres_solver.cpp         # The C-API wrapper
  src/jres_solver_impl.cpp    # The C++ solver logic
  src/jres_solver_types.cpp   # The JSON/Type logic
)

# === CRITICAL FIX: ENABLE SYMBOL EXPORTING & LINKING ===
if (WIN32)
  # CRITICAL: Define the export macro to force the generation of jres_solver.lib
  target_compile_definitions(jres_solver PRIVATE JRES_SOLVER_EXPORTS)
  
  # REMOVE THE OUTPUT DIRECTORY OVERRIDES (rely on standard CMake defaults now)
  # The consumer executable targets will specify the search path.
endif()
# =======================================================

# CBC headers must be PUBLIC so MSVC sees them during compilation
target_include_directories(jres_solver PUBLIC
  ${JRES_SOLVER_INCLUDE_DIR}   # our headers
  ${CBC_INCLUDE_DIRS}          # CBC headers
)

# Link all dependencies to the LIBRARY
target_link_libraries(jres_solver
  PRIVATE
    nlohmann_json
    ${CBC_LIBRARIES}
)

# --- Define Our Executable (The Client) ---

# This creates an executable named "solver"
add_executable(solver
  cmd/solver/cli.cpp
)

# FIX: C1083 - Explicitly add the include path for the executable target.
target_include_directories(solver PRIVATE
  ${CBC_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/cxxopts/include
)

# FIX: LNK1181 - Force build order.
add_dependencies(solver jres_solver)

# FIX: LNK1181 - Tell the linker the absolute path where the .lib file resides.
# Use CMAKE_BINARY_DIR/Release as this is the standard location for the DLL output.
if (WIN32)
  target_link_directories(solver PRIVATE 
    ${CMAKE_BINARY_DIR}/Release
  )
endif()

# Link the EXECUTABLE against our library and its own dependencies
target_link_libraries(solver PRIVATE
  # Links the DLL's import library (jres_solver.lib) using the standard target name.
  jres_solver
  
  # Linking against the INTERFACE library automatically provides the rest of the includes/linkage
  cxxopts

  # Needed by cli.cpp
  nlohmann_json
)

# --- Enable Testing ---
enable_testing()
add_subdirectory(test)
