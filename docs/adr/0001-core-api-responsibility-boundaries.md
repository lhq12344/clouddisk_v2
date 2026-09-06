# ADR 0001: Core API Responsibility Boundaries

Date: 2026-09-05

Status: Accepted and source-implemented through Phase 7 cleanup

## Context

CloudDisk V2 currently routes HTTP requests through the Drogon gateway, then forwards core account and file operations to Go gRPC services. The gateway already performs JWT decoding and request-context enrichment, while the downstream account and file services still receive user identity and make database, cache, object-storage, and event decisions.

This creates unclear ownership for authentication, authorization, metadata writes, upload sessions, object storage commands, and scan events. It also makes migration risky because a single request can cross multiple processes before the business transaction is complete.

## Decision

The target architecture is a Drogon Core API with clear application/domain/infrastructure boundaries, plus narrow Go storage execution roles.

- Drogon Core API owns account lifecycle, JWT compatibility, file metadata, user-file relationships, upload session business state, permissions, and outbox writes.
- Storage Control remains a narrow object-storage credential boundary. It accepts already-authorized object keys and storage session IDs, then executes MinIO/S3 commands such as presign, multipart complete, abort, head, and presigned GET.
- Storage Worker owns asynchronous IO and compute work: Kafka consumption, Inbox/DLQ, virus scanning, object cleanup, reconciliation, and future preview/transcode/index jobs.
- Client data-plane upload/download bytes must not flow through Core API. Browser upload/download uses presigned URLs directly against MinIO/S3.
- OpenResty remains the edge routing layer for TLS, static assets, request ID propagation, CORS, coarse rate limiting, and upstream health routing.

## Data Ownership

Core API is the business owner for these tables and concepts:

- `accounts`
- `files`
- `user_files`
- `upload_sessions`
- `outbox`

Storage Worker owns these asynchronous execution concerns:

- `inbox`
- DLQ records
- worker leases
- scan attempt state

Storage Worker may update only authorized asynchronous result fields on `files`, such as scan status/detail/timestamp, and those updates must use expected-current-state guards. It must not make general account, permission, or user-file relationship decisions.

## Migration Constraints

- During migration, each use case has exactly one writer selected before request handling starts.
- Shadow comparison is allowed for reads only.
- Schema changes must be additive through the rollback window.
- Old HTTP paths and key JSON fields remain compatible until the frontend is explicitly migrated.
- Existing JWT issuer and claim names remain compatible across the cutover window.
- Verification-code success means atomically consuming pending registration and creating the account. The existing gateway behavior that only compares Redis code is treated as a bug, not the long-term contract.

## Consequences

- Drogon gains database and transaction infrastructure before business logic migrates.
- Account and business file gRPC adapters were temporary rollback points, not permanent architecture; they have been retired in the Phase 6/7 source cutover.
- `account_srv` and the business-shaped `file_srv` have been deleted from the current source topology after endpoint-level source cutover.
- Go remains valuable for storage-facing execution, but no longer owns account or file metadata business rules.

## Phase 0 Exit Criteria

- Current HTTP contract is documented for account, file, upload, and token endpoints.
- Core ownership and state-machine decisions are documented.
- Characterization tests cover high-risk migration semantics.
- A baseline script can exercise latency checks for the key metadata/control endpoints.
