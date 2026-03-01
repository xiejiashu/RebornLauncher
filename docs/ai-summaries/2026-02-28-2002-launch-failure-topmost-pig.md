# AI Summary - 2026-02-28 20:02 (Asia/Tokyo)

## 1) User request (quote)
> "RebornLauncher发新版本时老是启动不了游戏，一启动就闪退。如果游戏启动失败，把猪弹出来，并且浮在所有窗口之上。"

## 2) What was done
- Added launch-failure UI escalation in `WorkThread::LaunchGameClient()`.
- When game process creation fails, launcher now forces the pig window to show and sets it as topmost.
- When game exits almost immediately after launch (quick-exit detection), launcher also forces pig window show + topmost.
- Verified build with `cmake --build _builds --target RebornLauncher --config Debug`.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadClientState.cpp`
  - Added `notifyLaunchFailureUi` lambda in `LaunchGameClient`.
  - On `CreateProcessA` failure, call UI notification before returning `false`.
  - On quick-exit branch (`WaitForSingleObject(..., 100) == WAIT_OBJECT_0`), call UI notification.
  - Normalized one inline comment to ASCII to avoid encoding/mojibake risk.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts for files touched in this task.
- `docs/ai-summaries/2026-02-28-2002-launch-failure-topmost-pig.md`
  - Added this task summary.

## 4) Rationale
- Existing launcher already renders pig UI in the main window; the issue was visibility and z-order during launch failure.
- Triggering `WM_SHOW_FOR_DOWNLOAD` + `ShowWindow` + `SetWindowPos(HWND_TOPMOST)` on failure guarantees the pig window is brought forward and stays above other windows.
- Handling both process-creation failure and immediate-exit failure covers "启动就闪退" scenarios.

## 5) Risks + rollback
- Risk: after a failure, launcher remains topmost until user changes window state manually.
- Risk: quick-exit detection uses a 100ms window and may not catch slightly delayed crashes.
- Rollback:
  - Revert `RebornLauncher/WorkThreadClientState.cpp` to previous state.
  - Rebuild `RebornLauncher` target.

## 6) Follow-ups/TODO (optional)
- Consider adding explicit "clear topmost" behavior after a confirmed stable launch period (for example 3-5 seconds).
