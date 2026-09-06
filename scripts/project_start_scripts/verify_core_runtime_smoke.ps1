param(
    [string]$BaseUrl = $(if ($env:BASE_URL) { $env:BASE_URL } else { 'http://127.0.0.1:2024' }),
    [string]$Jwt = $env:JWT,
    [string]$OtherJwt = $env:OTHER_JWT,
    [string]$SigninUsername = $env:SMOKE_SIGNIN_USERNAME,
    [string]$SigninPassword = $env:SMOKE_SIGNIN_PASSWORD,
    [string]$OtherSigninUsername = $env:SMOKE_OTHER_SIGNIN_USERNAME,
    [string]$OtherSigninPassword = $env:SMOKE_OTHER_SIGNIN_PASSWORD,
    [string]$ResultDir = $env:RESULT_DIR,
    [string]$FixturePath = $env:FIXTURE_PATH,
    [int]$FixtureSizeBytes = $(if ($env:FIXTURE_SIZE_BYTES) { [int]$env:FIXTURE_SIZE_BYTES } else { 262144 }),
    [int]$ScanTimeoutSeconds = $(if ($env:SCAN_TIMEOUT_SECONDS) { [int]$env:SCAN_TIMEOUT_SECONDS } else { 120 }),
    [switch]$RequireJwt,
    [switch]$RequireOtherJwt,
    [switch]$WaitForScanSuccess,
    [switch]$NoCleanup
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir '..\..')).Path
}

function New-Headers([bool]$Auth, [string]$AuthToken = '') {
    $headers = @{ 'X-Request-Id' = [guid]::NewGuid().ToString() }
    $token = if ($AuthToken) { $AuthToken } else { $Jwt }
    if ($Auth -and $token) {
        $headers['Authorization'] = "Bearer $token"
    }
    return $headers
}

function Get-SmokeToken([string]$Username, [string]$Password, [string]$Label) {
    if (-not $Username -or -not $Password) { return '' }
    $signin = Invoke-CoreRequest -Name "signin for $Label smoke token" -Method POST -Path '/user/signin' -Body @{
        username = $Username
        password = $Password
    } -ExpectedStatus @(200)
    if (-not $signin.json.token) {
        throw "signin for $Label succeeded but did not return token: $($signin.text)"
    }
    $summary.Add(@{ step = "signin-$Label"; status = $signin.status; username = $Username })
    return [string]$signin.json.token
}

function Read-ErrorBody($Response) {
    if ($null -eq $Response) { return '' }
    try {
        $stream = $Response.GetResponseStream()
        if ($null -eq $stream) { return '' }
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    } catch {
        return ''
    }
}

function Invoke-CoreRequest {
    param(
        [string]$Name,
        [string]$Method,
        [string]$Path,
        [object]$Body = $null,
        [bool]$Auth = $false,
        [string]$AuthToken = '',
        [int[]]$ExpectedStatus = @(200),
        [string]$BodyMustContain = ''
    )

    $uri = if ($Path.StartsWith('http')) { $Path } else { "$BaseUrl$Path" }
    $headers = New-Headers $Auth $AuthToken
    $payload = $null
    $contentType = $null
    if ($null -ne $Body) {
        $payload = $Body | ConvertTo-Json -Compress -Depth 10
        $contentType = 'application/json'
    }

    Write-Host "[runtime-smoke] $Name -> $Method $uri" -ForegroundColor Cyan
    try {
        $params = @{
            Uri = $uri
            Method = $Method
            Headers = $headers
            TimeoutSec = 60
            UseBasicParsing = $true
        }
        if ($null -ne $payload) { $params['Body'] = $payload }
        if ($null -ne $contentType) { $params['ContentType'] = $contentType }
        $response = Invoke-WebRequest @params
        $status = [int]$response.StatusCode
        $text = [string]$response.Content
    } catch {
        $response = $_.Exception.Response
        if ($null -eq $response) { throw }
        $status = [int]$response.StatusCode
        $text = Read-ErrorBody $response
    }

    if ($ExpectedStatus -notcontains $status) {
        throw "$Name returned HTTP $status; expected $($ExpectedStatus -join ', '). Body: $text"
    }
    if ($BodyMustContain -and $text -notmatch [regex]::Escape($BodyMustContain)) {
        throw "$Name response did not contain '$BodyMustContain'. Body: $text"
    }

    $json = $null
    if ($text) {
        try { $json = $text | ConvertFrom-Json } catch { $json = $null }
    }
    return @{ status = $status; text = $text; json = $json }
}

