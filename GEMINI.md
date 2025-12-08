# Gemini Project: JRES Solver C++

This document provides instructions for understanding, building, and contributing to the JRES Solver C++ project.

## Project Overview

JRES Solver is a C++ library designed to optimize endurance racing schedules. It uses the HiGHS Mixed Integer Programming (MIP) solver to assign drivers and optional spotters to race stints while satisfying various constraints.

The project is structured as a C-API library (`libjres_solver.a`) with two command-line interface (CLI) clients:
*   `jres_solver`: A tool that uses the library to solve race schedules.
*   `jres_formatter`: A tool to format the output of the solver.

The library provides helper functions to convert between its C-API data structures and JSON, making it easier to integrate with other systems.

### Public headers

The library exposes its API via headers in `include/jres_solver/`. Designed for seamless Foreign Function Interface (FFI) integration with languages like Python, Ruby, and Go, these headers are strictly:
* **C-Compatible:** All exported symbols utilize extern "C" linkage.
* **Self-Contained:** The interface uses standard C types and opaque handles only, ensuring no dependency on the C++ Standard Library (STL) at the boundary.
* **Standalone:** Each header is fully self-sufficient and requires no prior includes.

## Building and Running

This project uses CMake for building.

### Dependencies

*   **CMake (version 3.15+)**
*   **Git** (for managing submodules)
*   **HiGHS Optimization Solver**: This must be installed on your system.
    *   **macOS (Homebrew):** `brew install highs`
    *   **Linux:** Build from source (see `CONTRIBUTING.md`).
    *   **Windows (vcpkg):** `vcpkg install highs:x64-windows-static`

### Build Steps

1.  **Clone the repository and initialize submodules:**
    ```bash
    git clone <repository_url> jres_solver_cpp
    cd jres_solver_cpp
    git submodule update --init --recursive
    ```

2.  **Configure the project with CMake:**
    ```bash
    mkdir build
    cd build
    # Add platform-specific flags if needed, e.g., for macOS with Homebrew:
    # cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew
    cmake ..
    ```

3.  **Build the project:**
    ```bash
    cmake --build .
    ```
    This will generate the library and executables in the `build/` directory.

### Running Tests

The project uses GoogleTest for its test suite. To run the tests, execute the following command from the `build` directory:

```bash
ctest
```

### Running the CLI Tools

The compiled executables are located in the `build/` directory.

**Solver (`jres_solver`):**

```bash
# Run with an input file
./jres_solver -i ../data/short_race.json -s integrated

# Pipe data from stdin and output to a file
cat ../data/24h_race.json | ./jres_solver -s sequential -o /tmp/24_race_solution.json

# Run diagnostics on an infeasible schedule
./jres_solver -i ../data/short_race_no_solution.json --diagnose
```

**Formatter (`jres_formatter`):**

The formatter takes the JSON output from the solver and can generate different report formats.

```bash
# Load a solution and generate a txt summary
./jres_formatter -i /tmp/24_race_solution.json -o /tmp/summary.txt
```

## Development Conventions

*   **Build System:** The project uses CMake for building and configuration.
*   **Dependencies:** C++ library dependencies are managed as Git submodules (`cxxopts`, `nlohmann/json`). The HiGHS solver is an external dependency.
*   **Testing:** The test suite is built with GoogleTest and run via CTest. New tests should be added to the `test/` directory.
*   **API Design:** The core logic is exposed as a C-API for wider compatibility. Helper functions are provided for JSON serialization and deserialization.
*   **Code Style:** The codebase is written in C++. Please follow the existing coding style when contributing.

# MODEL INSTRUCTIONS
- **Verbosity:** Low. Do not explain the code unless asked. Just output the diff or the file.
- **Reasoning:** Perform deep reasoning internally, but output only the final solution.
- **Execution:** Don't stage commits or otherwise try to manage the repo.
