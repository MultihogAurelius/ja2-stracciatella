param([string]$GameDirectory = 'F:\GOG Games\Jagged Alliance 2', [switch]$OpenGL)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$playDirectory = Join-Path $projectRoot '_bin\build\play'
if (-not (Test-Path -LiteralPath (Join-Path $playDirectory 'ja2.exe'))) {
    throw 'Run Build Custom JA2.cmd first.'
}
$settings = @{
    GTEST_FILTER = 'VisualEquipment.*:VanillaGearAssets.*:DefaultTargetAimTest.*:MercAcknowledgement.*:InterfaceScaling.*:BulletTrail.*'
    GTEST_OUTPUT = "xml:$projectRoot\_bin\gear-tests.xml"
    JA2_VISUAL_TEST_GAMEDIR = $GameDirectory
    JA2_VISUAL_TEST_OUTPUT = "$projectRoot\_bin\vanilla-gear-preview.bmp"
    JA2_VIDEO_TEST_OPENGL = $(if ($OpenGL) { '1' } else { $null })
}
$previous = @{}
foreach ($name in $settings.Keys) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $settings[$name], 'Process')
}
Push-Location $playDirectory
try {
    & .\ja2.exe -gamedir $GameDirectory -unittests
    if ($LASTEXITCODE -ne 0) { throw "Visual gear checks failed (exit $LASTEXITCODE)." }
    Write-Host "Results: $projectRoot\_bin\gear-tests.xml"
    Write-Host "Preview: $projectRoot\_bin\vanilla-gear-preview.bmp"
}
finally {
    Pop-Location
    foreach ($name in $settings.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
