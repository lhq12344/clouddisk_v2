#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "$PROJECT_ROOT"

if ! command -v protoc >/dev/null 2>&1; then
  echo "protoc is required" >&2
  exit 2
fi
if ! command -v protoc-gen-go >/dev/null 2>&1; then
  echo "protoc-gen-go is required for Go stubs" >&2
  exit 2
fi
if ! command -v protoc-gen-go-grpc >/dev/null 2>&1; then
  echo "protoc-gen-go-grpc is required for Go gRPC stubs" >&2
  exit 2
fi

protoc --go_out=. --go-grpc_out=. proto/storage_control/storage_control.proto
protoc --cpp_out=. proto/storage_control/storage_control.proto

if command -v grpc_cpp_plugin >/dev/null 2>&1; then
  protoc --grpc_out=. --plugin=protoc-gen-grpc="$(command -v grpc_cpp_plugin)" proto/storage_control/storage_control.proto
else
  echo "grpc_cpp_plugin not found; skipped C++ stubs" >&2
fi
