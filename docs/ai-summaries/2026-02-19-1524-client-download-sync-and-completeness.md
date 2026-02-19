# 1) User request (quote)
> "When launcher handles client download requests, it must complete synchronously before notifying the client, and must ensure files are fully downloaded."

# 2) What was done
- Confirmed `/download` is request-synchronous (response is sent only after handler returns), then hardened it to be flow-serialized with update/launch work.
- Added strict post-download verification in `DownloadWithResume` before treating download as success.
- Verification now checks file existence, expected size, and manifest MD5 (when available).
- If P2P download verifies false, launcher deletes the bad file and falls back to HTTP.
- If HTTP download verifies false, launcher retries HTTP once after deleting local partial file.
- `/download` now runs under launch flow mutex so manifest updates and client file requests do not race.
- Rebuilt `RebornLauncher` Release target successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadDownloadResume.cpp`
  - Added helpers to resolve file config and normalize MD5 compare.
  - Added strict integrity checks (exists/size/md5).
  - Added bad-file cleanup + retry/fallback behavior.
- `RebornLauncher/WorkThreadWebService.cpp`
  - Added `m_launchFlowMutex` guard in `/download` handler to enforce serialized completion before response.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 file access counts.

# 4) Rationale
- Existing code could return success when transfer path reported success but file integrity was not explicitly validated.
- Client should only be notified success after the file is fully usable.
- Serializing `/download` with update flow avoids state races during manifest refresh/runtime update.

# 5) Risks + rollback
- Risk: stricter validation can increase 502 responses when remote delivers inconsistent content.
- Risk: extra MD5 verification adds CPU cost for large files, but only on completed downloads.
- Rollback:
  - Revert `RebornLauncher/WorkThreadDownloadResume.cpp` and `RebornLauncher/WorkThreadWebService.cpp` to previous behavior.

# 6) Follow-ups/TODO (optional)
- Add per-file timeout budget + exponential backoff strategy for retry behavior.
- Add metrics counters for verify-fail causes (size mismatch vs md5 mismatch).
