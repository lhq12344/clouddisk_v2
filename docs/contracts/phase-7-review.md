# Phase 7 Review: Legacy Service Removal Readiness

Date: 2026-09-05

## Scope Reviewed

- Added `scripts/project_start_scripts/verify_legacy_service_removal_ready.mjs`
  as a hard pre-delete gate for `account_srv` and business-shaped `file_srv`.
- Simplified `forward_part/config/nginx/nginx.conf` Core API routing from many
  duplicated exact `location` blocks into two regex locations: one streaming
  compatibility block for `/file/upload` and `/file/uploadpart`, and one common
  Core API proxy block for account/file/AI routes.
- Added `scripts/project_start_scripts/verify_openresty_core_routes.mjs` to
  source-verify route coverage and ensure duplicate proxy blocks do not creep
  back in.
- Added `scripts/project_start_scripts/verify_core_source_suite.ps1` to run the
  full local source gate set in one command and classify missing C++ SDKs as
  external blockers instead of source failures.
- Added `scripts/project_start_scripts/verify_gateway_core_logic_compile.ps1`
  to compile the Drogon-free Core domain/application seam and included it in
  the consolidated source suite.
- Added `scripts/project_start_scripts/verify_core_runtime_smoke.ps1` as the
  follow-up no-legacy runtime smoke runner for `/ready`, fail-closed upload
  compatibility routes, multipart presigned PUT, complete, query, and optional
  scan/download/preview validation once middleware is running.
- Added `docs/contracts/core-migration-completion-audit.md` to map every
  plan-level acceptance criterion to current source evidence and remaining
  runtime proof.
- Checked whether the repository can already satisfy the Phase 7 requirement:
  full tests and smoke checks without building or starting the legacy account
  and file business services.
- Deleted `backword_part/account_server/account_srv`,
  `backword_part/file_server/file_srv`, `proto/account_srv`, and
  `proto/file_srv` after moving remaining MCP and multipart test callers to
  Core-owned MySQL reads plus Storage Control.

## Findings

- Phase 7 source deletion gate now passes: legacy business service directories
  and account/file proto directories are removed.
- Gateway controllers and CMake no longer reference account/file gRPC protobufs
  or Consul lookup fallbacks.
- Local gateway config, Nacos bootstrap config, and C++ config parsing no
  longer retain legacy service entries as Core dependencies.
- Startup/status/stop scripts no longer manage `account_srv` or `file_srv`.
- OpenResty route duplication is now reduced at source level; runtime syntax
  validation is still pending an environment where OpenResty/nginx can run.
- The consolidated source suite passes all runnable source gates, including the
  Gateway Core compile probe, and reports full CMake configure as blocked on the
  missing Drogon CMake package.
- Storage Worker scan result writes now use a database-level
  `status = pending_scan` guard and treat terminal scan states as converged,
  preventing replay/concurrency from regressing `success`, `infected`, or
  `scan_failed` files.
- Gateway config loading now accepts `CLOUDDISK_CONFIG_JSON`,
  `CLOUDDISK_CONFIG_FILE`, and `CLOUDDISK_CONFIG_VERSION` before falling back to
  local config files, so runtime bootstrap can be injected consistently without
  reintroducing retired service keys.

## Decision

Phase 7 source cleanup is complete. The remaining gap is runtime certification:
the gateway still cannot be compiled in this Windows workspace because CMake
cannot find the Drogon package and now fails fast with an actionable `Drogon was
not found` message,
and no live no-legacy smoke run has been executed against the middleware stack
yet.
