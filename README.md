# JRES Solver

This library can be used to solve for optimal driver and spotter schedules for endurance racing events. It uses the **HiGHS** optimization library.

>[!NOTE]
>this is based on the python JRES Solver https://github.com/popmonkey/jres_solver

## Additional Documentation

* **[Tools](./TOOLS.md)** - releases include some command line tools that use the library
* **[Development](./CONTRIBUTING.md)** - instructions for development of the library

## The Library

**JresSolver** is a C++ library designed to optimize endurance racing schedules. It uses the **HiGHS** Mixed Integer Programming (MIP) solver to assign drivers (and optional spotters) to race stints while satisfying constraints such as fuel usage, maximum drive times, minimum rest periods, and driver availability.

### Data Structures

The C-API uses the following structs to pass data to and from the solver.

#### Input Structures

`JresSolverInput` is the main input struct. It contains arrays of the other input structs.

| Field | Type | Description |
| :--- | :--- | :--- |
| `teamMembers` | `JresTeamMember*` | A pointer to an array of team members. |
| `teamMembers_len` | `int` | The number of team members. |
| `availability` | `JresMemberAvailability*` | A pointer to an array of availability information. |
| `availability_len`| `int` | The number of members with availability information. |
| `stints` | `JresStint*` | A pointer to an array of stints. |
| `stints_len`| `int` | The number of stints. |

`JresTeamMember`

| Field | Type | Description |
| :--- | :--- | :--- |
| `name` | `const char*` | Unique identifier for the member. |
| `isDriver` | `int` | `1` if the member can drive, `0` otherwise. |
| `isSpotter` | `int` | `1` if the member can spot, `0` otherwise. |
| `maxStints`| `int` | Hard constraint: Maximum number of consecutive stints a member can perform. |
| `minimumRestHours` | `int` | Hard constraint: Minimum rest time required after a driving shift before driving again. |
| `tzOffset` | `double` | Timezone offset in hours from UTC. |

`JresStint`

| Field | Type | Description |
| :--- | :--- | :--- |
| `id` | `int` | Unique identifier for the stint. |
| `startTime` | `const char*` | ISO 8601 timestamp for the start of the stint. |
| `endTime` | `const char*` | ISO 8601 timestamp for the end of the stint. |

`JresAvailabilityEntry` & `JresMemberAvailability`

These structs are used to represent the availability of team members.

| Struct | Field | Type | Description |
| :--- | :--- | :--- | :--- |
| `JresAvailabilityEntry` | `time` | `const char*` | An ISO 8601 timestamp for the hour slot. |
| | `availability` | `JresAvailability` | The availability for that hour (`JRES_AVAILABILITY_UNAVAILABLE`, `JRES_AVAILABILITY_AVAILABLE`, `JRES_AVAILABILITY_PREFERRED`). |
| `JresMemberAvailability`| `name` | `const char*` | The name of the team member. |
| | `availability` | `JresAvailabilityEntry*` | A pointer to an array of availability entries. |
| | `availability_len` | `int` | The number of availability entries for this member. |


#### Output Structures

`JresSolverOutput` is the main output struct.

| Field | Type | Description |
| :--- | :--- | :--- |
| `schedule` | `JresScheduleEntry*` | A pointer to an array of schedule entries. |
| `schedule_len` | `int` | The number of schedule entries. |
| `diagnosis` | `const char**` | An array of strings with diagnostic information. Empty on success. |
| `diagnosis_len` | `int` | The number of diagnosis strings. |
| `teamMembers` | `JresTeamMember*` | A pointer to an array of team members, including their tzOffset. |
| `teamMembers_len` | `int` | The number of team members. |

`JresScheduleEntry`

| Field | Type | Description |
| :--- | :--- | :--- |
| `stintId` | `int` | The ID of the stint. |
| `driver` | `const char*` | Name of the assigned driver. |
| `spotter` | `const char*` | Name of the assigned spotter. |

#### Basic Usage Example