function New-SmokeFixture([string]$Path, [int]$SizeBytes) {
    $bytes = New-Object byte[] $SizeBytes
    $seed = [Text.Encoding]::UTF8.GetBytes("core-runtime-smoke:$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())")
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [byte](($i + $seed[$i % $seed.Length]) % 251)
    }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Upload-PartBytes([string]$Url, [byte[]]$Bytes) {
    $response = Invoke-WebRequest -UseBasicParsing -Method PUT -Uri $Url -Body $Bytes -ContentType 'application/octet-stream' -TimeoutSec 120
    if ([int]$response.StatusCode -lt 200 -or [int]$response.StatusCode -ge 300) {
        throw "presigned PUT returned HTTP $($response.StatusCode)"
    }
}

function Find-FileInQuery($QueryJson, [string]$Hash, [string]$Name) {
    if ($null -eq $QueryJson -or $null -eq $QueryJson.filelist) { return $null }
    foreach ($item in $QueryJson.filelist) {
        if ($item.filehash -eq $Hash -and $item.filename -eq $Name) {
            return $item
        }
    }
    return $null
}

$repoRoot = Resolve-RepoRoot
if (-not $ResultDir) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $ResultDir = Join-Path $repoRoot "baseline-results\core-runtime-smoke-$timestamp"
}
New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

if (-not $FixturePath) {
    $FixturePath = Join-Path $ResultDir 'core-runtime-smoke.bin'
    New-SmokeFixture $FixturePath $FixtureSizeBytes
}

$summary = New-Object System.Collections.Generic.List[object]

$health = Invoke-CoreRequest -Name 'health' -Method GET -Path '/health' -ExpectedStatus @(200)
$summary.Add(@{ step = 'health'; status = $health.status })

$ready = Invoke-CoreRequest -Name 'ready' -Method GET -Path '/ready' -ExpectedStatus @(200)
if ($ready.json.status -ne 'ready') {
    throw "/ready returned HTTP 200 but status was '$($ready.json.status)'"
}
$summary.Add(@{ step = 'ready'; status = $ready.json.status; dependencies = $ready.json.dependencies })

if (-not $Jwt) {
    $Jwt = Get-SmokeToken -Username $SigninUsername -Password $SigninPassword -Label 'primary'
}

if (-not $OtherJwt) {
    $OtherJwt = Get-SmokeToken -Username $OtherSigninUsername -Password $OtherSigninPassword -Label 'other-user'
}

if (-not $Jwt) {
    $message = '[runtime-smoke] JWT not set; authenticated upload/query/delete smoke was skipped'
    if ($RequireJwt) { throw $message }
    Write-Host $message -ForegroundColor Yellow
    $summary.Add(@{ step = 'authenticated-smoke'; status = 'skipped'; reason = 'JWT not set' })
    $summary | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $ResultDir 'summary.json')
    Write-Host "[runtime-smoke] results written to $ResultDir" -ForegroundColor Green
    exit 0
}

if (-not $OtherJwt -and $RequireOtherJwt) {
    throw '[runtime-smoke] OTHER_JWT not set and SMOKE_OTHER_SIGNIN_USERNAME/SMOKE_OTHER_SIGNIN_PASSWORD did not produce a second-user token'
}

Invoke-CoreRequest -Name 'small upload proxy fail-closed' -Method POST -Path '/file/upload' -Auth $true -ExpectedStatus @(503) -BodyMustContain 'small_upload_proxy_removed' | Out-Null
Invoke-CoreRequest -Name 'part proxy fail-closed' -Method POST -Path '/file/uploadpart' -Auth $true -ExpectedStatus @(503) -BodyMustContain 'part_proxy_removed' | Out-Null
$summary.Add(@{ step = 'legacy-upload-proxies'; status = 'fail-closed' })

