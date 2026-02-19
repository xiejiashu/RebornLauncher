# 1) User request (quote)
> "Show the correct status over the pig head, not only percent, so I can understand what the launcher is doing."

# 2) What was done
- Added a shared launcher status channel in `WorkThread` so runtime stages can publish human-readable text.
- Updated update/launch workflow to continuously set stage text (bootstrap, manifest refresh, runtime update, per-file download, launch, run-client flow, service restart, failure states).
- Updated pig overlay renderer so each pig shows `status + file progress` instead of only percent.
- Updated idle pig renderer to display current status text under the big percent so users can see what the launcher is doing at any time.
- Updated right-side UI status labels to sync with live launcher stage text.
- Rebuilt `RebornLauncher` Release target successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.h`: added `SetLauncherStatus` / `GetLauncherStatus`, thread-safe status state storage.
- `RebornLauncher/WorkThreadRunCoordinator.cpp`: added major flow stage updates and failure stage text.
- `RebornLauncher/WorkThreadManifest.cpp`: added manifest/bootstrap stage text for success/failure.
- `RebornLauncher/WorkThreadRuntimeUpdater.cpp`: added per-file status text during runtime updates.
- `RebornLauncher/WorkThreadDownloadResume.cpp`: added download-path stage text (P2P/HTTP/failure).
- `RebornLauncher/WorkThreadChunkDownload.cpp`: added chunk/single-stream/fallback stage text.
- `RebornLauncher/WorkThreadClientState.cpp`: added launch status and quick-exit warning stage text.
- `RebornLauncher/WorkThreadWebService.cpp`: added RunClient/download/listener restart stage text.
- `RebornLauncher/LauncherSplashRenderer.h`: added per-pig and global status fields.
- `RebornLauncher/LauncherSplashRenderer.cpp`: pig label now renders status + progress; idle view now renders status line under percent.
- `RebornLauncher/LauncherP2PController.cpp`: status panel and file/total labels now reflect live stage text.
- `docs/agent-meta/hot-files-index.md`: updated top-15 file access counts.

# 4) Rationale
- Percent-only UI does not explain current step, which makes failures and waits indistinguishable.
- A shared status string lets different subsystems report a clear phase without tightly coupling renderer logic to internals.
- Showing status both on pig overlays and in the control panel keeps behavior understandable in both overlay-follow and idle modes.

# 5) Risks + rollback
- Risk: frequent status updates may overwrite quickly during rapid transitions; this is expected and still better than percent-only feedback.
- Rollback: revert status additions in `WorkThread.h` and call-sites, then revert renderer/UI label composition changes.

# 6) Follow-ups/TODO (optional)
- Add status localization (CN/EN) with string table keys.
- Add a short status history (last 3 steps) in UI for troubleshooting.
