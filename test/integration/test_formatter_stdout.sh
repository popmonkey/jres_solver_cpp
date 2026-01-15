#!/bin/bash
# Test the formatter's ability to print to stdout

# Exit on error
set -e

# Path to the executables
BUILD_DIR="build"
FORMATTER="$BUILD_DIR/jres_formatter"
SOLVER="$BUILD_DIR/jres_solver"
INPUT_FILE="data/24h_race.json"
SOLUTION_FILE="/tmp/test_solution_stdout.json"

# Make sure we are in the project root
if [ ! -f "CMakeLists.txt" ]; then
    echo "Error: This script must be run from the project root directory."
    exit 1
fi

# Make sure binaries exist
if [ ! -f "$SOLVER" ]; then
    echo "Error: Solver binary not found at $SOLVER. Build the project first."
    exit 1
fi
if [ ! -f "$FORMATTER" ]; then
    echo "Error: Formatter binary not found at $FORMATTER. Build the project first."
    exit 1
fi

# Cleanup previous runs
rm -f "$SOLUTION_FILE"

echo "Running solver to generate solution..."
$SOLVER -i $INPUT_FILE -s sequential -o $SOLUTION_FILE --quiet

if [ ! -f "$SOLUTION_FILE" ]; then
    echo "Error: Solver failed to generate solution file."
    exit 1
fi

echo "Running formatter without -o flag..."
# Run the formatter and capture stdout
OUTPUT=$($FORMATTER -i "$SOLUTION_FILE")

# Check if the output contains expected strings from the summary report
if [[ "$OUTPUT" == *"--- DRIVER SUMMARY ---"* ]] && [[ "$OUTPUT" == *"--- SCHEDULE ---"* ]]; then
    echo "Success: Formatter output to stdout detected."
else
    echo "Error: Formatter did not print expected summary to stdout."
    echo "Output was:"
    echo "$OUTPUT"
    exit 1
fi

# Clean up
rm "$SOLUTION_FILE"
exit 0
