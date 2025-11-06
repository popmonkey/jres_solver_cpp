# JRES Solver (C++ Port)

This library can be used to solve for optimal driver and spotter schedules for endurance racing events.  It uses the COIN-OR Cbc optimization library.

It has been structured as a C-API library (`racesolver`) and a simple CLI client (`solver`) that uses the library.

## Project Structure

```
.
├── include/
│   └── racesolver.hpp      # The public C-API header for the library
├── src/
│   ├── racesolver.cpp      # The C++ library implementation
│   └── main.cpp            # The CLI client implementation
├── lib/
│   ├── cxxopts/            # (Git Submodule) cxxopts header-only library
│   └── json/               # (Git Submodule) nlohmann/json header-only library
├── CMakeLists.txt          # The main build script
└── README.md
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

  * `libracesolver.dylib` (or `.so` on Linux): The shared library.
  * `solver`: The CLI executable.

## Running the CLI Client

The `solver` executable is a client that uses the `racesolver` library.

```
# Run with a file
./solver -i ../data/race_data.json -s integrated

# Pipe from stdin
cat ../data/race_data.json | ./solver -s sequential --allow-no-spotter

# Get help
./solver --help
```
