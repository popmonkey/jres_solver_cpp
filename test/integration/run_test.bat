@echo off
setlocal

set BUILD_DIR=build
set TEST_DATA=data\short_race.json
set SOLUTION_JSON=%BUILD_DIR%\solution.json
set SUMMARY_TXT=%BUILD_DIR%\summary.txt
set SOLVER_BIN=%BUILD_DIR%\Release\jres_solver.exe
set FORMATTER_BIN=%BUILD_DIR%\Release\jres_formatter.exe

rem Make sure we are in the project root
if not exist "CMakeLists.txt" (
    echo Error: This script must be run from the project root directory.
    exit /b 1
)

rem Make sure binaries exist
if not exist "%SOLVER_BIN%" (
    echo Error: Solver binary not found at %SOLVER_BIN%. Build the project first.
    exit /b 1
)
if not exist "%FORMATTER_BIN%" (
    echo Error: Formatter binary not found at %FORMATTER_BIN%. Build the project first.
    exit /b 1
)

rem Cleanup previous runs
del /f /q %SOLUTION_JSON% %SUMMARY_TXT% > nul 2>&1

echo Running solver...
rem Step 1: Run solver
%SOLVER_BIN% -i %TEST_DATA% -s integrated -o %SOLUTION_JSON% --quiet
if %errorlevel% neq 0 (
    echo FAIL: Solver failed.
    exit /b 1
)

echo Running formatter...
rem Step 2: Run formatter
%FORMATTER_BIN% -i %SOLUTION_JSON% -o %SUMMARY_TXT%
if %errorlevel% neq 0 (
    echo FAIL: Formatter failed.
    exit /b 1
)


echo Verifying output...
rem Step 3: Verify output
findstr /c:"--- DRIVER SUMMARY ---" %SUMMARY_TXT% > nul
if %errorlevel% neq 0 (
    echo FAIL: Did not find "--- DRIVER SUMMARY ---" in summary.
    exit /b 1
)

findstr /c:"--- SPOTTER SUMMARY ---" %SUMMARY_TXT% > nul
if %errorlevel% neq 0 (
    echo FAIL: Did not find "--- SPOTTER SUMMARY ---" in summary.
    exit /b 1
)

findstr /c:"--- SCHEDULE ---" %SUMMARY_TXT% > nul
if %errorlevel% neq 0 (
    echo FAIL: Did not find "--- SCHEDULE ---" in summary.
    exit /b 1
)

findstr /c:"--- ITINERARIES ---" %SUMMARY_TXT% > nul
if %errorlevel% neq 0 (
    echo FAIL: Did not find "--- ITINERARIES ---" in summary.
    exit /b 1
)

echo All checks passed.

rem Cleanup
del /f /q %SOLUTION_JSON% %SUMMARY_TXT% > nul 2>&1

echo Integration test passed!
exit /b 0