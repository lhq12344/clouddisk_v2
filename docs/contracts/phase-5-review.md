# Phase 5 Review: Outbox Relay Split

Date: 2026-09-05

Supersession note: this review captures the relay split checkpoint. Phase 7
subsequently removed the legacy business-shaped `file_srv`; the standalone
`outbox_relay` is now the only active source-level outbox dispatcher owner.

## Scope Reviewed

- Extracted reusable Outbox dispatcher logic to `internal/outboxrelay`.
- Added standalone `backword_part/outbox_relay` process.
- At this checkpoint, the legacy `file_srv` embedded dispatcher had been disabled by default. Phase 7 later removed the legacy service and that embedded dispatcher path entirely.
- Updated start/status/stop scripts to manage `outbox_relay` independently.
- Added Core async delete source path behind `CORE_FILE_DELETE=core`.
- Added storage-worker handling for `OBJECT_DELETE_REQUESTED` events.
- Added an audit-only storage-worker reconciliation loop and a structured
  report for backlog, stale leases, scan latency, incomplete upload sessions,
  missing metadata objects, and bounded object-orphan detection.

## Implementation Notes

- The shared dispatcher preserves the existing outbox lease, `FOR UPDATE SKIP LOCKED`, retry, lock TTL, header propagation, and idempotent mark-sent/mark-failed behavior.
- `outbox_relay` owns Kafka sending in the target topology; after Phase 7, no `file_srv` dispatcher path remains.
- Shared dispatcher logic is preserved in `internal/outboxrelay.NewDispatcher` and used by the standalone relay.
- Core delete removes only the requesting user's `user_files` relation in the HTTP transaction. If no references remain, it writes an `OBJECT_DELETE_REQUESTED` outbox event instead of deleting the object synchronously.
- Storage worker consumes `OBJECT_DELETE_REQUESTED`, re-checks zero references, deletes the object idempotently, soft-deletes the `files` record, and marks Inbox done.
- `other_srv/store_srv/reconciliation` starts with `store_srv` and performs an
  initial audit followed by a periodic audit (five minutes by default).
  `RECONCILIATION_INTERVAL_SECONDS`,
  `RECONCILIATION_PENDING_SCAN_AGE_SECONDS`, and
  `RECONCILIATION_OBJECT_AUDIT_LIMIT` bound the job. It records findings only:
  no reconciliation run updates MySQL or removes an object.

## Evidence

- `go test ./...` passes in the current workspace.
- `go vet ./...` passes in the current workspace.
- `go build ./backword_part/outbox_relay` passes.
- `go build ./internal/outboxrelay` passes.
- `npm run build` in `forward_part/static` passes.
- `node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs` passes and covers the relay split.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` passes for existing frontend/gateway endpoints.
- C++ build remains unverified in this Windows workspace because CMake cannot find the Drogon package (`DrogonConfig.cmake` / `drogon-config.cmake`), even though MSVC is now discoverable.
- Runtime Kafka/MySQL lease competition and backlog-drain validation are pending middleware startup.

## Exit Criteria Assessment

- Outbox dispatcher split from `file_srv`: source-implemented, then finalized by Phase 7 deletion of the legacy service.
- Standalone relay process: source-implemented and Go-build verified.
- Startup/status/stop integration: source-implemented.
- Single active owner in deployed runtime: pending live process validation.
- Async delete lifecycle: source-implemented; runtime MinIO/MySQL/Kafka replay validation pending.
- Inbox/DLQ/worker ownership: delete path reuses Inbox idempotency; the worker
  now reports stale Inbox leases, DLQ count, Outbox backlog and stale leases,
  plus scan and object reconciliation findings. Export to a metrics backend
  and live operational thresholds remain deployment work.

## Review Decision

Phase 5 code is ready for middleware-backed fault-injection validation. The
remaining acceptance gap is operational: start MySQL, Kafka, MinIO and ClamAV
to validate relay lease competition, Kafka replay, scan failure recovery, and
the reconciliation reports against real state.
