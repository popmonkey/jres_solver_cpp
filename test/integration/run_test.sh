#!/bin/bash

# Exit on error
set -e

# Set paths
BUILD_DIR="build"
TEST_DATA="data/short_race.json"
SOLUTION_JSON="$BUILD_DIR/solution.json"
SUMMARY_TXT="$BUILD_DIR/summary.txt"
SOLVER_BIN="$BUILD_DIR/jres_solver"
FORMATTER_BIN="$BUILD_DIR/jres_formatter"

# Make sure we are in the project root
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: This script must be run from the project root directory."
    exit 1
fi

# Make sure binaries exist
if [ ! -f "$SOLVER_BIN" ]; then
    echo "Error: Solver binary not found at $SOLVER_BIN. Build the project first."
    exit 1
fi
if [ ! -f "$FORMATTER_BIN" ]; then
    echo "Error: Formatter binary not found at $FORMATTER_BIN. Build the project first."
    exit 1
fi


# Cleanup previous runs
rm -f "$SOLUTION_JSON" "$SUMMARY_TXT"

echo "Running solver..."
# Step 1: Run solver
"$SOLVER_BIN" \
    -i "$TEST_DATA" \
    -s integrated \
    -o "$SOLUTION_JSON" --quiet

echo "Running formatter..."
# Step 2: Run formatter
"$FORMATTER_BIN" \
    -i "$SOLUTION_JSON" \
    -o "$SUMMARY_TXT"

echo "Verifying output..."
# Step 3: Verify output
if ! grep -q -e "--- DRIVER SUMMARY ---" "$SUMMARY_TXT"; then
    echo "FAIL: Did not find '--- DRIVER SUMMARY ---' in summary."
    exit 1
fi

if ! grep -q -e "--- SPOTTER SUMMARY ---" "$SUMMARY_TXT"; then
    echo "FAIL: Did not find '--- SPOTTER SUMMARY ---' in summary."
    exit 1
fi

if ! grep -q -e "--- SCHEDULE ---" "$SUMMARY_TXT"; then
    echo "FAIL: Did not find '--- SCHEDULE ---' in summary."
    exit 1
fi

if ! grep -q -e "--- ITINERARIES ---" "$SUMMARY_TXT"; then
    echo "FAIL: Did not find '--- ITINERARIES ---' in summary."
    exit 1
fi

echo "All checks passed for file output mode."

# Cleanup
rm "$SOLUTION_JSON" "$SUMMARY_TXT"

echo "Running stdout formatter test..."
./test/integration/test_formatter_stdout.sh

echo "Integration test passed!"
exit 0
