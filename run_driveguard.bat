@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0.venv\Scripts\python.exe" (
    set "DRIVEGUARD_PYTHON=%~dp0.venv\Scripts\python.exe"
)
set "DRIVEGUARD_ROOT=%~dp0"
start "" "%~dp0build\bin\DriveGuardAI.exe"
