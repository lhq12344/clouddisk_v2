# Core Migration Completion Audit

Date: 2026-09-05

This audit maps the plan's testable acceptance criteria to current evidence. It
keeps source completion separate from runtime certification so the migration does
not claim production readiness from source checks alone.

## Current Gate Result

- Source suite: `scripts/project_start_scripts/verify_core_source_suite.ps1`
  passes all runnable source gates, including `verify_core_source_invariants.mjs`,
  `verify_gateway_core_logic_compile.ps1`, low-memory sequential Go runtime role
  builds, and frontend build, then reports CMake as externally blocked on the
  missing Drogon CMake package.
- Runtime smoke: `scripts/project_start_scripts/verify_core_runtime_smoke.ps1`
  is ready to run after the gateway and middleware stack are available. Local
  TCP probes for `127.0.0.1:2024` and `127.0.0.1:38080` currently fail, so no
  live gateway/OpenResty endpoint is available for smoke execution in this
  workspace yet. `verify_core_runtime_smoke_contract.mjs` is included in the
  source suite to statically guard the smoke script's endpoint coverage and
  summary-evidence contract until live runtime certification can run.
- Runtime prerequisite checker: `scripts/project_start_scripts/verify_core_runtime_prereqs.ps1`
  now validates command availability, Drogon CMake discovery environment,
  configured middleware ports, `BASE_URL` TCP reachability, `/health` HTTP
  response, and JWT/auto-login inputs before smoke or baseline execution; it
  returns non-zero outside `-WarnOnly` while the current environment is missing
  required runtime dependencies and supports `-Json` for CI/automation gating.
  `verify_core_runtime_prereqs_selftest.ps1` is included in the source suite to
  keep the JSON contract parseable, ensure the required checks stay present, and
  prove hard mode exits non-zero when required runtime checks fail. `/health`
  is treated as ready only for 2xx HTTP responses.
- PowerShell runtime/baseline scripts are parsed by
  `verify_powershell_script_syntax.ps1` inside the source suite, preventing
  syntax regressions from reaching runtime certification.
- Go unit-test isolation: `internal/viper_config_centre.go` skips external
  Nacos/MySQL/Redis/Kafka/MinIO bootstrap for Go test binaries by default;
  integration tests can opt back in with `CLOUDDISK_TEST_BOOTSTRAP=1`, and
  `internal/viper_config_centre_test.go` covers both Unix `.test` and Windows
  `.test.exe` detection.
- Historical manifest docs under `manifest_txt/` are now explicitly marked as
  pre-Core-migration snapshots so stale `account_srv`/`file_srv` descriptions do
  not conflict with the current execution contract in `AGENTS.md`, ADRs, and
  contract audits; the source invariant verifier scans every `manifest_txt/*.txt`
  file for this archive marker.
- Gateway config bootstrap supports `CLOUDDISK_CONFIG_JSON`,
  `CLOUDDISK_CONFIG_FILE`, and `CLOUDDISK_CONFIG_VERSION`, then falls back to
  local config files with the retired account/file service keys removed.
- Gateway `GetFreePort` is now platform-guarded for Windows/Linux and CMake
  links `ws2_32` on Windows, so the next C++ validation step is not expected to
  fail on the previous Unix-only socket header path.
- `/ready` now keeps the config snapshot but also probes MySQL with `select 1`
  and Redis with `PING`, failing closed when either client is absent or the
  probe fails.
- Config logs no longer print full Nacos payloads, and email SMTP credentials
  are injected with `EMAIL_SMTP_PASS` instead of being stored in
  `other_srv/email_srv/email_config.json`.
- `verify_no_committed_secrets.mjs` is included in the source suite to prevent
  the removed SMTP app password, private keys, AWS-style access keys, non-empty
  committed SMTP passwords, or direct secret-bearing log statements from
  re-entering the repository.
- Core upload/delete outbox payload builders now JSON-escape file hash, object
  key, content type, and request id before inserting event payload/header JSON;
  source invariants guard against reverting to raw string interpolation.
- Outbox relay now fails closed when DB/Kafka dependencies are missing, rejects
  malformed outbox header JSON instead of silently dropping headers, and checks
  `RowsAffected` for lease-owned SENT/FAILED transitions so lost ownership is
  observable. Storage Worker Inbox DONE/DLQ transitions and DLQ persistence now
  also require exactly one affected row, keeping Kafka offset commits aligned
  with durable Inbox state.