```cpp
#include "jres_solver/jres_solver.hpp"

// Configure solver options
JresSolverOptions options;
options.timeLimit = 5;
options.spotterMode = JRES_SPOTTER_MODE_INTEGRATED;
options.allowNoSpotter = false;
options.optimalityGap = 0.2;

// Create input struct from JSON
JresSolverInput* input = jres_input_from_json(raceDataJson);

// Solve the schedule
JresSolverOutput* output = solve_race_schedule(input, &options);

// Free the memory
free_jres_solver_input(input);
free_jres_solver_output(output);
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

### JSON Helper Functions

The library provides helper functions to convert between the C-API structs and JSON strings.

#### `jres_input_from_json(const char* jsonData)`
Parses a JSON string and returns a `JresSolverInput*`. The caller is responsible for freeing the memory using `free_jres_solver_input`.

#### `jres_output_to_json(const JresSolverOutput* output)`
Converts a `JresSolverOutput` struct to a JSON string. The caller is responsible for freeing the memory using `free_json_string`.

### Input JSON Specification

The `raceDataJson` string passed to `jres_input_from_json` must strictly follow this schema.

#### Root Object

| Field | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `teamMembers` | Array | Yes | List of drivers and spotters (see below). |
| `availability` | Object | Yes | Map of availability constraints (see below). |
| `stints` | Array | Yes | List of pre-defined race stints (see below). |

#### Stint Object

| Field | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `id` | Integer | Yes | Unique identifier for the stint. |
| `startTime` | String | Yes | ISO 8601 timestamp for the start of the stint. |
| `endTime` | String | Yes | ISO 8601 timestamp for the end of the stint. |

#### Team Member Object

| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | **Required** | Unique identifier for the member. |
| `isDriver` | Boolean | `true` | Can this member drive? |
| `isSpotter` | Boolean | `false` | Can this member spot? |
| `maxStints` | Integer| `1` | Hard constraint: Maximum number of consecutive stints a member can perform. |
| `minimumRestHours` | Integer| `0` | Hard constraint: Minimum rest time required after a driving shift before driving again. |
| `tzOffset` | Number | `0.0` | Timezone offset in hours from UTC. |

#### Availability Map & Time Formatting

The `availability` object maps a **Team Member's Name** to a dictionary of **Time Keys**.

**Important:** The solver discretizes time slots to the **top of the hour**.

  * You must provide availability for every hour the race covers.
  * The keys must be formatted exactly as: `YYYY-MM-DDTHH:00:00.000Z`.
  * If a driver/time pair is missing, the solver assumes the driver is **Available**.

##### Values:

  * `"Preferred"`: The solver is incentivized to schedule the driver here.
  * `"Unavailable"`: The driver is strictly forbidden from being scheduled.
  * `"Available"`: The driver is available but not preferred.

##### JSON Example

```json
{
  "teamMembers": [
    {
      "name": "Niki",
      "isDriver": true,
      "isSpotter": true,
      "maxStints": 2,
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
  },
  "stints": [
    { "id": 1, "startTime": "2024-06-15T15:00:00Z", "endTime": "2024-06-15T16:00:00Z" },
    { "id": 2, "startTime": "2024-06-15T16:00:00Z", "endTime": "2024-06-15T17:00:00Z" }
  ]
}
```

### Output JSON Specification

The `jres_output_to_json` function returns a JSON string containing the solution or error details.

#### Success Response

| Field | Type | Description |
| :--- | :--- | :--- |
| `schedule` | Array | List of optimized stint assignments. |
| `diagnosis`| Array | List of strings with diagnostic information. Empty on success. |
| `stats` | Object | Solver performance and complexity metrics. |
| `teamMembers` | Array | List of team members and their properties. |

##### Stats Object

| Field | Type | Description |
| :--- | :--- | :--- |
| `modelColumns`| Integer | The number of columns in the solver model. |
| `modelRows` | Integer | The number of rows in the solver model. |
| `searchNodes` | Integer | The number of nodes explored by the solver. |
| `finalGap` | Number | The final optimality gap of the solution. |
| `setupDurationMs`| Number | The time taken to set up the model in milliseconds. |
| `driverSolveDurationMs` | Number | The time taken to solve for the drivers in milliseconds. |
| `spotterSolveDurationMs` | Number | The time taken to solve for the spotters in milliseconds (sequential mode only). |

##### Schedule Entry Object

| Field | Type | Description |
| :--- | :--- | :--- |
| `stintId` | Integer | The ID of the stint. |
| `driver` | String | Name of the assigned driver. |
| `spotter` | String | Name of the assigned spotter (if Spotter Mode is active). |

#### Error Response

When the solver fails, the `schedule` array will be empty, and the `diagnosis` array will contain one or more strings explaining the failure.

##### Example Output

```json
{
  "schedule": [
    { "stintId": 1, "driver": "Niki", "spotter": "Alain" },
    { "stintId": 2, "driver": "Niki", "spotter": "Alain" },
    { "stintId": 3, "driver": "Alain", "spotter": "Niki" }
  ],
  "diagnosis": []
}
```

---

_Created by popmonkey, Gemini 2.5, Gemini 3.0, and ChatGPT 5.1_
