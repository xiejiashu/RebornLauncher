# 1) User request (quote)
> "What causes this error in logs? Is it code issue or remote service issue?"

# 2) What was done
- Analyzed the reported log sequence and mapped it to current download flow behavior.
- Confirmed from behavior pattern that the mismatch is most likely remote content inconsistency (manifest expects 3047, server repeatedly returns 2947 for same URL).
- Added a local robustness fix in download retry path to avoid false retries when stale/locked local file cannot be reset.
- Rebuilt `RebornLauncher` Release target successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadDownloadResume.cpp`
  - Added `resetLocalFile()` to enforce local file cleanup/truncation before retry.
  - Added explicit reset-failure logs (`UF-DL-RESET`) when local file cannot be reset.
  - Retry now aborts if local file reset fails (instead of retrying against stale local state).
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 file access counts.

# 4) Rationale
- The same URL failed twice with the same `actual_size=2947` against `expected_size=3047`, strongly indicating deterministic source-side mismatch.
- Previous retry path could silently continue even when local cleanup failed, making diagnostics less clear.
- Explicit reset verification improves distinction between remote mismatch and local file-state problems.

# 5) Risks + rollback
- Risk: stricter local reset check may fail earlier when local files are locked by external processes.
- Rollback: revert `RebornLauncher/WorkThreadDownloadResume.cpp` to previous retry cleanup behavior.

# 6) Follow-ups/TODO (optional)
- Verify remote `Version.dat` entry for `config.ini` against actual file bytes and MD5 on origin.
- Purge CDN/cache for `/game/live/202602111151/config.ini` if cached stale copy exists.
- Add response metadata logging (HTTP status/content-length/content-range) for faster remote triage.