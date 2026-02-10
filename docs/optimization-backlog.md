# UpdateForge / RebornLauncher Optimization Backlog

## Goal
Stabilize and harden the updater pipeline while keeping P2P-first download behavior.

## Phase 1 (Reliability)
- Enforce strict failure behavior: do not launch game if any required update file fails.
- Add bounded retry + exponential backoff + timeout budgets for all network calls.
- Add structured error codes in launcher UI and logs.
- Add download/session cancellation handling for clean shutdown.

## Phase 2 (Security)
- Sign `Version.dat` with asymmetric signature (recommended: Ed25519).
- Verify signature in launcher with embedded public key.
- Replace MD5-only trust with SHA-256 integrity checks.
- Add host allowlist / certificate pinning for update endpoints.

## Phase 3 (Update Safety)
- Normalize and validate all manifest paths; block path traversal.
- Download to temp + hash verify + atomic replace.
- Add rollback markers for interrupted update sessions.

## Phase 4 (Architecture)
- Split update core from UI (`manifest parse`, `download`, `verify`, `replace`).
- Keep launcher UI as a thin shell (render + progress + lifecycle).
- Define explicit state machine for update stages.

## Phase 5 (Observability & Tests)
- Add machine-readable update logs.
- Add integration tests for: partial downloads, hash mismatch, signature failure, resume correctness.
- Add fixture manifests for regression tests.
