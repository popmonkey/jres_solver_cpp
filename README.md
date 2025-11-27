# JRES Solver

This library can be used to solve for optimal driver and spotter schedules for endurance racing events. It uses the **HiGHS** optimization library.

>[!NOTE]
>this is based on the python JRES Solver https://github.com/popmonkey/jres_solver

## Additional Documentation

* **[Tools](./TOOLS.md)** - releases include some command line tools that use the library
* **[Development](./DEVELOPMENT.md)** - instructions for development of the library

## The Library

**JresSolver** is a C++ library designed to optimize endurance racing schedules. It uses the **HiGHS** Mixed Integer Programming (MIP) solver to assign drivers (and optional spotters) to race stints while satisfying constraints such as fuel usage, maximum drive times, minimum rest periods, and driver availability.

### Integration (C-API)

The library exposes a C-compatible API and can be bound to languages like C, C++, Go, Python, or Rust.

#### Basic Usage Example

```cpp
#include "jres_solver/jres_solver.hpp"

// Configure solver options
JresSolverOptions options;
options.timeLimit = 5;
options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
options.allowNoSpotter = false;
options.optimalityGap = 0.2;
options.quiet = false;

// Solve with race data JSON string
char* resultJson = nullptr;
int status = solve_race_schedule(raceDataJson, options, &resultJson);

// Always free the result
free_solver_result(resultJson);
```

For a complete working example, see [`cmd/solver/cli.cpp`](./cmd/solver/cli.cpp).

#### Solver vs. Diagnostic Mode

- **`solve_race_schedule()`**: Finds an optimal schedule satisfying all constraints. Returns an error if no feasible solution exists.

- **`diagnose_race_schedule()`**: When the solver fails, this runs a relaxed model to identify which constraints are causing the infeasibility.

#### Controlling Solve Time

The solver can take a very long time (or never complete) for complex schedules if not properly constrained. Use `timeLimit` and `optimalityGap` to prevent excessive runtimes:

**Recommended defaults:** `timeLimit = 5` seconds, `optimalityGap = 0.2` (20%)

- **`timeLimit`**: Maximum seconds the solver will run before returning the best solution found. This is a hard stop.

- **`optimalityGap`**: Allows the solver to stop early when it finds a "good enough" solution within this percentage of the theoretical optimum. For example, `0.2` means the solver stops once it finds a solution within 20% of optimal.

**Why a small optimality gap is expensive and unnecessary:**

Mixed Integer Programming problems like race scheduling are NP-hard. The solver may find a good feasible solution quickly (in seconds), but proving that solution is within 1% of optimal can take exponentially longer—hours or even days. For practical scheduling:

- A 20% gap solution is typically excellent and solves in seconds
- A 5% gap might take 10-100x longer with minimal practical benefit
- A 1% gap can be prohibitively expensive, often timing out
- The "optimal" schedule and a 20% suboptimal schedule are often nearly identical in practice—swapping equivalent drivers or spotters

The solver prioritizes hard constraints (rest times, fuel, availability) first. The optimality gap only affects soft preferences like minimizing consecutive stints. A 20% gap on these preferences is imperceptible in real-world use.

-----

### Input JSON Specification

The `raceDataJson` string passed to `solve_race_schedule` must strictly follow this schema.

#### Root Object (`RaceData`)

| Field | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `raceStartUTC` | String | Yes | ISO 8601 timestamp for the start of the race (e.g., `"2023-06-10T14:00:00"`). |
| `durationHours` | Number | Yes | Total length of the race in hours. |
| `avgLapTimeInSeconds`| Number | Yes | Average lap time used to calculate stint duration. |
| `pitTimeInSeconds` | Number | Yes | Time lost during a pit stop. |
| `fuelTankSize` | Number | Yes | Total fuel capacity (units must match `fuelUsePerLap`). |
| `fuelUsePerLap` | Number | Yes | Fuel consumed per lap. |
| `teamMembers` | Array | Yes | List of drivers and spotters (see below). |
| `availability` | Object | Yes | Map of availability constraints (see below). |
| `firstStintDriver` | String | No | Name of the driver forced to take the first stint. |

