# 1) User request (quote)
> “我点了Hide To Tray。虽然右下角有多出个图标。但是猪还是显示状态。。。所以修复下它。。。如果它在Tray状态下。只有出现下载时才显示猪。下载完就不显示。还有就是能不能把所有菜单名，还有显示的状态名都用中文显示。。。”

# 2) What was done
- Implemented a tray-state flow in `RebornLauncher`:
  - manual hide-to-tray state
  - temporary show while synchronous client download is running
  - auto-hide back to tray after download ends
- Updated synchronous `/download` flow to post explicit show/hide window messages.
- Localized launcher menus and visible UI status labels to Chinese (using `\u` escapes for encoding safety).
- Added centralized launcher status translation so existing `SetLauncherStatus(...)` English messages render as Chinese in UI.
- Built `RebornLauncher` (`Release`, `ClCompile`) successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/framework.h`
  - Added `WM_SHOW_FOR_DOWNLOAD` and `WM_HIDE_AFTER_DOWNLOAD`.
- `RebornLauncher/RebornLauncher.cpp`
  - Added manual tray mode state flags.
  - Right-click main menu/tray menu labels changed to Chinese.
  - Added handling for show/hide download messages.
  - `WM_DELETE_TRAY` now respects manual tray mode.
  - Manual restore/hide actions now update tray state flags consistently.
- `RebornLauncher/WorkThreadWebService.cpp`
  - Replaced sync path restore message with `WM_SHOW_FOR_DOWNLOAD`.
  - Added `WM_HIDE_AFTER_DOWNLOAD` on sync download success/failure.
- `RebornLauncher/LauncherP2PController.h`
  - Default animated status text changed to Chinese.
- `RebornLauncher/LauncherP2PController.cpp`
  - Main UI labels and runtime status display changed to Chinese.
  - P2P status prefixes changed to Chinese.
- `RebornLauncher/LauncherSplashRenderer.h`
  - Default global status text changed to Chinese.
- `RebornLauncher/LauncherSplashRenderer.cpp`
  - Empty-status fallback text changed to Chinese.
- `RebornLauncher/WorkThread.h`
  - Default launcher status changed to Chinese.
  - Added centralized status translation mapping/prefix rules.
  - `SetLauncherStatus` now stores translated Chinese status text.
- `docs/agent-meta/hot-files-index.md`
  - Updated top file access counts.

# 4) Rationale
- Previous `WM_DELETE_TRAY` behavior always restored window, which broke “manual tray hide” intent.
- Explicit show/hide messages for synchronous download make behavior deterministic:
  - tray mode + download start => show launcher
  - download end => return to tray
- Centralized translation avoids editing many workflow files and keeps status text output consistent.
- `\u` escaped wide strings reduce mixed-encoding risk in this repo’s existing files.

# 5) Risks + rollback
- Risk: translation map may miss some rare/new status strings, which would still show English.
  - Mitigation: fallback prefix translation exists for common dynamic status patterns.
- Risk: tray behavior can still be affected by older message paths (e.g. other restore posts).
  - Mitigation: manual tray mode now gates `WM_DELETE_TRAY`.
- Rollback:
  1. Revert message constants and tray-state logic in `RebornLauncher.cpp` + `framework.h`.
  2. Revert `/download` message posts in `WorkThreadWebService.cpp`.
  3. Revert localization changes in controller/splash/workthread files.

# 6) Follow-ups / TODO
- Extend translation map when new status strings are added.
- If needed, move status localization to dedicated i18n config/resource files.
