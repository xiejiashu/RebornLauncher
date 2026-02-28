# AI Summary - MD5 Mismatch Async Queue Download

## 1) User request (quote)
> "Keep missing-file handling synchronous, but if file exists and MD5 mismatches, submit to launcher async queue; do not wait. Mark submitted files and avoid duplicate async submissions for 5 minutes."

## 2) What was done
- Implemented split dispatch policy in `CryptoLoader`:
  - Missing file -> synchronous `/download`.
  - Existing file with MD5 mismatch -> asynchronous `/download?mode=async`.
- Added 5-minute anti-duplicate window for async mismatch submissions in `CryptoLoader`.
- Implemented async queue processing in `RebornLauncher` web service:
  - `/download?mode=async` enqueues and returns immediately (`202`).
  - Background worker consumes queue and downloads sequentially via existing flow.
- Kept sync behavior for missing files to satisfy game executable constraints.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `DownloadRequestMode` and `DownloadDispatchResult`.
  - Added async cooldown state (`g_asyncDownloadRequestTicks`) and 5-minute gating helpers.
  - `RequestDownloadFromLauncher` now accepts mode and appends `mode=async` for async requests.
  - Timeout policy split:
    - Async submit: short timeout.
    - Sync missing-file download: long read timeout for completion.
  - Hook logic now dispatches:
    - MD5 mismatch -> async queue submit.
    - Missing file -> sync blocking request.
  - Refresh attributes only after sync request path.
- `RebornLauncher/WorkThreadWebService.cpp`
  - Added async queue data structures and worker thread.
  - Added async request detection (`mode=async` / `async=1`).
  - `/download` now:
    - Async: enqueue + immediate `202` (`QUEUED`/`ALREADY_QUEUED`).
    - Sync: run existing download flow and return final result.
  - Reused unified execution path for sync/async processing with flow lock.
  - Async queue deduped by normalized file page while queued/in-flight.

## 4) Rationale
- Game process can exit if missing file is not ready immediately, so missing files must remain synchronous.
- Existing-file MD5 mismatch does not require blocking in hook path; queueing update avoids long stalls.
- Queue-based launcher handling prevents hook path from waiting on actual transfer while still guaranteeing eventual update.

## 5) Risks + rollback
- Risk: async queued updates may lag behind immediate gameplay access for mismatched-but-existing files.
- Risk: full `RebornLauncher` link step failed locally because target exe file was locked (`LNK1104/LNK1168`), but compile step for updated source succeeded.
- Rollback:
  - Revert `/download` async branch and worker logic.
  - Revert CryptoLoader mode split and route all requests back to sync.

## 6) Follow-ups/TODO (optional)
- Add queue length and processing latency logs to verify async throughput.
- Add bounded cleanup for async cooldown map in `CryptoLoader`.