$fileName = "core-runtime-smoke-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()).bin"
$fileHash = Get-Sha256 $FixturePath
$fileBytes = [IO.File]::ReadAllBytes($FixturePath)

$queryBefore = Invoke-CoreRequest -Name 'file query before upload' -Method POST -Path '/file/query' -Auth $true -ExpectedStatus @(200)
$summary.Add(@{ step = 'query-before-upload'; status = $queryBefore.status })

$init = Invoke-CoreRequest -Name 'init multipart' -Method POST -Path '/file/initupload' -Auth $true -Body @{
    file_name = $fileName
    file_hash = $fileHash
    file_size = $fileBytes.Length
    content_type = 'application/octet-stream'
} -ExpectedStatus @(200)

if (-not $init.json.upload_id -or -not $init.json.part_size -or -not $init.json.total_parts) {
    throw "initupload response missing upload_id/part_size/total_parts: $($init.text)"
}

$uploadId = [string]$init.json.upload_id
$partSize = [int64]$init.json.part_size
$totalParts = [int]$init.json.total_parts
$summary.Add(@{ step = 'initupload'; upload_id = $uploadId; part_size = $partSize; total_parts = $totalParts })

if ($OtherJwt) {
    Invoke-CoreRequest -Name 'other user upload status denied' -Method POST -Path '/file/Status' -Auth $true -AuthToken $OtherJwt -Body @{
        upload_id = $uploadId
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    Invoke-CoreRequest -Name 'other user presign parts denied' -Method POST -Path '/file/PresignParts' -Auth $true -AuthToken $OtherJwt -Body @{
        upload_id = $uploadId
        part_numbers = @(1)
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    Invoke-CoreRequest -Name 'other user complete upload denied' -Method POST -Path '/file/CompleteMultipart' -Auth $true -AuthToken $OtherJwt -Body @{
        upload_id = $uploadId
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    Invoke-CoreRequest -Name 'other user abort upload denied' -Method POST -Path '/file/AbortMultipart' -Auth $true -AuthToken $OtherJwt -Body @{
        upload_id = $uploadId
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    $summary.Add(@{ step = 'other-user-upload-session-denied'; status = 'pass' })
} else {
    $summary.Add(@{ step = 'other-user-upload-session-denied'; status = 'skipped'; reason = 'OTHER_JWT or SMOKE_OTHER_SIGNIN_USERNAME/PASSWORD not set' })
}

$statusBefore = Invoke-CoreRequest -Name 'multipart status before PUT' -Method POST -Path '/file/Status' -Auth $true -Body @{ upload_id = $uploadId } -ExpectedStatus @(200)
$summary.Add(@{ step = 'status-before-put'; uploaded_parts = $statusBefore.json.uploaded_parts })

$partNumbers = @(1..$totalParts)
$presign = Invoke-CoreRequest -Name 'presign parts' -Method POST -Path '/file/PresignParts' -Auth $true -Body @{
    upload_id = $uploadId
    part_numbers = $partNumbers
} -ExpectedStatus @(200)

foreach ($part in $presign.json.parts) {
    $partNumber = [int]$part.part_number
    $start = [int64](($partNumber - 1) * $partSize)
    $length = [int][Math]::Min($partSize, $fileBytes.Length - $start)
    $slice = New-Object byte[] $length
    [Array]::Copy($fileBytes, $start, $slice, 0, $length)
    Upload-PartBytes -Url ([string]$part.url) -Bytes $slice
}
$summary.Add(@{ step = 'presigned-put'; uploaded_parts = $totalParts })

$statusAfter = Invoke-CoreRequest -Name 'multipart status after PUT' -Method POST -Path '/file/Status' -Auth $true -Body @{ upload_id = $uploadId } -ExpectedStatus @(200)
if (@($statusAfter.json.uploaded_parts).Count -lt $totalParts) {
    throw "uploaded part count $(@($statusAfter.json.uploaded_parts).Count) is less than expected $totalParts"
}
$summary.Add(@{ step = 'status-after-put'; uploaded_parts = $statusAfter.json.uploaded_parts })

$complete = Invoke-CoreRequest -Name 'complete multipart' -Method POST -Path '/file/CompleteMultipart' -Auth $true -Body @{ upload_id = $uploadId } -ExpectedStatus @(200)
if ($complete.json.status -notin @('pending_scan', 'ready', 'success')) {
    throw "unexpected complete status '$($complete.json.status)': $($complete.text)"
}
$summary.Add(@{ step = 'complete'; status = $complete.json.status; object_key = $complete.json.object_key })

$queryAfter = Invoke-CoreRequest -Name 'file query after upload' -Method POST -Path '/file/query' -Auth $true -ExpectedStatus @(200)
$uploadedFile = Find-FileInQuery $queryAfter.json $fileHash $fileName
if ($null -eq $uploadedFile) {
    throw "uploaded file was not returned by /file/query for hash $fileHash name $fileName"
}
$summary.Add(@{ step = 'query-after-upload'; file_status = $uploadedFile.status })

if ($OtherJwt) {
    Invoke-CoreRequest -Name 'other user download denied' -Method POST -Path '/file/download' -Auth $true -AuthToken $OtherJwt -Body @{
        filename = $fileName
        filehash = $fileHash
        file_size = $fileBytes.Length
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    Invoke-CoreRequest -Name 'other user preview denied' -Method POST -Path '/file/showfile' -Auth $true -AuthToken $OtherJwt -Body @{
        filename = $fileName
        filehash = $fileHash
        file_size = $fileBytes.Length
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    Invoke-CoreRequest -Name 'other user delete denied' -Method POST -Path '/file/delete' -Auth $true -AuthToken $OtherJwt -Body @{
        filename = $fileName
        filehash = $fileHash
    } -ExpectedStatus @(401, 403, 404, 409) | Out-Null
    $summary.Add(@{ step = 'other-user-authorization-denied'; status = 'pass' })
} else {
    $summary.Add(@{ step = 'other-user-authorization-denied'; status = 'skipped'; reason = 'OTHER_JWT or SMOKE_OTHER_SIGNIN_USERNAME/PASSWORD not set' })
}

if ($WaitForScanSuccess) {
    $deadline = (Get-Date).AddSeconds($ScanTimeoutSeconds)
    do {
        Start-Sleep -Seconds 3
        $queryScan = Invoke-CoreRequest -Name 'poll scan status' -Method POST -Path '/file/query' -Auth $true -ExpectedStatus @(200)
        $uploadedFile = Find-FileInQuery $queryScan.json $fileHash $fileName
        if ($null -ne $uploadedFile -and $uploadedFile.status -eq 'success') { break }
    } while ((Get-Date) -lt $deadline)

    if ($null -eq $uploadedFile -or $uploadedFile.status -ne 'success') {
        throw "file did not reach success scan status within $ScanTimeoutSeconds seconds"
    }

    Invoke-CoreRequest -Name 'download presign after scan' -Method POST -Path '/file/download' -Auth $true -Body @{
        filename = $fileName
        filehash = $fileHash
        file_size = $fileBytes.Length
    } -ExpectedStatus @(200) | Out-Null
    Invoke-CoreRequest -Name 'preview presign after scan' -Method POST -Path '/file/showfile' -Auth $true -Body @{
        filename = $fileName
        filehash = $fileHash
        file_size = $fileBytes.Length
    } -ExpectedStatus @(200) | Out-Null
    $summary.Add(@{ step = 'download-preview-after-scan'; status = 'pass' })
}

if (-not $NoCleanup -and $null -ne $uploadedFile -and $uploadedFile.status -ne 'pending_scan') {
    $delete = Invoke-CoreRequest -Name 'delete uploaded file relation' -Method POST -Path '/file/delete' -Auth $true -Body @{
        filename = $fileName
        filehash = $fileHash
    } -ExpectedStatus @(200)
    $summary.Add(@{ step = 'delete'; status = $delete.status; body = $delete.json })
} elseif (-not $NoCleanup) {
    $summary.Add(@{ step = 'delete'; status = 'skipped'; reason = 'file is still pending_scan; rerun with -WaitForScanSuccess to include cleanup' })
}

$summary | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 (Join-Path $ResultDir 'summary.json')
Write-Host "[runtime-smoke] PASS: Core runtime smoke completed" -ForegroundColor Green
Write-Host "[runtime-smoke] results written to $ResultDir" -ForegroundColor Green
