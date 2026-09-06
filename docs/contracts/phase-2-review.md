# Phase 2 Review: Account Read and Write Migration

Date: 2026-09-05

Supersession note: this file records the Phase 2 checkpoint. Phase 6/7 later
changed defaults to Core, retired legacy account/file adapters, and deleted the
old business-shaped services. Current status is tracked in `phase-6-review.md`
and `phase-7-review.md`.

## Scope Reviewed

- `/user/info` now has a Core read path behind `CORE_ACCOUNT_READS=core|shadow`.
- `/user/signin` now has a Core login path behind `CORE_ACCOUNT_LOGIN=core`.
- `/user/signup`, `/user/sendcode`, and `/user/code` now have Core registration paths behind `CORE_ACCOUNT_REGISTRATION=core`.
- Default behavior at this checkpoint remained legacy for account endpoints unless flags were explicitly enabled; current defaults are Core after Phase 6/7.
- Existing frontend HTTP paths and JSON expectations remain unchanged.

## Implementation Notes

- User info reads use `MysqlAccountRepository` and trusted request context (`ID`, `Name`) from the JWT filter.
- Login reads account credentials from MySQL, verifies the existing PBKDF2-MD5 password hash shape, issues a JWT with compatible `issuer=Signin`, `ID`, and `Name` claims, then reuses the existing Redis whitelist flow.
- Signup stores pending registration payloads under `pending_reg:<email>` with a 10-minute TTL and preserves the old password hash format.
- Verification checks the email-code Redis key, loads pending registration, creates the account, and deletes both pending registration and code keys. If the account already exists after pending registration is gone, verification returns success to preserve idempotency.
- Send-code now has a Core outbox path that writes `EMAIL_VERIFICATION_REQUESTED` to `outboxes`; `email_srv` accepts both legacy raw email Kafka messages and new JSON outbox payloads.

## Evidence

- `node scripts/project_start_scripts/verify_phase1_gateway_skeleton.mjs` passes and now covers account read/login/registration wiring.
- `node scripts/project_start_scripts/verify_core_http_contract.mjs` now passes for 15 active endpoints and 2 fail-closed compatibility routes.
- `go test ./...` passes.
- `go vet ./...` passes in prior review and should remain in the final pre-handoff gate.
- `npm run build` in `forward_part/static` passes.
- C++ build remains unverified in this Windows workspace because CMake cannot find the Drogon package (`DrogonConfig.cmake` / `drogon-config.cmake`), even though MSVC is now discoverable.

## Exit Criteria Assessment

- `userinfo` migration: source-implemented behind flag; runtime DB validation pending.
- Login/JWT migration: source-implemented behind flag; runtime MySQL/Redis/JWT validation pending.
- Signup/verification migration: source-implemented behind flag; runtime Redis/MySQL idempotency validation pending.
- Reliable email command: source-implemented through MySQL outbox; outbox relay and email worker runtime validation pending.
- Deleting account gRPC/proto/startup dependencies: completed at source level in Phase 6/7; runtime observation still requires middleware-backed smoke evidence.

## Review Decision

Phase 2 was code-ready for toolchain and middleware validation at this checkpoint. Later phases completed file migration, source cutover, and old-service deletion; production-like validation remains gated on C++ build plus MySQL/Redis/Outbox/Kafka runtime tests.
