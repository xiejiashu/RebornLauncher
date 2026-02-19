# 1) User request (quote)
> "When update fails, write logs under the program Logs directory, include error code and root cause, be detailed, and log every error."

# 2) What was done
- Added a unified launcher error logging facility writing to `Logs/update-YYYY-MM-DD.log`.
- Standardized log fields to include: timestamp, error code, source function, reason, details, Win32 error code/message, HTTP status, and HTTP library error code.
- Added error logging across update-critical paths: bootstrap fetch, manifest refresh/parse, runtime file selection/download, chunked and fallback downloads, web service launch/update endpoints, and game client launch failures.
- Kept logging thread-safe with a mutex to avoid interleaved lines from worker/web threads.
- Rebuilt `RebornLauncher` Release target successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.h`: introduced `LogUpdateError(...)`, log path/error formatting helpers, and inline implementation for writing detailed logs to `Logs`.
- `RebornLauncher/WorkThreadRunCoordinator.cpp`: added high-level error codes for init/base package/manifest/runtime/launch failures.
- `RebornLauncher/WorkThreadManifest.cpp`: added detailed logging for bootstrap HTTP/JSON/field/url failures and manifest HTTP/empty/parse failures.
- `RebornLauncher/WorkThreadRuntimeUpdater.cpp`: added logging for missing runtime entries and runtime download failures.
- `RebornLauncher/WorkThreadDownloadResume.cpp`: added logging when HTTP client is unavailable or HTTP resume fails.
- `RebornLauncher/WorkThreadChunkDownload.cpp`: added logging for URL parsing/session failures, probe failures, temp file failures, fallback failures, incomplete chunks, and rename failures.
- `RebornLauncher/WorkThreadWebService.cpp`: added logging for `/download` and `/RunClient` failure responses and listener restart/failure events.
- `RebornLauncher/WorkThreadClientState.cpp`: added logging for `CreateProcess` failures and quick-exit failures.
- `RebornLauncher/WorkThreadCrypto.cpp`: added logging for fatal crypto API/decompression failures.
- `RebornLauncher/WorkThreadLogging.cpp`: added lightweight translation unit (logging logic remains inline in header).
- `docs/agent-meta/hot-files-index.md`: updated hot file counts.

# 4) Rationale
- Previously, many failures only printed to console or returned false without durable diagnostics.
- Persistent structured logs under `Logs/` make field debugging possible on user machines.
- Including both symbolic error codes and low-level diagnostics (`Win32`, HTTP status/error) helps isolate whether failures are network, file system, parse, or process-launch related.
- Thread-safe append avoids corrupted lines under concurrent update + web-service activity.

# 5) Risks + rollback
- Risk: More synchronous file writes on failure paths (low overhead, because only on errors).
- Risk: Header-inline logging implementation increases compile-time impact slightly.
- Rollback:
  - Revert `RebornLauncher/WorkThread.h` and remove `LogUpdateError` call-sites from touched `.cpp` files.
  - Remove `RebornLauncher/WorkThreadLogging.cpp` stub if not needed.

# 6) Follow-ups/TODO (optional)
- Add a log retention policy (e.g., keep last N days / max size).
- Add optional `LogUpdateInfo`/`LogUpdateWarn` for non-fatal traceability.
- Surface latest error line in launcher UI status area for quick user reporting.