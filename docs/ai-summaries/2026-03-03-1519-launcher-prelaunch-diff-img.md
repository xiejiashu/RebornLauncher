# AI Task Summary - 2026-03-03 15:19:01 (Asia/Tokyo)

## 1) User request (quote)
> “增加个功能。就是远程的Version.dat文件跟本地的Version.dat里面对比出来的差异化的img文件，也直接下载完再启动。。。加在Launcher里面。之前是先下完运行时再启动的，现在img也是先下载完再启动。。。”

## 2) What was done
- Updated launcher manifest refresh flow to compute differential `.img` entries by comparing local `Version.dat` file map and remote `Version.dat` file map.
- Appended those differential `.img` files into prelaunch `runtimeList`, so they are included in `DownloadRunTimeFile()` before client launch.
- Hardened runtime updater behavior for `.img`: disabled deferred-on-busy fallback for `.img` runtime items, so these files must finish download before launch continues.
- Built `RebornLauncher` Debug target to verify compile success.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadManifest.cpp`
  - Added manifest path normalization + `.img` detection helpers.
  - Added differential compare logic (`local files` vs `remote files`) based on `Version.dat` entries.
  - Appended differential `.img` entries into `m_versionState.runtimeList` (deduplicated), with log output for appended count.
- `RebornLauncher/WorkThreadRuntimeUpdater.cpp`
  - Added `.img` extension check helper.
  - Changed deferred behavior: `.img` runtime downloads now use `allowDeferredOnBusy = false`.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts and ordering metadata.

## 4) Rationale
- `UpdateForge` currently populates `runtime` primarily with executable/runtime-critical file types, not `.img`, so changed `.img` files can be left to in-game on-demand flow.
- By deriving differential `.img` directly from `Version.dat` compare at manifest refresh time and injecting into prelaunch runtime queue, launcher now downloads those `.img` updates before game process startup.
- Disabling deferred fallback for `.img` prelaunch items prevents "queued for later" behavior from violating the requirement.

## 5) Risks + rollback
- Risk:
  - If a required `.img` is locked by another process, update now fails fast instead of deferring, which may block launch until lock is cleared.
  - If local manifest is missing/empty, no differential baseline exists; appended differential `.img` list is skipped in that case.
- Rollback:
  - Revert `RebornLauncher/WorkThreadManifest.cpp` and `RebornLauncher/WorkThreadRuntimeUpdater.cpp` to previous versions.
  - Keep existing runtime-only `.img` fetching behavior.

## 6) Follow-ups/TODO (optional)
- Add a launcher integration test that verifies:
  - changed `.img` between local/remote `Version.dat` is appended to prelaunch runtime queue;
  - launcher does not call `LaunchGameClient()` until those `.img` downloads finish.
