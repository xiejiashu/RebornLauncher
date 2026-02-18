# AI Summary - version-dat-retry-on-500

## 1) User request (quote)
> "MapleReborn.7z extraction completed ... Failed to fetch Version.dat from http://127.0.0.1:3000/game/live/Version.dat, status: 500 ... 我本地瞬间下载完的，为啥下载Version.dat也会出错呢？"

## 2) What was done
- Traced the `Version.dat` fetch path in launcher flow (`RefreshRemoteVersionManifest`).
- Confirmed failure came from HTTP response status handling (single attempt and immediate fail on non-200).
- Added retry logic for `Version.dat` fetch to tolerate transient local server `500` responses.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadManifest.cpp`
  - Added retry wrapper for manifest fetch (`3` attempts, `250ms` delay).
  - Applied retries to both primary manifest path and fallback path.
  - Extended failure log with retry count.
- `docs/agent-meta/hot-files-index.md`
  - Updated accessed-file counters.
- `docs/agent-meta/ai-mistake-log.md`
  - Appended one line for a PowerShell binary/string response handling mistake during diagnostics.

## 4) Rationale
- A one-shot request is brittle when local service occasionally returns transient `500`.
- Retrying at launcher side removes false-negative update failures without changing manifest parsing behavior.

## 5) Risks + rollback
- Risk: startup can be delayed by up to about `500ms` on repeated failures.
- Mitigation: only applies when fetch fails; successful first request is unchanged.
- Rollback:
  - Revert `RebornLauncher/WorkThreadManifest.cpp`.

## 6) Follow-ups / TODO (optional)
- If `500` continues, inspect local server logs on port `3000` for root cause (file lock, route exception, or upstream dependency error).
