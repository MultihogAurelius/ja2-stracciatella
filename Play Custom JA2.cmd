@echo off
setlocal
pushd "%~dp0_bin\build\play"
if not exist "ja2.exe" (
    echo The custom game has not been built. Run Build Custom JA2.cmd first.
    pause
    popd
    exit /b 1
)
ja2.exe -gamedir "F:\GOG Games\Jagged Alliance 2" -window -res 1920x1080 %*
set "gameExitCode=%errorlevel%"
popd
exit /b %gameExitCode%
