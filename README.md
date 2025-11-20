# JRES Solver

This library can be used to solve for optimal driver and spotter schedules for endurance racing events. It uses the COIN-OR Cbc optimization library.

It has been structured as a C-API library (`jres_solver`) and two CLI clients (`solver` and `formatter`) that use the library.

>[!NOTE]
>this is based on the python JRES Solver https://github.com/popmonkey/jres_solver

## Additional Documentation

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

-----

This project relies on `cmake` for building and `git` for dependency management (via submodules). It requires `Cbc` (for optimization).

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
  "firstStintDriver": "Alice",
  "teamMembers": [
    {
      "name": "Alice",
      "isDriver": true,
      "isSpotter": true,
      "preferredStints": 2,
      "minimumRestHours": 4
    },
    {
      "name": "Bob",
      "isDriver": true,
      "isSpotter": false
    }
  ],
  "availability": {
    "Alice": {
      "2024-06-15T18:00:00.000Z": "Unavailable",
      "2024-06-15T19:00:00.000Z": "Preferred"
    }
  }
}
```

This project relies on `cmake` for building and `git` for dependency management (via submodules). It requires `Cbc` (for optimization).

### 3. Output JSON Specification

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

### 2. Install Dependencies (macOS & Linux)

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
