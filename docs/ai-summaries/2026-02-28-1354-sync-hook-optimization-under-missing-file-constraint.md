# AI Summary - Sync Hook Optimization Under Missing-File Constraint

## 1) User request (quote)
> "Async download cannot be used because the game process exits if it detects a missing file. Without exe source, optimize under this synchronous constraint."

## 2) What was done
- Kept the hook workflow synchronous for missing files (no async conversion).
- Optimized hot-path overhead while preserving blocking behavior when a download is required.
- Added bounded WinHTTP timeouts to reduce long stalls when launcher communication is slow/unavailable.
- Refined attribute refresh logic: re-query file attributes only when this hook call actually requested a download.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `EndsWithImgExtension(const CHAR*)` to avoid per-call string allocations for extension checks.
  - Added `#include <cstring>` for `std::strlen`.
  - `RequestDownloadFromLauncher` now sets WinHTTP timeouts via `WinHttpSetTimeouts(hSession, 300, 500, 1200, 1200)`.
  - `HandleHookedFileCheck` now returns `bool requestedDownload`.
  - In `HookedGetFileAttributesA`, second `g_originalGetFileAttributesA` call is now conditional: only done when download was requested.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line about mixed-encoding apply-patch context failure and the fix.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts/order.

## 4) Rationale
- The executable requires a blocking “file must exist now” contract, so async is not viable.
- Most `.img` calls do not need to trigger download; avoiding redundant extension parsing and redundant second attribute queries reduces hook overhead.
- Timeout bounds keep worst-case synchronous waits more predictable.

## 5) Risks + rollback
- Risk: tighter HTTP timeouts may increase failure probability on very slow systems.
- Rollback:
  - Remove or relax `WinHttpSetTimeouts` values.
  - Revert conditional second attribute query to unconditional if strict post-check visibility is preferred.

## 6) Follow-ups/TODO (optional)
- Add separate timeout profile for first-attempt vs retry to balance responsiveness and robustness.
- Introduce bounded cache eviction (`g_downloadRequestTicks`, `g_fileCheckCache`) to control memory growth.
