# 1) User request (quote)
> “隐藏托盘后，猪还是显示状态。并且点左键按住猪也无法移动，并且猪的渲染会遮挡掉右键菜单。”

# 2) What was done
- Located root cause in splash renderer dock animation:
  - Every tick it called `SetWindowPos(... HWND_TOPMOST ... SWP_SHOWWINDOW)`.
  - That force-showed the window after tray hide, forced topmost z-order, and kept overriding manual move.
- Applied targeted fix in dock animation update:
  - Return early unless `m_followingGameWindows == true` and window is visible.
  - Removed `SWP_SHOWWINDOW` from `SetWindowPos`.
- Rebuilt `RebornLauncher` target to verify compile success.

# 3) Changes (paths + brief)
- `RebornLauncher/LauncherSplashRenderer.cpp`
  - `UpdateDockAnimation(HWND)` now bails out when not following game windows or when hidden.
  - Removed `SWP_SHOWWINDOW` flag from window position update.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts/top 15 ordering.

# 4) Rationale
- Tray hide failed because periodic animation logic immediately re-showed the hidden launcher window.
- Drag failed because periodic dock positioning continuously moved the window after user drag.
- Menu being covered came from forced topmost behavior plus continuous repaint repositioning.

# 5) Risks + rollback
- Risk: if future logic expects auto-docking while not following game windows, this behavior is now disabled.
- Rollback:
  1. Revert `UpdateDockAnimation` guard condition.
  2. Restore `SWP_SHOWWINDOW` if that forced-show behavior is actually required (not recommended).

# 6) Follow-ups / TODO
- If needed later, add an explicit user setting for “always dock to corner/topmost” instead of implicit always-on behavior.
