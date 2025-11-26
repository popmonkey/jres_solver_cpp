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

#### Header: `jres_solver.h`

```cpp
// Structure defining solver configuration
struct JresSolverOptions {
    int timeLimit;               // Max runtime in seconds (e.g., 30)
    JresSpotterMode spotterMode; // 0=None, 1=Integrated, 2=Sequential
    bool allowNoSpotter;         // If true, stints can go without a spotter
    double optimalityGap;        // Stop when solution is within this % of optimal (e.g., 0.05)
    bool quiet;                  // If true, suppresses stdout logging
};

// Enum for spotter modes
enum JresSpotterMode {
    JRES_SPOTTER_MODE_NONE = 0,       // No Spotter schedule is solved for
    JRES_SPOTTER_MODE_INTEGRATED = 1, // Driver and Spotter schedule is solved together
    JRES_SPOTTER_MODE_SEQUENTIAL = 2  // Driver schedule is prioritized
};

// Main Solver Function
// Returns 0 on success, -1 on failure.
// outputJson is allocated by the library and must be freed by the caller.
int solve_race_schedule(const char* raceDataJson,
                        const JresSolverOptions& options,
                        char** outputJson);

// Diagnostic Solver Function
// Runs a relaxed model to identify why a schedule is infeasible.
// Returns 0 on success (diagnosis complete), -1 on internal failure.
// outputJson will contain a "diagnosis" array with string explanations.
int diagnose_race_schedule(const char* raceDataJson,
                           const JresSolverOptions& options,
                           char** outputJson);

// Memory Cleanup
void free_solver_result(char* resultJson);
```

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
