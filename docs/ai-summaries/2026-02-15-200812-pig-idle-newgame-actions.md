# AI Summary

## 1) User request (quote)
> 猪在闲置的时候，右键对着它不仅可以退出，也可以启动一个新游戏。双击猪也行。

## 2) What was done
- Added a new idle action to start a new game from the pig window.
- Updated idle right-click menu to include `Start New Game` and `Exit`.
- Added idle double-click detection on the pig window to trigger new game launch.
- Reused the existing `/RunClient` request path for game launch consistency.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added menu command ID `ID_START_NEW_GAME`.
  - Added `RequestNewGameWithError(HWND)` helper.
  - Updated `WM_RBUTTONUP` idle popup menu to include `Start New Game`.
  - Updated `WM_COMMAND` handling for the new menu action.
  - Added idle double-click logic in `WM_LBUTTONDOWN` (time + position threshold).
- `docs/agent-meta/hot-files-index.md`
  - Incremented access count for `RebornLauncher/RebornLauncher.cpp`.

## 4) Rationale
- The launcher already supports launching extra clients via `/RunClient`; using this path avoids duplicate launch code.
- Restricting behavior to idle mode (`!IsFollowingGameWindows()`) matches the requested interaction and avoids overlay-state side effects.
- Double-click detection uses Windows double-click timing and distance thresholds for expected behavior.

## 5) Risks + rollback
- Risks:
  - Two rapid left-clicks in the idle area may be interpreted as a double-click and trigger launch.
  - If local web service is not ready, new game request will fail and show an error dialog.
- Rollback:
  - Revert with:
    - `git restore -- RebornLauncher/RebornLauncher.cpp docs/agent-meta/hot-files-index.md`

## 6) Follow-ups/TODO (optional)
- Optionally add `Start New Game` to tray icon right-click menu for interaction parity.
