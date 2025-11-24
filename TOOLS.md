# JRES Endurance Tools: Usage Guide

This suite consists of two command-line tools designed to generate and format driver schedules for endurance racing events.

## Platform Support

These tools run on **Linux**, **macOS**, and **Windows**.

### Windows Specific Requirement

On Windows, the `jres_solver` application is built as a thin executable that relies on a shared library. You must ensure the following DLL is present in the same directory as the executable (or in your system `%PATH%`):

* **`jres_solver.dll`**

The `jres_formatter` tool is statically linked and does not require this DLL.

---

## 1. Solver (`jres_solver`)

The **Solver** is the core engine. It accepts raw race data (track info, driver constraints, car specs) and calculates the optimal driver schedule.

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

**Advanced Optimization:**
Run for up to 5 minutes (`300s`), use `integrated` spotter logic, and allow a 1% margin of error for faster results.

```sh
./jres_solver -i race_config.json -o solution.json -t 300 --spotter-mode integrated --optimality-gap 0.01
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

## 2. Formatter (`jres_formatter`)

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
| `-f` | `--format` | The desired output format. Options: `zip`, `csv`, `txt`. | No | `zip` |
| `-h` | `--help` | Print usage instructions. | No | |

### Supported Formats

* **`zip`**: Creates a compressed archive containing multiple CSV views (e.g., per-driver schedules, team overview).
* **`csv`**: Creates a single master schedule CSV.
* **`txt`**: Creates a human-readable text summary.

### Examples

**Generate a ZIP package (Default):**

```sh
./jres_formatter -i solution.json -o race_schedule.zip
```

**Generate a Text Summary:**

```sh
./jres_formatter -i solution.json -o summary.txt -f txt
```

---

## Complete Workflow Example

Here is how you would go from a raw configuration file to a final distributable ZIP file for your team.

### Linux / macOS

```sh
# Step 1: Solve the schedule (allowing 60 seconds for calculation)
./jres_solver -i race_24h_config.json -o solved_schedule.json -t 60

# Step 2: Format the solution into a ZIP file
./jres_formatter -i solved_schedule.json -o team_schedule.zip -f zip
```

### Windows (Command Prompt)

```cmd
REM Ensure jres_solver.dll is in the current folder

REM Step 1: Solve
jres_solver.exe -i race_24h_config.json -o solved_schedule.json -t 60

REM Step 2: Format
jres_formatter.exe -i solved_schedule.json -o team_schedule.zip -f zip
```
