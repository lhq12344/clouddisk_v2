$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $PSCommandPath
$projectRoot = (Resolve-Path (Join-Path $scriptDir '..\..')).Path
Push-Location $projectRoot
try {
    foreach ($tool in @('protoc', 'protoc-gen-go', 'protoc-gen-go-grpc')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool is required"
        }
    }

    protoc --go_out=. --go-grpc_out=. proto/storage_control/storage_control.proto
    protoc --cpp_out=. proto/storage_control/storage_control.proto

    $grpcCppPlugin = Get-Command grpc_cpp_plugin -ErrorAction SilentlyContinue
    if ($grpcCppPlugin) {
        protoc --grpc_out=. "--plugin=protoc-gen-grpc=$($grpcCppPlugin.Source)" proto/storage_control/storage_control.proto
    } else {
        [Console]::Error.WriteLine('grpc_cpp_plugin not found; skipped C++ stubs')
    }
} finally {
    Pop-Location
}
