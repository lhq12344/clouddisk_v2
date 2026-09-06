# Phase 6 Review: Cutover and Rollback Readiness

Date: 2026-09-05

## Scope Reviewed

- Added `scripts/project_start_scripts/verify_core_cutover_readiness.mjs` as a
  source-level gate for removing Core API dependencies on business-shaped
  `account_srv` and `file_srv`.
- Made local start/status scripts use the Core topology. `CORE_RUNTIME_PROFILE`
  now controls Core/shadow flag defaults only; retired `account_srv` and
  business-shaped `file_srv` are no longer started, checked, or stopped.
- Added start-time Core flag defaults for each runtime profile. The `core`
  profile enables completed Core account, file read/access, upload-control,
  upload-complete, file-delete, direct-small-upload, and outbox-relay paths
  after adding the Storage Control client adapter.
- Updated the compiled feature-flag defaults to Core so an unset environment no
  longer silently falls back to retired account/file gRPC adapters.
- Core file access and multipart control paths now use `GrpcStorageControlAdapter`
  against Consul-discovered `storage_control`; file gRPC fallback code and
  protobuf links have been removed from the gateway.
- The current frontend no longer chooses `/file/upload` for files below 5 MiB;
  it uses the same multipart presigned upload flow for all file sizes. The
  hidden edit/save path was also moved to multipart, and loopback presigned URL
  fallback no longer posts part bytes through `/file/uploadpart`.
- Checked current gateway controllers, CMake sources, local/Nacos config, and
  start/status/stop scripts for remaining legacy service dependencies.
- Retired feature-flag rollback to `account_srv` / `file_srv`; explicit legacy
  flag values now fail closed with actionable errors instead of reviving old
  adapters.

## Findings

- Gateway account/file controllers no longer include account/file protobufs or
  call `FindService("account_srv")` / `FindService("file_srv")`.
- Gateway CMake no longer links account/file protobuf generated sources; AI
  proto and Storage Control proto remain.
- Local gateway config, Nacos bootstrap config, and C++ config parsing no longer
  retain `account_srv` or `file_srv` as Core Consul dependencies.
- Startup/status/stop scripts no longer manage `account_srv` and `file_srv`.
- The script only fills feature-flag defaults when the operator has not already
  supplied explicit values, so Core/shadow experiments remain controllable per
  use case; explicit legacy values fail closed because old adapters are retired.
- `verify_core_cutover_readiness.mjs` now passes at source level.

## Decision

Phase 6 source cutover is complete. Runtime certification is still pending a
C++ build plus middleware-backed smoke evidence for MySQL, Redis, Consul,
Storage Control, Kafka/outbox relay, MinIO, and ClamAV.
