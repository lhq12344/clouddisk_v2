# Phase 1 Review: Drogon Core Skeleton

Date: 2026-09-05

Supersession note: this file records the Phase 1 checkpoint. Phase 6/7 later
changed defaults to Core, retired legacy account/file adapters, and deleted the
old business-shaped services. Current status is tracked in `phase-6-review.md`
and `phase-7-review.md`.

## Scope Reviewed

- Added Drogon Core API skeleton directories for `application`, `domain`, and `infrastructure`.
- Added request context, result/error, feature flag, transaction runner, account/file ports, upload state-machine, runtime readiness, MySQL transaction-runner placeholder, MySQL account repository, and legacy password/JWT compatibility infrastructure. Temporary legacy adapter markers used during early migration were removed after Phase 7 source cutover.
- Added `/ready` endpoint while preserving `/health` as a lightweight liveness endpoint.
- Wired CoreRuntime initialization into gateway startup and CMake source discovery.
- Added account read/login/registration vertical slices for `/user/info`, `/user/signin`, `/user/signup`, `/user/sendcode`, and `/user/code` behind `CORE_ACCOUNT_READS=core|shadow`, `CORE_ACCOUNT_LOGIN=core`, and `CORE_ACCOUNT_REGISTRATION=core`; default behavior at this checkpoint remained legacy.
- Preserved existing account/file controller traffic on legacy behavior at this checkpoint; Phase 6/7 later completed source cutover and old-service removal.

## Evidence

- `node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs` passes.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` now passes for 15 active endpoints and 2 fail-closed compatibility routes.
- `go test ./...` passes.
- `go vet ./...` passes.
- `npm run build` in `forward_part/static` passes.
- `cmake -S forward_part/gateway -B build/gateway-phase1-check` now reaches the C++ configure phase with MSVC, but cannot complete because the Drogon CMake package is not installed or not discoverable (`DrogonConfig.cmake` / `drogon-config.cmake`).

## Exit Criteria Assessment

- New skeleton can be source-verified locally: complete.
- DB/Redis readiness is represented by runtime dependency status and `/ready`: partially complete until compiled and exercised with real middleware.
- At least one read use case through the new repository path: source-implemented for `userinfo`, pending C++ build and middleware-backed integration validation.
- Account login path: source-implemented with old PBKDF2-MD5 hash compatibility and legacy JWT claim/issuer compatibility, pending C++ build and Redis/MySQL integration validation.
- Account registration path: source-implemented with pending registration storage, verification-code match, idempotent already-created handling, account insert, and pending/code cleanup; pending C++ build and Redis/MySQL integration validation.
- Email verification send path: source-implemented as a MySQL outbox command with `EMAIL_VERIFICATION_REQUESTED`; email worker now accepts both legacy raw email messages and new JSON outbox payloads.
- Legacy flag/adapter rollback point existed at this checkpoint; after Phase 6/7, explicit legacy values fail closed because old adapters are retired.
- Old paths remain compatible: complete by source-level contract check.

## Review Decision

Phase 1 skeleton plus the first account-read vertical slice was ready for C++ toolchain validation at this checkpoint. Later phases continued implementation behind flags and completed source-level cutover; live middleware and full C++ build validation remain explicit runtime gates.
