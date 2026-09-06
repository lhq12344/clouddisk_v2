param(
    [switch]$WarnOnly,
    [switch]$Json,
    [string]$ConfigPath = 'scripts/nacos-config/clouddisk.json',
    [string]$BaseUrl = $(if ($env:BASE_URL) { $env:BASE_URL } else { 'http://127.0.0.1:2024' })
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function Add-Check {
    param(
        [System.Collections.Generic.List[object]]$Checks,
        [string]$Name,
        [string]$Status,
        [string]$Detail = ''
    )
    $Checks.Add([pscustomobject]@{ name = $Name; status = $Status; detail = $Detail }) | Out-Null
}

function Test-CommandAvailable {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Test-TcpPort {
    param(
        [string]$HostName,
        [int]$Port,
        [int]$TimeoutMs = 800
    )

    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $task = $client.ConnectAsync($HostName, $Port)
        $connected = $task.Wait($TimeoutMs) -and $client.Connected
        $client.Dispose()
        return $connected
    } catch {
        return $false
    }
}

function Test-HttpHealth {
    param(
        [string]$Url,
        [int]$TimeoutSec = 5
    )

    try {
        $response = Invoke-WebRequest -UseBasicParsing -TimeoutSec $TimeoutSec -Uri $Url
        return [pscustomobject]@{ ok = ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300); detail = "HTTP $($response.StatusCode)" }
    } catch {
        return [pscustomobject]@{ ok = $false; detail = $_.Exception.Message }
    }
}

function Get-HostPort {
    param(
        [object]$Node,
        [string]$DefaultHost = '127.0.0.1'
    )
    if ($null -eq $Node) { return $null }
    $hostName = if ($Node.host) { [string]$Node.host } else { $DefaultHost }
    $port = [int]$Node.port
    return [pscustomobject]@{ host = $hostName; port = $port }
}

$repoRoot = Resolve-RepoRoot
Set-Location $repoRoot

$checks = [System.Collections.Generic.List[object]]::new()

foreach ($cmd in @('go', 'node', 'npm', 'cmake')) {
    if (Test-CommandAvailable $cmd) {
        Add-Check $checks "command:$cmd" 'pass'
    } else {
        Add-Check $checks "command:$cmd" 'fail' 'required command is not on PATH'
    }
}

if ($env:Drogon_DIR -or $env:CMAKE_PREFIX_PATH) {
    Add-Check $checks 'drogon-cmake-package' 'pass' 'Drogon_DIR or CMAKE_PREFIX_PATH is set'
} else {
    Add-Check $checks 'drogon-cmake-package' 'fail' 'set Drogon_DIR or CMAKE_PREFIX_PATH before requiring gateway CMake build'
}

if (Test-Path -LiteralPath $ConfigPath) {
    Add-Check $checks 'runtime-config' 'pass' $ConfigPath
    $config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
} else {
    Add-Check $checks 'runtime-config' 'fail' "missing $ConfigPath"
    $config = $null
}

$portChecks = @()
if ($config) {
    $portChecks += @{ name = 'mysql'; endpoint = Get-HostPort $config.mysql }
    $portChecks += @{ name = 'redis'; endpoint = Get-HostPort $config.redis }
    $portChecks += @{ name = 'consul'; endpoint = Get-HostPort $config.consul }
    $portChecks += @{ name = 'kafka'; endpoint = Get-HostPort $config.kafka }
    $portChecks += @{ name = 'minio'; endpoint = Get-HostPort $config.minio }
    $portChecks += @{ name = 'clamav'; endpoint = Get-HostPort $config.clamav }
    $portChecks += @{ name = 'storage_control'; endpoint = Get-HostPort $config.consul.storage_control }
    $portChecks += @{ name = 'mcp_srv'; endpoint = Get-HostPort $config.consul.mcp_srv }
    $portChecks += @{ name = 'ai_srv'; endpoint = Get-HostPort $config.consul.ai_srv }
    $portChecks += @{ name = 'gateway'; endpoint = Get-HostPort $config.consul.gateway_srv }
}

try {
    $baseUri = [System.Uri]$BaseUrl
    $portChecks += @{ name = 'base_url'; endpoint = [pscustomobject]@{ host = $baseUri.Host; port = $baseUri.Port } }
} catch {
    Add-Check $checks 'base-url' 'fail' "invalid BASE_URL: $BaseUrl"
}

foreach ($item in $portChecks) {
    if ($null -eq $item.endpoint) { continue }
    $label = "tcp:$($item.name)"
    $detail = "$($item.endpoint.host):$($item.endpoint.port)"
    if (Test-TcpPort -HostName $item.endpoint.host -Port $item.endpoint.port) {
        Add-Check $checks $label 'pass' $detail
    } else {
        Add-Check $checks $label 'fail' "not listening at $detail"
    }
}

if ($env:JWT) {
    Add-Check $checks 'auth-token' 'pass' 'JWT is set'
} elseif ($env:SMOKE_SIGNIN_USERNAME -and $env:SMOKE_SIGNIN_PASSWORD) {
    Add-Check $checks 'auth-token' 'pass' 'SMOKE_SIGNIN_USERNAME/PASSWORD are set for auto-login'
} else {
    Add-Check $checks 'auth-token' 'fail' 'set JWT or SMOKE_SIGNIN_USERNAME/SMOKE_SIGNIN_PASSWORD before authenticated smoke/baseline'
}

$healthUrl = $BaseUrl.TrimEnd('/') + '/health'
$health = Test-HttpHealth -Url $healthUrl
if ($health.ok) {
    Add-Check $checks 'http:health' 'pass' "$healthUrl - $($health.detail)"
} else {
    Add-Check $checks 'http:health' 'fail' "$healthUrl - $($health.detail)"
}

$failed = @($checks | Where-Object { $_.status -ne 'pass' })

if ($Json) {
    [pscustomobject]@{
        status = if ($failed.Count -eq 0) { 'pass' } else { 'fail' }
        baseUrl = $BaseUrl
        configPath = $ConfigPath
        checks = @($checks)
    } | ConvertTo-Json -Depth 5
} else {
    Write-Host '[runtime-prereqs] summary' -ForegroundColor Cyan
    foreach ($check in $checks) {
        $color = if ($check.status -eq 'pass') { 'Green' } else { 'Yellow' }
        $line = "  $($check.status.ToUpper())  $($check.name)"
        if ($check.detail) { $line += " - $($check.detail)" }
        Write-Host $line -ForegroundColor $color
    }
}

if ($failed.Count -gt 0 -and -not $WarnOnly) {
    exit 1
}

exit 0
