param(
    [string[]]$Path = @(
        'scripts/project_start_scripts/verify_core_runtime_prereqs.ps1',
        'scripts/project_start_scripts/verify_core_runtime_prereqs_selftest.ps1',
        'scripts/project_start_scripts/verify_core_runtime_smoke.ps1',
        'scripts/project_start_scripts/verify_gateway_core_logic_compile.ps1',
        'scripts/project_start_scripts/verify_core_source_suite.ps1',
        'scripts/project_start_scripts/core_api_baseline.ps1'
    )
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

$failed = $false
foreach ($scriptPath in $Path) {
    $resolved = Resolve-Path -LiteralPath $scriptPath -ErrorAction Stop
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($resolved.Path, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -gt 0) {
        $failed = $true
        Write-Host "[powershell-syntax] FAIL: $scriptPath" -ForegroundColor Red
        foreach ($err in $errors) {
            Write-Host "  line $($err.Extent.StartLineNumber): $($err.Message)" -ForegroundColor Red
        }
    } else {
        Write-Host "[powershell-syntax] PASS: $scriptPath" -ForegroundColor Green
    }
}

if ($failed) {
    exit 1
}

Write-Host 'PowerShell script syntax verification passed.' -ForegroundColor Green
