#!/bin/bash
# Test the formatter's ability to print to stdout

# Path to the formatter executable
FORMATTER="./build/jres_formatter"
INPUT_FILE="data/24h_race.json"

# First, ensure we have a solution file to format. 
# We'll run the solver on the input file and pipe it to a temporary file.
SOLVER="./build/jres_solver"
SOLUTION_FILE="/tmp/test_solution.json"

echo "Running solver to generate solution..."
$SOLVER -i $INPUT_FILE -s sequential -o $SOLUTION_FILE

if [ ! -f $SOLUTION_FILE ]; then
    echo "Error: Solver failed to generate solution file."
    exit 1
fi

echo "Running formatter without -o flag..."
# Run the formatter and capture stdout
OUTPUT=$($FORMATTER -i $SOLUTION_FILE)

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
rm $SOLUTION_FILE
exit 0
