param(
    [string]$BaseUrl = $(if ($env:BASE_URL) { $env:BASE_URL } else { 'http://127.0.0.1:2024' }),
    [string]$Jwt = $env:JWT,
    [string]$SigninUsername = $env:SMOKE_SIGNIN_USERNAME,
    [string]$SigninPassword = $env:SMOKE_SIGNIN_PASSWORD,
    [int]$Duration = $(if ($env:DURATION) { [int]$env:DURATION } else { 10 }),
    [int]$Qps = $(if ($env:QPS) { [int]$env:QPS } else { 20 }),
    [int]$Concurrency = $(if ($env:CONCURRENCY) { [int]$env:CONCURRENCY } else { 5 }),
    [string]$ResultDir = $env:RESULT_DIR
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function ConvertTo-SafeName([string]$Name) {
    return $Name -replace '[^A-Za-z0-9_.-]', '_'
}

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return 0 }
    $sorted = $Values | Sort-Object
    $position = ($sorted.Count - 1) * ($Percentile / 100.0)
    $floor = [math]::Floor($position)
    $ceiling = [math]::Min($floor + 1, $sorted.Count - 1)
    if ($floor -eq $ceiling) { return [double]$sorted[$floor] }
    return [double]$sorted[$floor] + ([double]$sorted[$ceiling] - [double]$sorted[$floor]) * ($position - $floor)
}

function Invoke-BaselineProbe {
    param(
        [string]$Name,
        [string]$Method,
        [string]$Path,
        [object]$Body = $null,
        [hashtable]$Headers = @{}
    )

    $uri = "$BaseUrl$Path"
    $totalRequests = [math]::Max(1, $Duration * $Qps)
    $delayMs = if ($Qps -gt 0) { [math]::Max(0, [int](1000 / $Qps)) } else { 0 }
    $latencies = New-Object System.Collections.Generic.List[double]
    $statuses = New-Object System.Collections.Generic.List[string]
    $payload = $null

    if ($null -ne $Body) {
        $payload = $Body | ConvertTo-Json -Compress
        $Headers['Content-Type'] = 'application/json'
    }

    Write-Output "[baseline] $Name -> $Method $Path"
    for ($i = 0; $i -lt $totalRequests; $i++) {
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            $params = @{
                Uri = $uri
                Method = $Method
                Headers = $Headers
                TimeoutSec = 30
                UseBasicParsing = $true
            }
            if ($null -ne $payload) {
                $params['Body'] = $payload
            }
            $response = Invoke-WebRequest @params
            $statuses.Add([string][int]$response.StatusCode)
        } catch {
            $response = $_.Exception.Response
            if ($null -ne $response -and $null -ne $response.StatusCode) {
                $statuses.Add([string][int]$response.StatusCode)
            } else {
                $statuses.Add('ERR')
            }
        } finally {
            $watch.Stop()
            $latencies.Add($watch.Elapsed.TotalMilliseconds)
        }
        if ($delayMs -gt 0) { Start-Sleep -Milliseconds $delayMs }
    }

    $avg = if ($latencies.Count -gt 0) { ($latencies | Measure-Object -Average).Average } else { 0 }
    $statusSummary = $statuses | Group-Object | Sort-Object Name | ForEach-Object { "  $($_.Name): $($_.Count)" }
    $summary = @(
        '',
        '=== Performance Summary ===',
        "Requests: $($latencies.Count)",
        ('Latency ms: avg={0:N2} p50={1:N2} p90={2:N2} p99={3:N2}' -f $avg, (Get-Percentile $latencies.ToArray() 50), (Get-Percentile $latencies.ToArray() 90), (Get-Percentile $latencies.ToArray() 99)),
        'Status codes:'
    ) + $statusSummary

    $logPath = Join-Path $ResultDir "$(ConvertTo-SafeName $Name).log"
    $summary | Tee-Object -FilePath $logPath
}

$repoRoot = Resolve-RepoRoot
if (-not $ResultDir) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ResultDir = Join-Path $repoRoot "baseline-results\core-api-$timestamp"
}

try {
    Invoke-WebRequest -UseBasicParsing -Uri "$BaseUrl/health" -TimeoutSec 5 | Out-Null
} catch {
    [Console]::Error.WriteLine("[baseline] Core API health check failed at $BaseUrl/health")
    [Console]::Error.WriteLine('[baseline] start the intended OpenResty/gateway stack first, then rerun this script')
    exit 3
}

New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

Invoke-BaselineProbe -Name 'health' -Method 'GET' -Path '/health'

if (-not $Jwt -and $SigninUsername -and $SigninPassword) {
    Write-Output '[baseline] JWT not set; signing in with SMOKE_SIGNIN_USERNAME/SMOKE_SIGNIN_PASSWORD'
    $signinBody = @{ username = $SigninUsername; password = $SigninPassword } | ConvertTo-Json -Compress
    try {
        $signinResponse = Invoke-WebRequest -UseBasicParsing -Uri "$BaseUrl/user/signin" -Method POST -Body $signinBody -ContentType 'application/json' -TimeoutSec 30
        $signinJson = $signinResponse.Content | ConvertFrom-Json
        if ($signinJson.token) {
            $Jwt = [string]$signinJson.token
            '[baseline] signin token acquired' | Tee-Object -FilePath (Join-Path $ResultDir 'signin.log')
        }
    } catch {
        "[baseline] signin failed: $($_.Exception.Message)" | Tee-Object -FilePath (Join-Path $ResultDir 'signin.log')
    }
}

if (-not $Jwt) {
    '[baseline] JWT not set; skipping authenticated endpoint probes' | Tee-Object -FilePath (Join-Path $ResultDir 'auth-skipped.log')
    exit 0
}

$authHeaders = @{ Authorization = "Bearer $Jwt" }
Invoke-BaselineProbe -Name 'userinfo' -Method 'GET' -Path '/user/info' -Headers $authHeaders.Clone()
Invoke-BaselineProbe -Name 'file_query' -Method 'POST' -Path '/file/query' -Headers $authHeaders.Clone()
Invoke-BaselineProbe -Name 'initupload' -Method 'POST' -Path '/file/initupload' -Headers $authHeaders.Clone() -Body @{ file_name = 'baseline.bin'; file_hash = 'baseline-phase0-hash'; file_size = 1048576; content_type = 'application/octet-stream' }
Invoke-BaselineProbe -Name 'presign_parts' -Method 'POST' -Path '/file/PresignParts' -Headers $authHeaders.Clone() -Body @{ upload_id = 'baseline-upload-id'; part_numbers = @(1) }
Invoke-BaselineProbe -Name 'complete_multipart' -Method 'POST' -Path '/file/CompleteMultipart' -Headers $authHeaders.Clone() -Body @{ upload_id = 'baseline-upload-id' }

Write-Output "[baseline] results written to $ResultDir"
