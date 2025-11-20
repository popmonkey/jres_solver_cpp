# JRES Solver

This library can be used to solve for optimal driver and spotter schedules for endurance racing events. It uses the COIN-OR Cbc optimization library.

It has been structured as a C-API library (`jres_solver`) and two CLI clients (`solver` and `formatter`) that use the library.

>[!NOTE]
>this is a C++ port and continuation of the python JRES Solver https://github.com/popmonkey/jres_solver

## Project Structure

```
.
├── include/
|   └── jres_solver/        # The public C-API header for the library
├── src/                    # The C++ library implementation
├── lib/
│   ├── cxxopts/            # (Git Submodule) cxxopts header-only library
│   └── json/               # (Git Submodule) nlohmann/json header-only library
├── command/
│   ├── solver/             # The Solver CLI implementation
│   └── formatter/          # The Formatter CLI implementation           
├── command/                # Tests
└── CMakeLists.txt          # The main build script
```

## Bootstrap & Dependencies

This project relies on `cmake` for building and `git` for dependency management (via submodules). It requires `Cbc` (for optimization).

### 1. Clone & Init Submodules

First, clone the repo and initialize the `lib` submodules:

```
git clone git@github.com:popmonkey/jres_solver_cpp.git jres_solver_cpp
cd jres_solver_cpp
git submodule update --init --recursive
```

### 2. Install Dependencies (macOS & Linux)

#### Step A: Install Cbc

**macOS (Homebrew)**
```
brew install cbc
```

**Linux (apt)**
```
sudo apt-get update
sudo apt-get install coinor-cbc coinor-libclp-dev coinor-libcoinutils-dev coinor-libosi-dev
```

## Building the Project

We use a standard out-of-source CMake build.

1.  **Create a build directory:**

    ```
    mkdir build
    cd build
    ```

2.  **Run CMake (Configure):**
    
    **On macOS (Homebrew on Apple Silicon):**
    ```
    cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew"
    ```

    **On Linux/Intel macOS:**
    ```
    cmake ..
    ```

3.  **Run Make (Build):**

    ```
    make
    ```

This will create the products in the `build/` directory:
  * `libjres_solver.dylib`: The shared library.
  * `solver`: The optimization CLI.
  * `formatter`: The output generation CLI.

## Running the Tools

### 1. The Solver
The `solver` ingests JSON data and outputs a raw solution JSON.

```
# Run with a file
./solver -i ../data/race_data.json -s integrated -o solution.json
```

### 2. The Formatter
The `formatter` takes the `solution.json` and converts it into human-readable formats (ZIP of CSVs, single CSV, Text).

```
# Generate a ZIP containing detailed CSV schedules for every member
./formatter -i solution.json -o schedule.zip --format zip

# Generate a single master CSV
./formatter -i solution.json -o schedule.csv --format csv
```
