param([string]$Target = 'ja2')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $projectRoot '_bin\build'
$toolRoot = Join-Path $projectRoot '_bin\tools'
$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$vsEnvironment = & cmd.exe /d /s /c "`"$vsRoot\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64 -vcvars_ver=14.44 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual C++ environment.' }
foreach ($entry in $vsEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:CARGO_HOME = Join-Path $toolRoot 'cargo'
$env:RUSTUP_HOME = Join-Path $toolRoot 'rustup'
$env:PATH = "$PSScriptRoot;$env:CARGO_HOME\bin;$env:PATH"
# Older bundled dependencies predate CMake 4's policy compatibility floor.
$env:CMAKE_POLICY_VERSION_MINIMUM = '3.5'

& $cmake -S $projectRoot -B $buildRoot -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    '-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-msvc.cmake' `
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo' `
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$buildRoot/play" `
    '-DBUILD_LAUNCHER=OFF' '-DWITH_RUST_BINARIES=OFF' '-DWITH_UNITTESTS=ON'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $cmake --build $buildRoot --target $Target --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

# Data-only changes must reach the playable build even when no relink is needed.
if ($Target -eq 'ja2') {
    & $cmake -E copy_directory_if_different (Join-Path $projectRoot 'assets\externalized') (Join-Path $buildRoot 'play\externalized')
    if ($LASTEXITCODE -ne 0) { throw 'Asset copy failed.' }
}
