@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\build.ps1"
if errorlevel 1 (
    echo Build failed. See the error above.
    pause
    exit /b 1
)
echo Build complete. Run Play Custom JA2.cmd to play.
pause