#### Team Member Object

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | **Required** | Unique identifier for the member. |
| `isDriver` | Boolean | `false` | Can this member drive? |
| `isSpotter` | Boolean | `false` | Can this member spot? |
| `preferredStints` | Integer| `3` | Soft constraint: solver attempts to limit consecutive stints to this number. |
| `minimumRestHours` | Integer| `0` | Hard constraint: Minimum rest time required after a driving shift before driving again. |

#### Availability Map & Time Formatting

The `availability` object maps a **Team Member's Name** to a dictionary of **Time Keys**.

**Important:** The solver discretizes time slots to the **top of the hour**.

  * You must provide availability for every hour the race covers.
  * The keys must be formatted exactly as: `YYYY-MM-DDTHH:00:00.000Z`.
  * If a driver/time pair is missing, the solver assumes the driver is **Available** (Standard) unless specific logic interprets missing keys as unavailable.
      * *Note on current implementation:* The solver checks for `"Unavailable"` explicitly. If the key exists and value is `"Unavailable"`, they cannot drive. If the key is `"Preferred"`, the cost is reduced.

##### Values:

  * `"Preferred"`: The solver is incentivized to schedule the driver here.
  * `"Unavailable"`: The driver is strictly forbidden from being scheduled.
  * (Missing/Other): The driver is available but not preferred.

##### JSON Example

```json
{
  "raceStartUTC": "2024-06-15T15:00:00",
  "durationHours": 24,
  "avgLapTimeInSeconds": 210.5,
  "pitTimeInSeconds": 45.0,
  "fuelTankSize": 100.0,
  "fuelUsePerLap": 3.2,
  "firstStintDriver": "Niki",
  "teamMembers": [
    {
      "name": "Niki",
      "isDriver": true,
      "isSpotter": true,
      "preferredStints": 2,
      "minimumRestHours": 4
    },
    {
      "name": "Alain",
      "isDriver": true,
      "isSpotter": false
    }
  ],
  "availability": {
    "Niki": {
      "2024-06-15T18:00:00.000Z": "Unavailable",
      "2024-06-15T19:00:00.000Z": "Preferred"
    }
  }
}
```

-----

### Output JSON Specification

The function returns a JSON string containing the solution or error details.

#### Success Response

| Field | Type | Description |
| :--- | :--- | :--- |
| `success` | Boolean | Always `true`. |
| `solveDurationSeconds` | Number | Time taken by the C++ solver to reach the solution. |
| `schedule` | Array | List of optimized stint assignments. |
| `raceData` | Object | Echoes the input configuration for verification. |

##### Schedule Entry Object

| Field | Type | Description |
| :--- | :--- | :--- |
| `stint` | Integer | 1-based stint index. |
| `driver` | String | Name of the assigned driver. |
| `spotter` | String | Name of the assigned spotter (if Spotter Mode is active). |

#### Error Response

| Field | Type | Description |
| :--- | :--- | :--- |
| `success` | Boolean | Always `false`. |
| `error` | String | Description of the failure (e.g., "Infeasible model", "Parse error"). |
| `diagnosis` | Array | (Diagnostic Mode Only) List of strings explaining why the schedule is infeasible. |

##### Example Output

```json
{
  "success": true,
  "solveDurationSeconds": 0.45,
  "schedule": [
    { "stint": 1, "driver": "Niki", "spotter": "Alain" },
    { "stint": 2, "driver": "Niki", "spotter": "Alain" },
    { "stint": 3, "driver": "Alain", "spotter": "Niki" }
  ],
  "raceData": { ... }
}
```


---

_Created by popmonkey, Gemini 2.5, Gemini 3.0, and ChatGPT 5.1_
