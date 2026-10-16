@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\test-visual-equipment.ps1"
set "gearTestExitCode=%errorlevel%"
pause
exit /b %gearTestExitCode%
