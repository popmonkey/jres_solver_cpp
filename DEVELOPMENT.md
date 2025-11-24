# JRES Solver

This library can be used to solve for optimal driver and spotter schedules for endurance racing events.  It uses the COIN-OR Cbc optimization library.

It has been structured as a C-API library (`jres_solver`) and a simple CLI client (`jres_solver`) that uses the library.

>[!NOTE]
>this is a C++ port of the python Solver [https://github.com/popmonkey/jres_solver](https://github.com/popmonkey/jres_solver)

## Project Structure

```
.
├── include/
|   └── jres_solver/                # The public C-API header for the library
├── src/                            # The C++ library implementation
|   ├── jres_solver.cpp             # C-API Wrapper and Orchestrator
|   ├── jres_solver_utils.cpp       # Shared utilities and Base Class
|   ├── jres_standard_solver.cpp    # Optimized Strict Solver
|   ├── jres_diagnostic_solver.cpp  # Relaxed Diagnostic Solver
|   └── jres_solver_types.cpp       # JSON serialization logic
├── lib/
│   ├── cxxopts/                    # (Git Submodule) cxxopts header-only library
│   └── json/                       # (Git Submodule) nlohmann/json header-only library
├── cmd/
│   ├── /solver/                    # A CLI solver
│   └── /formatter/                 # A CLI formatter
├── test/                           # Google Test suite
└── CMakeLists.txt                  # The main build script
```

## Bootstrap & Dependencies

This project relies on `cmake` for building and `git` for dependency management (via submodules). The only external binary dependency is the Cbc library.

### 1. Clone & Init Submodules

First, clone the repo and initialize the `lib` submodules:

```
git clone git@github.com:popmonkey/jres_solver_cpp.git jres_solver_cpp
cd jres_solver_cpp
git submodule update --init --recursive
```

### 2. Install Cbc

The COIN-OR Cbc library must be installed on your system.

#### macOS (Homebrew)

This is the easiest method for macOS:

```
brew install cbc
```

#### Linux (apt)

```
sudo apt-get update
sudo apt-get install coinor-cbc coinor-libclp-dev coinor-libcoinutils-dev coinor-libosi-dev
```

*(Note: Package names may vary slightly by distribution).*

#### Windows

This is the most complex path. It's recommended to use a package manager like `vcpkg` to install `cbc` and all its dependencies, or to build from source using MSYS2.

## Building the Project

We use a standard out-of-source CMake build.

1.  **Create a build directory:**

    ```
    mkdir build
    cd build
    ```

2.  **Run CMake (Configure):**
    You must provide a "hint" (the `CMAKE_PREFIX_PATH`) to tell CMake where your Cbc installation lives.

    **On macOS (Homebrew on Apple Silicon):**

    ```
    cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew
    ```

    **On macOS (Homebrew on Intel):**

    ```
    cmake .. -DCMAKE_PREFIX_PATH=/usr/local
    ```

    **On Linux (Standard):**
    (CMake should find the system libraries automatically)

    ```
    cmake ..
    ```

3.  **Run Make (Build):**

    ```
    make
    ```

This will create two main products in the `build/` directory:

  * `libjres_solver.dylib` (or `.so` on Linux): The shared library.
  * `jres_solver`: The CLI executable.

## Running the CLI Client

The `jres_solver` executable is a client that uses the `jres_solver` library.

```
# Run with a file
./jres_solver -i ../data/race_data.json -s integrated

# Pipe from stdin
cat ../data/race_data.json | ./jres_solver -s sequential --allow-no-spotter

# Run diagnostics on a failing schedule
./jres_solver -i ../data/infeasible.json --diagnose

# Get help
./jres_solver --help
```