- Upload finalization now rejects a transaction that fails to update the owned
  upload session row, preventing `CompleteMultipart` from returning success
  while the durable session remains in an earlier state.
- C++ gateway build: not certified yet. CMake now detects MSVC but cannot find
  the Drogon package; `forward_part/gateway/CMakeLists.txt` now fails fast with
  a clear `Drogon was not found` message and tells operators to set
  `Drogon_DIR` or `CMAKE_PREFIX_PATH`. A read-only discovery pass found neither
  `drogon_ctl` on PATH nor `DrogonConfig.cmake` under the common local project,
  vcpkg, or Program Files locations checked in this workspace.
- C++ Core logic compile probe: certified. The standalone probe builds the Core
  domain/application upload-state and result-contract headers without requiring
  Drogon/Nacos and now links the feature-flag implementation to exercise
  environment override parsing without MSVC `getenv` warnings. It also executes
  fake-port application flows for signin, registration verification account
  creation/idempotency, email verification outbox command enqueueing, file-access
  presign denial before scan success, async file-delete queueing, and duplicate
  multipart complete idempotency, catching header/implementation/service-wiring
  regressions before the full gateway dependency stack is available.
- Middleware certification: pending live MySQL, Redis, Consul, Kafka,
  MinIO/OSS, ClamAV, OpenResty, Storage Control, Outbox relay, and Storage
  Worker execution.

## Acceptance Criteria Matrix

