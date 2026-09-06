param(
    [string]$BaseUrl = 'http://127.0.0.1:1'
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function Require-Check {
    param(
        [object]$Report,
        [string]$Name
    )
    $match = @($Report.checks | Where-Object { $_.name -eq $Name })
    if ($match.Count -ne 1) {
        throw "expected exactly one prereq check named '$Name', got $($match.Count)"
    }
    return $match[0]
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

$jsonText = & powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_prereqs.ps1 -WarnOnly -Json -BaseUrl $BaseUrl
if ($LASTEXITCODE -ne 0) {
    throw "verify_core_runtime_prereqs.ps1 -WarnOnly -Json exited with $LASTEXITCODE"
}

$report = $jsonText | ConvertFrom-Json
if ($report.status -notin @('pass', 'fail')) {
    throw "unexpected prereq report status: $($report.status)"
}
if ($report.baseUrl -ne $BaseUrl) {
    throw "baseUrl mismatch: $($report.baseUrl)"
}

Require-Check $report 'command:go' | Out-Null
Require-Check $report 'command:node' | Out-Null
Require-Check $report 'command:npm' | Out-Null
Require-Check $report 'command:cmake' | Out-Null
Require-Check $report 'runtime-config' | Out-Null
Require-Check $report 'drogon-cmake-package' | Out-Null
Require-Check $report 'tcp:base_url' | Out-Null
Require-Check $report 'http:health' | Out-Null
Require-Check $report 'auth-token' | Out-Null

& powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_prereqs.ps1 -BaseUrl $BaseUrl *> $null
if ($LASTEXITCODE -eq 0) {
    throw 'verify_core_runtime_prereqs.ps1 must exit non-zero in hard mode when required runtime checks fail'
}

Write-Host 'Runtime prerequisite checker self-test passed.' -ForegroundColor Green
