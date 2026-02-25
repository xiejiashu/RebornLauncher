# 1) User request (quote)
> "Move the pig to the bottom-right near taskbar. When launcher starts from double-click, pig should move from center to bottom-right with visible motion. Keep it always at bottom-right. No following game windows. No multiple pigs."

# 2) What was done
- Reworked splash renderer to single-pig mode only (removed multi-pig/game-window overlay behavior).
- Added dock target calculation based on Windows work area (taskbar-aware bottom-right position).
- Added smooth startup docking animation from current window position (center) to bottom-right.
- Added persistent docking enforcement so pig stays at bottom-right.
- Kept existing percent/status display in single-pig view and added current file line.
- Rebuilt `RebornLauncher` Release target successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/LauncherSplashRenderer.h`
  - Removed multi-pig overlay state.
  - Added docking state fields and helpers (`ComputeDockTargetPos`, `UpdateDockAnimation`).
- `RebornLauncher/LauncherSplashRenderer.cpp`
  - Replaced follow-window logic with fixed bottom-right docking logic.
  - `RefreshOverlayState` now updates status/file text and docking animation only.
  - `DrawScene` now always renders single pig (no game-window-follow branch).
  - `OnTimerTick` now updates docking each tick.

# 4) Rationale
- User requested a deterministic launcher mascot position and explicit movement transition on startup.
- Anchoring to `SPI_GETWORKAREA` keeps pig near taskbar while avoiding overlap with taskbar area.
- Removing follow-window and multi-pig paths eliminates behavior divergence and visual clutter.

# 5) Risks + rollback
- Risk: fixed docking may override user drag attempts (intended by request).
- Risk: docking position depends on current work area and can shift on display/taskbar changes.
- Rollback: revert `RebornLauncher/LauncherSplashRenderer.h` and `RebornLauncher/LauncherSplashRenderer.cpp`.

# 6) Follow-ups/TODO (optional)
- Optionally expose a setting for dock margin from taskbar edge.
- Optionally add easing duration config for startup movement speed.