# JRES Endurance Tools: Usage Guide

This suite consists of two command-line tools designed to generate and format driver schedules for endurance racing events.

> [!NOTE]
Currently the easiest way to generate input for the tools (and library) is to use [the JRES Availability Planner spreadsheet](https://docs.google.com/spreadsheets/d/1jI2mtS0R8dtT7gnAtxIkMfLRcRnqWuXaFc0xvM96Smw/edit?usp=sharing)

## Platform Support

These tools run on **Linux**, **macOS**, and **Windows**.

### MacOS Specifics

In addition to the download, the solver binaries are also available via Homebrew:

```sh
brew tap popmonkey/jres_solver
brew install jres_solver
```

---

## Solver (`jres_solver`)

The **Solver** is the core engine. It accepts raw race data (track info, driver constraints, car specs) and calculates the optimal driver schedule.

> [!TIP]
> See the [README](./README.md#controlling-solve-time) for guidance on controlling solve time and understanding the difference between [Solver vs. Diagnostic Mode](./README.md#solver-vs-diagnostic-mode).

### Usage

```sh
# Linux / macOS
./jres_solver [options]

# Windows
jres_solver.exe [options]
```

### Input/Output Behavior

* **Input:** Accepts a JSON file via the `-i` flag. If no input flag is provided, it reads from **Standard Input (stdin)**.
* **Output:** Prints the schedule summary to **stdout** (unless `-q` is used). To save the raw solution for the Formatter, use the `-o` flag.

### Options

| Flag | Long Flag            | Description                                                                                   | Default |
| :--- | :------------------- | :-------------------------------------------------------------------------------------------- | :------ |
| `-i` | `--input`            | Path to the race data `.json` file. Reads from `stdin` if omitted.                            | `stdin` |
| `-o` | `--output`           | Path to save the calculated schedule (JSON). **Required for the Formatter.** | `stdout`|
| `-t` | `--time-limit`       | Maximum time (in seconds) to let the optimizer run.                                           | `30`    |
| `-q` | `--quiet`            | Suppress INFO logs and the printed schedule summary.                                          | `false` |
| `-s` | `--spotter-mode`     | Strategy for assigning spotters. Options: `none`, `integrated`, `sequential`.                 | `none`  |
|      | `--allow-no-spotter` | Allow specific stints to have no spotter assigned (if spotter mode is active).                | `false` |
| `-g` | `--optimality-gap`   | Stop solver when the solution is within this gap of perfection (e.g., `0.01` for 1%).         | `0.0`   |
| `-d` | `--diagnose`         | Run in **Diagnostic Mode** to explain why a schedule is infeasible.                           | `false` |
| `-h` | `--help`             | Print usage instructions.                                                                     |         |

### Spotter Modes

#### Integrated Mode (`JRES_SPOTTER_MODE_INTEGRATED`)
This mode solves for drivers and spotters simultaneously within a single Mixed-Integer Linear Programming (MILP) model.

* **How it works:** The solver adds both driver and spotter variables to the same mathematical problem instance.
* **Advantage:** This is the most optimal method. The solver can adjust the *driver* schedule to accommodate *spotter* availability (and vice-versa) to find the best global solution.

#### Sequential Mode (`JRES_SPOTTER_MODE_SEQUENTIAL`)
This mode utilizes a two-stage approach, prioritizing the driver schedule first.

* **How it works:**
    1.  The library solves the driver schedule completely using the main model.
    2.  It then creates a **separate** solver instance specifically for spotters treating the driver schedule as an availability map.
* **Advantage:** This ensures that driving duties always take priority and are optimal for the drivers, treating spotting as a secondary task to be filled with whoever is left.

#### None (`JRES_SPOTTER_MODE_NONE`)
This mode simply disables spotter scheduling entirely. The logic for generating spotter constraints is skipped, and the output JSON will exclude spotter assignments.

#### Additional Configuration: "Allow No Spotter"
For both **Integrated** and **Sequential** modes, this option modifies the coverage constraints.
* **If False (Default):** The solver requires exactly one spotter per stint. If no one is available, the schedule is deemed infeasible.
* **If True:** The solver allows gaps in the spotter schedule if no valid spotter can be found.

### Examples

**Basic Run:**
Solve a race configuration and save the result for formatting.

```sh
./jres_solver -i race_config.json -o solution.json
```

**Diagnostic Run:**
If a schedule fails to solve, run with `-d` to get a plain English explanation of the blockers (e.g., "Driver A violated minimum rest").

```sh
./jres_solver -i race_config.json --diagnose
```

**With Spotter Scheduling:**
Use `integrated` spotter logic with a reasonable optimality gap for faster results.

```sh
./jres_solver -i race_config.json -o solution.json --spotter-mode integrated --optimality-gap 0.2
```

**Extended Solve Time:**
For complex schedules, allow more time while maintaining a practical optimality gap.

```sh
./jres_solver -i race_config.json -o solution.json -t 30 --optimality-gap 0.2
```

**Pipeline Usage:**
Pipe a JSON generator directly into the solver.

```sh
# Linux / macOS
cat race_data.json | ./jres_solver -o solution.json

# Windows PowerShell
Get-Content race_data.json | .\jres_solver.exe -o solution.json
```

---

## Formatter (`jres_formatter`)

The **Formatter** takes the raw JSON solution produced by the Solver and converts it into human-readable files or importable data formats.

### Usage

```sh
# Linux / macOS
./jres_formatter -i <solution.json> -o <output_path> [options]

# Windows
jres_formatter.exe -i <solution.json> -o <output_path> [options]
```

### Options

| Flag | Long Flag | Description | Required | Default |
| :--- | :--- | :--- | :--- | :--- |
| `-i` | `--input` | Path to the solved schedule JSON (output from `jres_solver`). | **Yes** | |
| `-o` | `--output` | Path for the resulting file (e.g., `schedule.zip`). | **Yes** | |
| `-f` | `--format` | The desired output format (`zip`, `csv`, `txt`). If omitted, format is determined by output filename extension. | No | *Auto* |
| `-h` | `--help` | Print usage instructions. | No | |

### Supported Formats

* **`csv`**: Creates a single master schedule CSV.
* **`txt`**: Creates a human-readable text summary.
* **`zip`**: Creates a compressed archive containing multiple CSV views (e.g., per-driver schedules, team overview, plus the text summary).

### Examples

**Generate a ZIP package:**

```sh
./jres_formatter -i solution.json -o race_schedule.zip
```

**Generate a Text Summary:**

```sh
./jres_formatter -i solution.json -o summary.txt
```

---

## Complete Workflow Example

Here is how you would go from a raw configuration file to a final distributable ZIP file for your team.

### Linux / macOS

```sh
# Step 1: Solve the schedule (allowing 30 seconds with 20% optimality gap)
./jres_solver -i race_24h_config.json -o solved_schedule.json -t 30 -g 0.2

# Step 2: Format the solution into a ZIP file
./jres_formatter -i solved_schedule.json -o team_schedule.zip
```

### Windows (Command Prompt)

```cmd
REM Step 1: Solve
jres_solver.exe -i race_24h_config.json -o solved_schedule.json -t 30 -g 0.2

REM Step 2: Format
jres_formatter.exe -i solved_schedule.json -o team_schedule.zip
```
