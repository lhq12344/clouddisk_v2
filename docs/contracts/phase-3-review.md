# Phase 3 Review: File Read, Authorization, and Access Decisions

Date: 2026-09-05

Supersession note: Phase 4 replaced the temporary legacy storage adapter with
the narrow Storage Control service, and Phase 6/7 retired the business-shaped
`file_srv` fallback. This file records the Phase 3 review point; current
cutover status is tracked in `phase-6-review.md` and `phase-7-review.md`.

## Scope Reviewed

- `/file/query` now has a Core metadata read path behind `CORE_FILE_READS=core|shadow`.
- `/file/download` and `/file/showfile` now have Core authorization/access-decision paths behind `CORE_FILE_ACCESS=core|shadow`.
- Default behavior at the Phase 3 checkpoint remained legacy for file endpoints unless flags were explicitly enabled; current defaults are Core after Phase 6/7.
- Existing frontend HTTP paths and JSON fields remain unchanged.

## Implementation Notes

- `MysqlFileRepository::listOwnedFiles` reads only `user_files` + `files`; it does not call object storage or `file_srv`.
- `FileAuthorizationPolicy` authorizes object access using trusted request context, owner ID, and scan status.
- Download/preview resolve the owned metadata row first, reject non-`success` scan states, and only then ask a storage-control port for a presigned URL.
- At the Phase 3 checkpoint, URL signing still used a temporary legacy adapter after Core authorization; this was replaced in Phase 4 by `GrpcStorageControlAdapter` against the narrow Go Storage Control service.
- File list avoids the current Go behavior that synchronously stats each successful object during list rendering.

## Evidence

- `node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs` passes and now covers file read/access wiring.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` now passes for 15 active endpoints and 2 fail-closed compatibility routes.
- `go test ./...` passes in the current workspace.
- `go vet ./...` passes in the current workspace.
- `npm run build` in `forward_part/static` passes.
- C++ build remains unverified in this Windows workspace because CMake cannot find the Drogon package (`DrogonConfig.cmake` / `drogon-config.cmake`), even though MSVC is now discoverable.

## Exit Criteria Assessment

- Unified file authorization policy: source-implemented for owner + `success` scan-state access.
- File list via MySQL only: source-implemented behind flag; runtime DB validation pending.
- Download/preview Core decisions: source-implemented; URL signing is now handled through Storage Control after Phase 4.
- Closed old file read flag without calling `file_srv`: complete after Phase 6/7 source cutover and legacy service removal.
- Runtime unauthorized matrix and object-storage call counter tests: pending C++ build and middleware/fake-storage test harness.

## Review Decision

Phase 3 was code-ready for toolchain and middleware validation at the review checkpoint. Its remaining storage-control dependency has since been addressed by Phase 4 and the Phase 6/7 source cutover.
