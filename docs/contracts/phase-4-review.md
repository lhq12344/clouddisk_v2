# Phase 4 Review: Upload Control and Storage Boundary

Date: 2026-09-05

## Scope Reviewed

- Added durable `upload_sessions` model and SQL migration.
- Added Core upload-session application ports and `MultipartUploadService`.
- Added MySQL upload-session repository with owner-scoped session loading and compare-and-set state transition support.
- Added Core control paths for `/file/initupload`, `/file/PresignParts`, `/file/Status`, and `/file/AbortMultipart` behind `CORE_UPLOAD_CONTROL=core|shadow`.
- Added Core `CompleteMultipart` application flow and MySQL finalization repository behind `CORE_UPLOAD_COMPLETE=core`.
- Added explicit `storage_control` Consul/config entries in Go config, C++ config, local gateway config, and Nacos bootstrap config.
- Added `proto/storage_control/storage_control.proto` to define the future narrow Go Storage Control boundary.
- Generated Go stubs for the Storage Control proto and added an initial `storage_control` gRPC server implementation.
- Added reproducible proto generation helpers for Bash and PowerShell.
- Generated C++ Storage Control protobuf message stubs and added a minimal
  hand-written client stub while `grpc_cpp_plugin` is unavailable locally; both
  are included in the gateway CMake target.
- Updated start/status/stop scripts to include `storage_control`.
- Added `GrpcStorageControlAdapter` for Core download/preview presigned GET,
  multipart init, part presign, status/list-parts, complete, and abort. Legacy
  adapters remain only for rollback paths.
- Updated the frontend upload manager so small files also use the multipart
  presigned PUT flow; `/file/upload` and `/file/uploadpart` remain as
  compatibility routes that fail closed instead of proxying object bytes.

## Implementation Notes

- `upload_sessions` is now the target source of truth for multipart owner, hash, object key, storage upload ID, part plan, state, expiry, and idempotency key.
- Redis may remain an acceleration/compatibility cache during migration, but the Core API no longer treats Redis-only session metadata as the intended final design.
- Presign/status/abort now load the session from MySQL using both `upload_id` and authenticated owner ID before touching storage control.
- The new Storage Control proto accepts object keys and storage upload IDs, not business user identity or file ownership decisions.
- The new Storage Control Go implementation delegates only to `internal.MinIOClient` operations and returns gRPC errors when MinIO is unavailable.
- Core `CompleteMultipart` now calls the narrow Storage Control RPC and then
  finalizes `files`, `user_files`, `upload_sessions`, and scan Outbox in the
  Core MySQL transaction. It no longer calls the business-shaped `file_srv`
  complete RPC when `CORE_UPLOAD_COMPLETE=core`.
- Core finalization writes scan outbox payloads with the real upserted `file_id`, so `storage-worker` can update scan status without relying on placeholder IDs.

## Evidence

- `node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs` passes and now covers upload session, upload control, and Storage Control proto wiring.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` now passes for 15 active endpoints and 2 fail-closed compatibility routes.
- `go test ./...` passes in the current workspace.
- `go vet ./...` passes in the current workspace.
- `go build ./backword_part/storage_control` passes.
- `go build ./clouddisk_v2/storage_control/protobuf` passes.
- `npm run build` in `forward_part/static` passes in the current workspace.
- C++ build remains unverified in this Windows workspace because CMake cannot find the Drogon package (`DrogonConfig.cmake` / `drogon-config.cmake`), even though MSVC is now discoverable. The checked-in C++ Storage Control client stub is source-verified; replacing it with generated output remains preferred once `grpc_cpp_plugin` is available.

## Exit Criteria Assessment

- Durable upload state model: source-implemented.
- Core init/presign/status/abort control plane: source-implemented behind flag, now using the new Storage Control adapter; pending C++ build and middleware validation.
- Storage Control boundary: proto, generated Go stubs, generated C++ message stubs, minimal C++ client stub, service entrypoint, and MinIO-backed implementation are source-implemented and Go-build verified where Go tooling is available; runtime MinIO/Consul validation pending.
- Core `CompleteMultipart` transaction/outbox finalization: source-implemented as a Core repository flow with real `file_id` in scan event payloads; runtime readiness still requires C++ build plus MySQL/Redis/MinIO/Kafka validation.
- Small-file direct upload migration: frontend source-implemented. Legacy
  `/file/upload` and loopback `/file/uploadpart` byte proxies no longer have
  active UI callers and now fail closed in Core.

## Review Decision

Phase 4 is code-ready but not runtime-certified. The next safe step is to
compile the gateway in a C++ toolchain, replace the hand-written client stub
with generated output when `grpc_cpp_plugin` is available, and validate the
Core complete flow with MySQL, Redis, MinIO, Kafka, and the outbox relay.
