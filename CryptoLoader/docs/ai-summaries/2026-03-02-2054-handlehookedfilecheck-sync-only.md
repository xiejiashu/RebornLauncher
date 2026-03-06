# 1) User request (quote)
> "[Translated] Modify dllmain.cpp so HandleHookedFileCheck always uses synchronous flow, not async requests."

# 2) What was done
- Updated `HandleHookedFileCheck` so MD5 mismatch dispatch now uses synchronous download request flow.
- Removed async-only request plumbing in `dllmain.cpp` to keep behavior consistent and avoid dead async paths.

# 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - MD5 mismatch branch now uses `ShouldRequestDownloadNow(...)` + synchronous request.
  - Removed async cooldown state/functions.
  - Simplified request API to sync-only (`RequestDownloadFromLauncher(const std::string& page)`).
  - Removed `mode=async` query construction and async-specific timeout/status handling.
- `docs/agent-meta/hot-files-index.md`
  - Added/initialized hot files index for this workspace.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for a path-resolution mistake during scan commands.

# 4) Rationale
- The requested behavior is to avoid async requests in `HandleHookedFileCheck`.
- Making the request path sync-only prevents accidental async dispatch and keeps all file-check-triggered downloads consistent.

# 5) Risks + rollback
- Risk: sync requests can block hook path longer under launcher/network delay.
- Rollback: revert `dllmain.cpp` to previous commit/state.

# 6) Follow-ups/TODO (optional)
- Consider reducing sync timeout values if hook latency becomes visible in runtime.
