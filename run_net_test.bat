@echo off
echo ===================================================
echo Quake.cpp Multi-Client Networking Test Launcher
echo ===================================================

echo.
echo [1/4] Running Pre-Test System Check...
echo ---------------------------------------------------

:: Terminate any existing running Quake instances
echo  - Terminating old Quake.cpp.exe processes...
taskkill /F /T /IM Quake.cpp.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul

:: Port Collision ^& Zombie Check
set MAX_CLEANUP_ATTEMPTS=5
set CLEANUP_ATTEMPT=0

:pretest_port_check
set PORT_BUSY=0

netstat -an -p UDP | findstr ":26000" >nul 2>&1
if not errorlevel 1 set PORT_BUSY=1
netstat -an -p UDP | findstr ":26001" >nul 2>&1
if not errorlevel 1 set PORT_BUSY=1
netstat -an -p UDP | findstr ":26002" >nul 2>&1
if not errorlevel 1 set PORT_BUSY=1

if "%PORT_BUSY%"=="1" (
    set /a CLEANUP_ATTEMPT+=1
    echo  - [WARNING] Detected active process on test ports! Force-killing zombie processes...
    taskkill /F /T /IM Quake.cpp.exe >nul 2>&1
    ping -n 2 127.0.0.1 >nul
    goto pretest_port_check
)

echo  - [OK] All test ports (26000, 26001, 26002) are verified free and ready.
echo ---------------------------------------------------
echo.

set EXE_PATH=.\build\Quake.cpp.exe
if not exist %EXE_PATH% (
    set EXE_PATH=.\build\Debug\Quake.cpp.exe
)

if not exist %EXE_PATH% (
    echo [ERROR] Could not find Quake.cpp.exe in build or build\Debug directory.
    echo Please build the project first using: cmake --build build
    pause
    exit /b 1
)

echo [2/4] Starting Dedicated Server on port 26000 (Map: e1m1)...
start "Quake Server (Port 26000)" %EXE_PATH% -dedicated 4 -port 26000 +map e1m1

echo Waiting for Server to finish launching and listen on port 26000...
set MAX_TRIES=15
set ATTEMPT=0

:wait_server
set /a ATTEMPT+=1
if %ATTEMPT% GTR %MAX_TRIES% (
    echo [ERROR] Server failed to start listening on port 26000 after %MAX_TRIES% seconds.
    pause
    exit /b 1
)
netstat -an -p UDP | findstr ":26000" >nul 2>&1
if errorlevel 1 (
    ping -n 2 127.0.0.1 >nul
    goto wait_server
)

echo [SUCCESS] Server is active and listening on port 26000!
:: Brief delay to ensure map loading and entity initialization finish
ping -n 2 127.0.0.1 >nul

echo [3/4] Starting Client 1 (Player1) on port 26001...
start "Quake Client 1 (Player1)" %EXE_PATH% -port 26001 +name "Player1" +connect 127.0.0.1:26000

ping -n 2 127.0.0.1 >nul

echo [4/4] Starting Client 2 (Player2) on port 26002...
start "Quake Client 2 (Player2)" %EXE_PATH% -port 26002 +name "Player2" +connect 127.0.0.1:26000

echo ===================================================
echo Server and 2 Clients launched successfully!
echo ===================================================
