# 1) User request (quote)
> User reported: right-click hide works, but auto-move pig to bottom-right was removed, and left-click drag still did not move the pig window.

# 2) What was done
- Restored "pig auto move to bottom-right" as a one-shot dock animation.
- Made manual drag take priority by canceling dock animation on left-button drag start.
- Kept tray-hide behavior intact (no forced re-show from animation loop).
- Restarted dock animation when launcher is restored from tray / shown for download.
- Avoided z-order hijacking during animation (`SWP_NOZORDER`, no topmost forcing).
- Verified with `RebornLauncher` `Release` compile.

# 3) Changes (paths + brief)
- `RebornLauncher/LauncherSplashRenderer.h`
  - Added `RestartDockToCorner()` and `CancelDockToCorner()`.
  - Added `m_dockToCornerEnabled`.
- `RebornLauncher/LauncherSplashRenderer.cpp`
  - Implemented restart/cancel dock helpers.
  - `UpdateDockAnimation` now runs only when dock is enabled and window is visible.
  - Removed forced topmost/show behavior from `SetWindowPos`.
  - Auto-disables dock animation after finishing once.
- `RebornLauncher/RebornLauncher.cpp`
  - `WM_CREATE`: trigger dock animation once.
  - `WM_LBUTTONDOWN`: cancel dock before moving window with mouse drag.
  - Tray restore and `WM_SHOW_FOR_DOWNLOAD`: restart dock animation.

# 4) Rationale
- Previous fix stopped re-show issues but also effectively disabled docking behavior.
- Original always-on docking logic interfered with user drag and menu layering.
- One-shot docking + explicit cancel on drag gives both behaviors:
  - auto move to bottom-right
  - user can still drag/move manually afterwards.

# 5) Risks + rollback
- Risk: users may prefer no auto-dock on tray restore.
  - Rollback by removing `RestartDockToCorner()` calls in restore paths.
- Risk: if future code expects always-on docking, this one-shot model differs.
  - Rollback by not auto-disabling `m_dockToCornerEnabled` when animation finishes.

# 6) Follow-ups / TODO
- If needed, add a launcher option: "Auto dock to bottom-right on restore (on/off)".
