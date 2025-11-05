# JRES Race Solver (C++)
A C++ port of the Python-based endurance race scheduling solver, designed for high performance and portability.This project uses CMake for building and links against the COIN-OR Cbc (Branch and Cut) optimization library.Core RequirementsBefore building, you will need:GitCMake (v3.15 or later)A C++17 compliant compiler (e.g., AppleClang, GCC, MSVC)Building the SolverThe project includes header-only libraries (cxxopts, nlohmann/json) as Git submodules. The first time you clone the repository, or any time you pull updates, you must initialize them:# Clone the repository
git clone [YOUR_REPO_URL]
cd jres_solver_cpp

# Initialize and pull the submodules (cxxopts, nlohmann/json)
git submodule update --init --recursive
After that, follow the instructions for your platform.macOS (with Homebrew)This is the simplest setup, as Homebrew manages all paths.Install Dependencies:brew install cmake cbc
Configure (VSCode):This is the recommended method.Install the "CMake Tools" extension.Open the project folder in VSCode.The .vscode/settings.json file in this repository will automatically tell CMake to use the Homebrew path.When prompted, select a Kit (e.g., "Clang").The project will configure automatically. All IntelliSense errors will be resolved.Configure & Build (Terminal):If you prefer the command line, you can pass the Homebrew prefix manually.# Clean any old build
rm -rf build

# Configure, hinting CMake at the Homebrew path
cmake -S . -B build -DCMAKE_PREFIX_PATH=$(brew --prefix)

# Build the 'solver' executable
cmake --build build
Linux (e.g., Ubuntu/Debian)Linux package managers typically place headers and libraries in standard system paths, so no "hint" is required.Install Dependencies:sudo apt-get update
sudo apt-get install build-essential g++ cmake libcbc-dev
(Note: libcbc-dev should provide all necessary Cbc, Osi, Clp, and CoinUtils headers and libraries.)Configure & Build (Terminal):rm -rf build
cmake -S . -B build
cmake --build build
Windows (with Visual Studio & vcpkg)Windows requires using the vcpkg package manager to install Cbc and passing its "toolchain file" to CMake.Install Core Tools:Install Visual Studio 2022 (or 2019) with the "Desktop development with C++" workload.Install Git and CMake.Install vcpkg and Cbc:(Run these commands in PowerShell or CMD)# Clone vcpkg to a permanent location (e.g., C:\src\vcpkg)
git clone [https://github.com/microsoft/vcpkg.git](https://github.com/microsoft/vcpkg.git) C:\src\vcpkg
.\C:\src\vcpkg\bootstrap-vcpkg.bat

# Install the Cbc library for 64-bit Windows
.\C:\src\vcpkg\vcpkg install cbc:x64-windows
Configure & Build (Terminal):The key is telling CMake to use the vcpkg toolchain file.# Clean any old build
rm -rf build

# Configure, pointing to the vcpkg toolchain
# (Adjust the path to your vcpkg install)
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake

# Build the 'solver.exe' executable (in Release mode)
cmake --build build --config Release
Running the SolverAfter building, the executable will be located at:./build/solver (on macOS / Linux)./build/Release/solver.exe (on Windows, if using the command above)