@echo off
echo ==========================================
echo    CleanQuake Build and Log Generator
echo ==========================================
echo.

:: Step 0: Remove any existing log files to avoid confusion
if exist build.log del build.log
if exist clean_build.log del clean_build.log

:: Step 1: Force a clean build so the linker runs on all files again
echo [1/4] Cleaning previous build...
cmake --build build --target clean

:: Step 2: Build the project and redirect stdout and stderr to a file
echo.
echo [2/4] Building project and capturing output...
echo       (This may take a minute. Please wait, no text will print here...)
cmake --build build > build.log 2>&1

:: Step 3: Filter the log dynamically based on the src/ folder
echo.
echo [3/4] Filtering log for all warnings, errors, and relevant discarded code...
powershell -Command "$srcNames = (Get-ChildItem -Path '%~dp0src' -File).BaseName -join '|'; Select-String -Path build.log -Pattern 'Discarded|warning|error' | ForEach-Object { $_.Line } | Where-Object { $d = ($_ -match 'Discarded' -and $_ -match ('(' + $srcNames + ').*\.(o|obj)') -and $_ -notmatch '\.lib|SDL2'); $w = ($_ -match 'warning|error' -and $_ -notmatch 'SDL2|LIBCMT|msvcrt|OLDNAMES|uuid\.lib|ws2_32\.lib|winmm\.lib|kernel32\.lib'); $d -or $w } | Out-File -FilePath clean_build.log -Encoding utf8"

:: Step 4: Finish and open the log
echo.
echo [4/4] Build and filter complete! 
echo       Outputs successfully saved to build.log and clean_build.log
echo.

:: Automatically open both the raw log and the clean warnings log in VS Code
code build.log clean_build.log

pause