# AI Summary (2026-02-12 15:48:19 JST)

## 1) User request (quote)
> 没有游戏进程在运行时，让不规则窗口(一只猪)显示在最前面，这样便于玩家右键关闭。

## 2) What was done
- Updated pig overlay z-order behavior so that when no game window/process is tracked, the irregular pig window is shown as topmost.
- Kept existing follow behavior while game windows exist, so overlay remains aligned with tracked game windows and is not forced to global topmost in follow mode.
- Added a small state guard to avoid calling `SetWindowPos` to topmost on every UI refresh frame.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added `g_idleTopmost` state flag.
  - In `RefreshPigOverlayState(...)`:
    - when following game windows (`hasBounds == true`): reset idle-topmost flag.
    - when no game windows are available: switch overlay window to `HWND_TOPMOST` at saved window position and mark idle-topmost active.
- `docs/agent-meta/hot-files-index.md`
  - Incremented access count for `RebornLauncher/RebornLauncher.cpp`.

## 4) Rationale
- User interaction target is right-click on pig window to close; when no game is running, making the irregular window topmost improves clickability and discoverability.
- Guarding repeated topmost calls avoids unnecessary z-order churn in the 15ms UI update loop.

## 5) Risks + rollback
- Risk: In no-game state, the pig window now stays topmost, which can overlap other desktop apps until user closes or minimizes it.
- Rollback: Revert `RebornLauncher/RebornLauncher.cpp` changes in `RefreshPigOverlayState(...)` to use `HWND_NOTOPMOST` in idle state and remove `g_idleTopmost`.

## 6) Follow-ups / TODO (optional)
- Optional: If needed, add a user setting to control idle topmost behavior (on/off) without code changes.
