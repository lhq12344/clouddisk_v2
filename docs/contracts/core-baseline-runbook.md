# Core API Baseline Runbook

Date: 2026-09-05

This runbook captures the Phase 0 one-command baseline expectation before endpoint cutover begins.

## Prerequisites

- OpenResty and the current gateway stack are running locally.
- A valid JWT is available for authenticated endpoints, or a test username/password is available for the runtime smoke runner to call `/user/signin`.
- For Bash: `curl`, `awk`, and `python3` are available for `test/perf_test.sh`.
- For PowerShell: Windows PowerShell or PowerShell 7 is available.

## Command

```bash
JWT='<valid token>' scripts/project_start_scripts/core_api_baseline.sh
```

On Windows PowerShell, use the equivalent runner:

```powershell
$env:JWT = '<valid token>'
.\scripts\project_start_scripts\core_api_baseline.ps1
```

Optional environment variables:

- `BASE_URL` defaults to `http://127.0.0.1:2024`.
- `JWT` enables authenticated metadata/control endpoint probes.
- `SMOKE_SIGNIN_USERNAME` and `SMOKE_SIGNIN_PASSWORD` let the PowerShell baseline runner acquire a JWT automatically when `JWT` is not set.
- `DURATION`, `QPS`, and `CONCURRENCY` tune each latency probe.
- `RESULT_DIR` controls where timestamped baseline logs are written.
- `PROJECT_ROOT` can override repository root detection when running from a non-standard checkout path.
- `PERF_SCRIPT` can override the latency helper path.

The PowerShell runner supports the same `BASE_URL`, `JWT`, `DURATION`, `QPS`, `CONCURRENCY`, and `RESULT_DIR` environment variables. Its request pacing is sequential, so use it for local comparability checks; use the Bash runner for higher-throughput Linux/K8s baseline captures.

## Runtime Smoke Before Baseline

Before collecting performance numbers, run the Core runtime smoke once against
the same stack. This verifies `/ready`, no-legacy upload proxy behavior,
multipart presigned PUT, completion, query, and optional scan/download/preview
behavior. If the uploaded fixture is still `pending_scan`, cleanup is skipped;
add `-WaitForScanSuccess` when the Storage Worker and ClamAV are active to also
verify download/preview and delete the test relation.

```powershell
$env:BASE_URL = 'http://127.0.0.1:2024'
$env:JWT = '<valid token>'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_smoke.ps1 -RequireJwt
```

If a JWT is not already available, provide `SMOKE_SIGNIN_USERNAME` and
`SMOKE_SIGNIN_PASSWORD`; the smoke runner will call `/user/signin` and use the
returned token for authenticated probes.

When Storage Worker and ClamAV are active, add `-WaitForScanSuccess` to include
download and preview presign checks after scan success.

## Required Baseline Targets

- `/health`
- `/user/info`
- `/file/query`
- `/file/initupload`
- `/file/PresignParts`
- `/file/CompleteMultipart`

Authenticated endpoints without a JWT are skipped rather than reported as success.

## Exit Criteria

- Baseline output directory contains one log per attempted endpoint.
- Each authenticated baseline result records status-code distribution and p50/p90/p99 latency.
- Results are attached to the migration review before Phase 1 starts.
- If `/health` is unavailable, the script exits before recording endpoint latency so failed connections cannot be mistaken for a valid baseline.
