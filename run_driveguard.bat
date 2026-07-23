@echo off
setlocal
cd /d "%~dp0"

set "DRIVEGUARD_ROOT=%~dp0"

if exist "%~dp0.venv\Scripts\python.exe" (
    set "DRIVEGUARD_PYTHON=%~dp0.venv\Scripts\python.exe"
)

if exist "%~dp0bin\DriveGuardAI.exe" (
    start "" "%~dp0bin\DriveGuardAI.exe"
    exit /b 0
)

if exist "%~dp0build\bin\DriveGuardAI.exe" (
    start "" "%~dp0build\bin\DriveGuardAI.exe"
    exit /b 0
)

echo DriveGuardAI.exe was not found.
echo Expected either "%~dp0bin\DriveGuardAI.exe" or "%~dp0build\bin\DriveGuardAI.exe".
pause
exit /b 1
