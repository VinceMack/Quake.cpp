@echo off
echo ==========================================
echo    CleanQuake Clang-Tidy Analyzer
echo ==========================================
echo.

:: Step 1: Wipe the old build folder so we can change the generator
echo [1/4] Deleting old MSBuild cache...
if exist build rmdir /s /q build

:: Step 2: Configure CMake to use Ninja
echo.
echo [2/4] Configuring CMake with Ninja...
cmake -G Ninja -B build
if %ERRORLEVEL% neq 0 (
    echo.
    echo ERROR: Ninja was not found! 
    echo Please run this script from the "x64 Native Tools Command Prompt for VS" 
    echo or install Ninja manually.
    pause
    exit /b
)

:: Step 3: Build the project and capture output
echo.
echo [3/4] Running build and Clang-Tidy analysis...
echo       (This will take longer than a normal build. Please wait...)
cmake --build build > analysis.log 2>&1

:: Step 4: Filter for Clang-Tidy warnings
echo.
echo [4/4] Extracting Clang-Tidy recommendations...
powershell -Command "Select-String -Path analysis.log -Pattern 'warning:.*\[misc-|warning:.*\[clang-analyzer-' | Out-File -FilePath clang_tidy_results.log -Encoding utf8"

echo.
echo Analysis complete! 
code clang_tidy_results.log
pause