| # | Requirement | Current evidence | Status |
|---:|---|---|---|
| 1 | Current frontend can register, verify, login, list, upload, resume, download, preview, and delete with compatible HTTP paths/fields. | `verify_core_http_contract.mjs` passes for 15 active endpoints and 2 fail-closed compatibility routes; frontend now uses multipart presigned upload for all file sizes. | Source-verified; runtime E2E pending. |
| 2 | `/user/code` creates the account atomically after code verification and is idempotent. | `RegistrationService::verifyCode` checks code, loads pending registration, creates account, treats already-created account as success, then deletes pending/code keys; source invariant checks keep the registration path on Core/fail-closed behavior; the Gateway Core compile probe exercises the verify-code create-account and already-created idempotency paths through fake ports. | Source-verified; Redis/MySQL runtime pending. |
| 3 | Old password hashes still login and new JWTs remain filter-compatible. | `LegacyPasswordVerifier` keeps PBKDF2-MD5 compatibility; `LegacyJwtIssuer` keeps `issuer=Signin`, `ID`, and `Name` claims; Go contract tests pass. | Source-verified; cross-language live token test pending. |
| 4 | Unauthorized users cannot query/access/delete another user's file or operate another upload session. | Core file reads and upload-session loads use trusted request context and owner-scoped queries; source verifier covers the seams; runtime smoke now supports `OTHER_JWT` or `SMOKE_OTHER_SIGNIN_USERNAME`/`SMOKE_OTHER_SIGNIN_PASSWORD` to verify a second user cannot operate the primary user's upload session (`Status`, `PresignParts`, `CompleteMultipart`, `AbortMultipart`) or download, preview, or delete the primary user's uploaded file; `-RequireOtherJwt` makes this matrix mandatory for full permission certification. | Source-verified; live runtime unauthorized matrix pending. |
| 5 | Same-hash duplicate upload avoids duplicate object writes; infected files cannot be instant-uploaded/downloaded/previewed. | Core finalization upserts metadata/relation and access policy rejects non-success scan states for download/preview. | Partially source-verified; duplicate/infected runtime fixtures pending. |
| 6 | Core API does not receive multipart bytes; browser PUTs directly to presigned URLs. | Frontend `UploadManager` calls presign then `fetch(..., { method: 'PUT' })`; `/file/upload` and `/file/uploadpart` fail closed. | Source-verified; RSS/data-plane runtime test pending. |
| 7 | File list does not call object storage per item. | `MysqlFileRepository::listOwnedFiles` reads MySQL metadata only; file list constructs `GrpcStorageControlAdapter(nullptr)` and does not presign/stat. | Source-verified; mock/counter runtime test pending. |
| 8 | Complete upload commits metadata, user relation, and Outbox atomically. | `MultipartUploadService` now CAS-transitions to `completing` before Storage Control complete, avoids duplicate storage complete after CAS conflict, and `MysqlUploadSessionRepository::finalizeCompletedUpload` writes `files`, `user_files`, `outboxes`, and session state inside one MySQL transaction; final session status update must affect a row or the use case returns conflict; the Gateway Core compile probe covers the upload state machine transition contract and duplicate complete idempotency through fake ports. | Source-verified; DB failure-injection pending. |
| 9 | Kafka downtime is recovered by Outbox relay with idempotent scan event delivery. | `internal/outboxrelay` extracted; standalone `backword_part/outbox_relay` builds; generic Core outbox enqueue and scan outbox writes are idempotent on `event_id`; relay now fails closed on missing DB/Kafka, malformed headers, and lost lease-owned state updates; outbox relay unit tests lock operational defaults, exponential retry backoff cap, missing producer behavior, and malformed header rejection; Storage Worker tests cover retry classification/backoff and DLQ event-context preservation. | Source-verified; Kafka outage/backlog drain pending. |
| 10 | Last reference delete emits cleanup event; HTTP delete does not synchronously require MinIO. | Core delete writes `OBJECT_DELETE_REQUESTED`; Storage Worker re-checks references before object delete; the Gateway Core compile probe exercises file-delete service queueing semantics through fake ports. | Source-verified; MinIO outage/replay runtime pending. |
| 11 | Scan state transitions cannot regress terminal states. | Storage Worker now updates file scan status only with `status = pending_scan`, treats `success`/`infected`/`scan_failed` as already converged, and verifies Inbox DONE/DLQ state changes through affected-row checks before considering work durable; the invariant verifier plus worker helper/retry/DLQ tests lock this guard and its failure handling. | Source-verified; concurrent/replay runtime pending. |
| 12 | Drogon no longer links account/file protobuf or discovers `account_srv`/`file_srv`; AI gRPC remains. | `verify_core_cutover_readiness.mjs`, `verify_legacy_service_removal_ready.mjs`, `verify_core_source_invariants.mjs`, and `verify_gateway_core_logic_compile.ps1` pass; account/file proto dirs are deleted. | Source-verified; full Drogon gateway build pending external dependency. |
| 13 | Start/status/stop no longer require old services; new roles are independently checked. | Scripts manage `outbox_relay`, `storage_control`, `store_srv`, `AI_srv`, `mcp_srv`, gateway, frontend; README and AGENTS.md are updated; source suite builds Go roles sequentially into `.cache/source-suite-bins`; Go unit tests skip external bootstrap unless explicitly opted in, with cross-platform test-binary detection covered by unit tests; gateway config bootstrap accepts env JSON/file/version without legacy service keys; `/ready` probes MySQL and Redis; runtime prereq checker identifies missing dependencies before smoke. | Source-verified; live script smoke pending. |
| 14 | Fixed fixtures preserve HTTP status/JSON contracts except documented bug fixes. | HTTP contract snapshot and verifier are updated to current active endpoints and fail-closed compatibility routes. | Source-verified; fixture replay pending. |
| 15 | Migration performance does not regress metadata endpoints; upload/download data bypass Core API. | PowerShell and Bash baseline runners exist; both support JWT or smoke auto-login credentials; `verify_core_baseline_contract.mjs` is included in the source suite to keep health/userinfo/file-query/init/presign/complete coverage and p50/p90/p99 reporting intact; runtime smoke can exercise upload data-plane; no live p95 comparison yet. | Source-verified harness; pending runtime benchmark. |

## Next Runtime Certification Steps

1. Run `verify_core_runtime_prereqs.ps1` without `-WarnOnly` to confirm the
   runtime certification environment is ready.
2. Install or expose the Drogon CMake package so gateway CMake configure and
   build can run with `verify_core_source_suite.ps1 -RequireCmake`.
3. Start MySQL, Redis, Consul, Kafka, MinIO/OSS, ClamAV, OpenResty, gateway,
   Storage Control, Outbox relay, Storage Worker, AI, MCP, and frontend through
   the current start scripts.
4. Run `verify_core_runtime_smoke.ps1 -RequireJwt` with a valid Core JWT.
5. Run `verify_core_runtime_smoke.ps1 -RequireJwt -WaitForScanSuccess` when the
   Storage Worker and ClamAV are active, then capture download/preview evidence.
6. Run `core_api_baseline.ps1` / `core_api_baseline.sh` to collect p50/p90/p99
   endpoint latency and compare against the migration baseline.
