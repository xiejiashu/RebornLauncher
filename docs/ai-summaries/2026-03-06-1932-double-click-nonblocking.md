# AI Task Summary - Double-click Non-blocking RunClient

## 1) User request (quote)
> 有点小瑕疵，双击猪猪的时候会卡住猪猪。。。

## 2) What was done
- Converted in-launcher “start new game” trigger to async dispatch so UI thread no longer blocks while waiting `/RunClient`.
- Added in-flight guard to prevent duplicate parallel launch requests from rapid double-click/menu clicks.
- Added shutdown wait for in-flight async request to finish, avoiding teardown races.
- Kept existing HTTP recovery/retry logic unchanged, but moved it off UI thread via async wrapper.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added `g_runClientRequestInFlight` atomic flag.
  - Added `RequestNewGameAsync(HWND)` helper using detached `std::thread`.
  - Switched double-click and tray/menu “Start New Game” to use async helper.
  - Added owner-window validation before showing error `MessageBox`.
  - Added shutdown wait loop before `updateCoordinator.Stop()` when request is still in progress.

## 4) Rationale
- Previous flow invoked `/RunClient` synchronously from window message handlers (`WM_LBUTTONDOWN` / command path), which blocks the launcher UI while remote update/launch work runs.
- Async dispatch preserves behavior but prevents visible UI freeze during request/timeout/recovery phases.

## 5) Risks + rollback
- Risk: detached worker thread increases lifecycle complexity; mitigated by in-flight flag + shutdown wait.
- Risk: repeated clicks during in-flight request are ignored by design.
- Rollback:
  - Revert `RebornLauncher/RebornLauncher.cpp` async wrapper and restore direct `RequestNewGameWithError(...)` calls.

## 6) Follow-ups / TODO (optional)
- Consider showing a small non-blocking “launch request in progress” status to improve UX feedback.
