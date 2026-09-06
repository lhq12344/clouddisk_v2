param(
    [switch]$RequireCmake,
    [switch]$SkipGo,
    [switch]$SkipFrontend,
    [string]$CmakeBuildDir = 'build/gateway-phase1-check'
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Body,
        [switch]$AllowCmakeCompilerGap
    )

    Write-Host "[source-suite] $Name" -ForegroundColor Cyan
    try {
        & $Body
        Write-Host "[source-suite] PASS: $Name" -ForegroundColor Green
        return @{ name = $Name; status = 'pass'; detail = '' }
    } catch {
        $detail = $_.Exception.Message
        if ($AllowCmakeCompilerGap -and $detail -match 'No CMAKE_CXX_COMPILER could be found|Could not find a package configuration file provided by|Drogon was not found|librdkafka\+\+ and/or librdkafka libraries not found') {
            Write-Host "[source-suite] BLOCKED: $Name - C++ toolchain/dependency gap" -ForegroundColor Yellow
            return @{ name = $Name; status = 'blocked'; detail = ($detail -split "`r?`n" | Where-Object { $_ -match 'No CMAKE_CXX_COMPILER|Could not find a package configuration file provided by|Drogon was not found|librdkafka' } | Select-Object -First 1) }
        }
        Write-Host "[source-suite] FAIL: $Name - $detail" -ForegroundColor Red
        throw
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($ArgumentList -join ' ')"
    }
}

function Invoke-CmakeConfigure {
    param([string]$BuildDir)

    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & cmake -S forward_part/gateway -B $BuildDir 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $text = ($output | ForEach-Object {
        if ($_ -is [System.Management.Automation.ErrorRecord]) {
            $_.Exception.Message
        } else {
            [string]$_
        }
    } | Where-Object { $_ -ne 'System.Management.Automation.RemoteException' }) -join [Environment]::NewLine
    if ($text) { Write-Host $text }
    if ($exitCode -ne 0) {
        throw $text
    }
}

function Invoke-GoRuntimeRoleBuilds {
    $outputDir = Join-Path $repoRoot '.cache/source-suite-bins'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

    $packages = @(
        @{ Name = 'storage_control'; Path = './backword_part/storage_control' },
        @{ Name = 'outbox_relay'; Path = './backword_part/outbox_relay' },
        @{ Name = 'store_srv'; Path = './other_srv/store_srv' },
        @{ Name = 'ai_srv'; Path = './backword_part/AI_server' },
        @{ Name = 'mcp_srv'; Path = './backword_part/mcp_server' }
    )

    foreach ($package in $packages) {
        $output = Join-Path $outputDir $package.Name
        if ($IsWindows -or $env:OS -eq 'Windows_NT') {
            $output = "$output.exe"
        }
        Write-Host "[source-suite]   go build $($package.Path)"
        Invoke-Native -FilePath go -ArgumentList @('build', '-p', '1', '-ldflags=-w -s', '-o', $output, $package.Path)
    }
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

$results = New-Object System.Collections.Generic.List[object]

$results.Add((Invoke-Step 'gateway skeleton verifier' { Invoke-Native node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs }))
$results.Add((Invoke-Step 'HTTP contract verifier' { Invoke-Native node scripts/project_start_scripts/verify_core_http_contract.mjs }))
$results.Add((Invoke-Step 'Core cutover verifier' { Invoke-Native node scripts/project_start_scripts/verify_core_cutover_readiness.mjs }))
$results.Add((Invoke-Step 'legacy removal verifier' { Invoke-Native node scripts/project_start_scripts/verify_legacy_service_removal_ready.mjs }))
$results.Add((Invoke-Step 'OpenResty route verifier' { Invoke-Native node scripts/project_start_scripts/verify_openresty_core_routes.mjs }))
$results.Add((Invoke-Step 'Core source invariant verifier' { Invoke-Native node scripts/project_start_scripts/verify_core_source_invariants.mjs }))
$results.Add((Invoke-Step 'Runtime smoke contract verifier' { Invoke-Native node scripts/project_start_scripts/verify_core_runtime_smoke_contract.mjs }))
$results.Add((Invoke-Step 'Baseline contract verifier' { Invoke-Native node scripts/project_start_scripts/verify_core_baseline_contract.mjs }))
$results.Add((Invoke-Step 'Committed secret verifier' { Invoke-Native node scripts/project_start_scripts/verify_no_committed_secrets.mjs }))
$results.Add((Invoke-Step 'Gateway Core compile probe' {
    Invoke-Native -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/project_start_scripts/verify_gateway_core_logic_compile.ps1')
}))
$results.Add((Invoke-Step 'Runtime prereq checker self-test' {
    Invoke-Native -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/project_start_scripts/verify_core_runtime_prereqs_selftest.ps1')
}))
$results.Add((Invoke-Step 'PowerShell script syntax verifier' {
    Invoke-Native -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts/project_start_scripts/verify_powershell_script_syntax.ps1')
}))

if (-not $SkipGo) {
    $env:GOCACHE = Join-Path $repoRoot '.cache/go-build'
    $env:GOTOOLCHAIN = if ($env:GOTOOLCHAIN) { $env:GOTOOLCHAIN } else { 'go1.25.5' }
    Remove-Item Env:GOMODCACHE -ErrorAction SilentlyContinue
    $results.Add((Invoke-Step 'go test ./...' { Invoke-Native go test ./... }))
    $results.Add((Invoke-Step 'go vet ./...' { Invoke-Native go vet ./... }))
    $results.Add((Invoke-Step 'Go runtime role builds' { Invoke-GoRuntimeRoleBuilds }))
}

if (-not $SkipFrontend) {
    $results.Add((Invoke-Step 'frontend build' {
        Push-Location forward_part/static
        try {
            Invoke-Native npm run build
        } finally {
            Pop-Location
        }
    }))
}

$results.Add((Invoke-Step 'git diff whitespace check' { Invoke-Native git diff --check }))

$cmakeStep = { Invoke-CmakeConfigure -BuildDir $CmakeBuildDir }
if ($RequireCmake) {
    $results.Add((Invoke-Step 'CMake configure' $cmakeStep))
} else {
    $results.Add((Invoke-Step 'CMake configure' $cmakeStep -AllowCmakeCompilerGap))
}

$failed = @($results | Where-Object { $_.status -eq 'fail' })
$blocked = @($results | Where-Object { $_.status -eq 'blocked' })

Write-Host ''
Write-Host '[source-suite] summary' -ForegroundColor Cyan
$results | ForEach-Object {
    $line = "  $($_.status.ToUpper())  $($_.name)"
    if ($_.detail) { $line += " - $($_.detail)" }
    Write-Host $line
}

if ($failed.Count -gt 0) {
    exit 1
}
if ($RequireCmake -and $blocked.Count -gt 0) {
    exit 2
}

exit 0
