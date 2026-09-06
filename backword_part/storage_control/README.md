# Storage Control

Storage Control is the narrow Go object-storage boundary used by the Drogon Core API.

It intentionally accepts only Core-authorized object keys and storage upload IDs. It must not decide user identity, file ownership, quota, deduplication, or metadata transactions.

## RPC Surface

- `InitiateMultipart`
- `PresignPart`
- `ListParts`
- `CompleteMultipart`
- `AbortMultipart`
- `PresignGet`
- `HeadObject`

The current implementation owns the MinIO adapter behind this narrow RPC surface. Core API owns metadata and upload finalization; the retired business-shaped `file_srv` no longer participates in the runtime topology.
