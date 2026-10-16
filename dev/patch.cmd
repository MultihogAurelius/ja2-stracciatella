@echo off
rem The project's dependency patch step uses patch -p1 with stdin.
rem Use native Git on Windows so the build does not require an MSYS runtime.
if not "%~1"=="-p1" exit /b 1
"C:\Program Files\Git\cmd\git.exe" apply --ignore-whitespace --whitespace=nowarn -p1 -
exit /b %errorlevel%
