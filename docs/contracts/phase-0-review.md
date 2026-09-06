# Phase 0 Review: Contract and Decision Lock

Date: 2026-09-05

## Scope Reviewed

- ADR for Core API ownership and storage role boundaries.
- Frontend-compatible HTTP contract for account, file metadata/access, and multipart endpoints.
- Use-case scoped feature flags and rollback rules.
- Baseline runbook and executable baseline helper.
- Windows-compatible baseline helper for this workspace.
- Characterization tests for verification-code account creation, JWT compatibility, file authorization, dedup safety, async deletion, upload state transitions, atomic complete, and multipart ownership.

## Evidence

- `go test ./internal/core_contract` passes.
- `go test ./...` passes for all Go packages in the current workspace.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` passes for the current frontend API and Drogon route declarations.
- `bash -n scripts/project_start_scripts/core_api_baseline.sh` passes after using system Bash for syntax-only validation.
- Baseline script now resolves the repository root from its own path and fails closed before creating result files when `/health` is unavailable, preventing false baseline logs from connection-refused responses.
- `scripts/project_start_scripts/core_api_baseline.ps1` parses successfully and follows the same fail-closed `/health` precheck.
- `go vet ./...` passes.
- `npm run build` in `forward_part/static` passes after installing dependencies with a repository-local npm cache.
- Live `http://127.0.0.1:2024/health` probe fails in this workspace because the local stack is not running.

## Exit Criteria Assessment

- ADR documents target boundaries, table ownership, and Storage Control/Worker responsibilities: complete.
- Existing frontend-facing endpoints have a source-level contract check: complete.
- High-risk migration semantics have executable characterization tests: complete for pure domain/use-case contracts; live Drogon integration tests remain a Phase 1+ implementation dependency.
- One-command baseline harness exists for Bash and PowerShell: complete, but live latency numbers still require a running local stack and valid JWT. The existing `start_all.sh` is Linux-path-bound to `/home/lihaoqian/...`, so this Windows workspace should capture live baselines only after the intended runtime is actually listening rather than silently starting an unintended partial stack.
- Feature flags are defined by use case with unique-writer rollback rules: complete.

## Review Decision

Phase 0 is implementation-ready, but Phase 1 should wait until live baseline results are captured in an environment where OpenResty/gateway and a valid JWT are available.

Do not begin Phase 1 implementation without attaching those baseline logs to the migration review, because the plan requires latency/RSS comparison before changing traffic paths.
