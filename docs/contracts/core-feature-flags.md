# Core API Migration Feature Flags

Date: 2026-09-05

Flags are scoped by use case so cutover and post-cutover experiments do not require switching the entire service at once. Write paths must never dual-write. Read paths may run a shadow compare while a single authoritative response path is selected. After Phase 7, retired `account_srv` and business-shaped `file_srv` adapters are no longer available; explicit `legacy` values fail closed instead of reviving old services.

| Flag | Type | Current default | Controls | Legacy / compatibility behavior |
|---|---|---:|---|---|
| `core.account.reads` | read | `core` | `/user/info` | Explicit `legacy` fails closed because the legacy account adapter has been removed. |
| `core.account.login` | write | `core` | `/user/signin`, JWT whitelist behavior | Explicit `legacy` fails closed because the legacy account adapter has been removed. |
| `core.account.registration` | write | `core` | `/user/signup`, `/user/code`, `/user/sendcode` | Core verification consumes pending registration and creates the account atomically. |
| `core.file.reads` | read | `core` | `/file/query`, hash resolve | File list reads MySQL metadata only. |
| `core.file.access` | read/control | `core` | `/file/download`, `/file/showfile` | Core authorizes access and asks Storage Control for presigned GET. |
| `core.upload.control` | write/control | `core` | `/file/initupload`, `/file/PresignParts`, `/file/Status`, `/file/AbortMultipart` | Core owns upload session state; Storage Control only executes object-storage commands. |
| `core.upload.complete` | write | `core` | `/file/CompleteMultipart`, dedup/秒传 finalization | Core calls Storage Control complete, then writes metadata/relation/outbox atomically. |
| `core.file.delete` | write | `core` | `/file/delete` | Core removes user relation and queues object cleanup if it was the last reference. |
| `core.small_upload.direct` | write/data-plane | `core` | Frontend upload selection; `/file/upload` compatibility route | Current UI uses multipart presigned PUT for all file sizes; the old proxy route is retained only to fail closed. |
| `core.outbox.relay` | process role | `core` | Outbox dispatch ownership | Standalone relay is the active owner; the old embedded dispatcher has been removed with `file_srv`. |

## Rules

- Flag evaluation happens before the use case starts.
- A write request must have one and only one authoritative implementation.
- Shadow reads must log mismatch details without changing response shape.
- Schema remains additive-compatible through the observation window; recovery must not require dropping new columns or tables.
- Every flag/profile change requires contract tests and the relevant smoke test before traffic use.
