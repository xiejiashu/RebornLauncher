# AI Task Summary - External Launch Foreground + Starting State

## 1) User request (quote)
> 嗯，如果玩家双击了exe文件或图标。就直接把猪猪拉到最前面的层级。然后猪猪显示启动中。。。但是刚起的exe就没必要异步了。如果刚启的exe把目标猪猪杀掉了就走正常启动路径。

## 2) What was done
- Added a cross-process window notification path so the newly started launcher process can tell the running launcher to come to the foreground and show a "starting" status immediately.
- Kept external second-launch flow synchronous (no async dispatch in the newly started exe).
- Tightened stale-instance recovery condition:
  - If `/RunClient` returns an HTTP status (service responded), do not kill target process.
  - Only when `/RunClient` has no response (`httpStatus == 0`) do stale-process terminate + mutex takeover recovery.
- Existing successful stale takeover still continues into normal startup path.

## 3) Changes (paths + brief)
- `RebornLauncher/framework.h`
  - Added `WM_EXTERNAL_RUNCLIENT_REQUEST` custom message id.
- `RebornLauncher/RebornLauncher.cpp`
  - Added running-launcher window discovery via `EnumWindows`.
  - Added `NotifyRunningLauncherStartRequest()` to send `WM_EXTERNAL_RUNCLIENT_REQUEST` to the active instance.
  - In single-instance branch:
    - call `NotifyRunningLauncherStartRequest()` before `/RunClient`.
    - only trigger kill-and-takeover when no HTTP response exists.
  - In `WndProc`, added `WM_EXTERNAL_RUNCLIENT_REQUEST` handling:
    - restore/show window,
    - force z-order promotion to front,
    - call `SetLauncherStatus(L"启动中...")`.

## 4) Rationale
- User intent is immediate visual feedback and focus transfer when launching from icon/exe while an instance already runs.
- Distinguishing "HTTP responded with error" from "HTTP unreachable" prevents unnecessary process termination.
- Keeping the second exe path synchronous is safe because it is a separate process and does not block the existing UI message loop.

## 5) Risks + rollback
- Risk: foreground activation can still be limited by OS focus rules in edge cases; fallback remains restore + topmost toggle.
- Risk: if window enumeration matches an unexpected same-path process window, message may target wrong window (mitigated by same executable path and skipping self pid).
- Rollback:
  - Revert changes in `framework.h` and `RebornLauncher.cpp` for custom message + external-launch notify path.

## 6) Follow-ups / TODO (optional)
- Optionally add a small timeout-based reset from "启动中..." to idle text if `/RunClient` is not received quickly.
