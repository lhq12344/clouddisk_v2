param(
    [string]$BuildDir = 'build/gateway-core-logic-compile'
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

cmake -S forward_part/gateway/compile_probe -B $BuildDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$executable = Join-Path $BuildDir 'Release/core_logic_compile_probe.exe'
if (-not (Test-Path $executable)) {
    $executable = Join-Path $BuildDir 'core_logic_compile_probe'
}
if (-not (Test-Path $executable)) {
    throw "core logic compile probe executable was not produced under $BuildDir"
}

& $executable
if ($LASTEXITCODE -ne 0) {
    throw "core logic compile probe exited with code $LASTEXITCODE"
}

Write-Host 'Gateway Core logic compile probe passed.' -ForegroundColor Green
