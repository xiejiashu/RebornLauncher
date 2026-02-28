# 1) User request (quote)
> User reported they could not drag the pig window, and asked to persist drag position so next launch moves from center to that saved position.

# 2) What was done
- Reworked left-button drag to use explicit mouse-capture drag flow in `WndProc`:
  - start drag on `WM_LBUTTONDOWN`
  - move on `WM_MOUSEMOVE`
  - finish/save on `WM_LBUTTONUP`
- Added launcher window position persistence (`WindowX`/`WindowY`) in `launcher.ini`.
- Added load + clamp logic for saved window position at startup.
- Changed startup animation behavior:
  - window still starts centered
  - if saved position exists, pig animates from center to saved position
  - otherwise keeps existing corner-dock startup behavior
- Kept tray hide/show behavior and removed forced restore reposition side effects from prior path.
- Built `RebornLauncher` (`Release`, `ClCompile`) successfully.

# 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added globals: saved-position flags and config keys (`WindowX`, `WindowY`).
  - Added helper functions:
    - parse int
    - clamp window position to work area
    - load/save launcher window position
  - `wWinMain`: load saved window position before window init.
  - `WM_CREATE`: if saved position exists, start dock animation to saved target.
  - `WM_LBUTTONDOWN/WM_MOUSEMOVE/WM_LBUTTONUP/WM_CAPTURECHANGED`: implemented stable drag flow.
  - `WM_DESTROY`: persist final window position.
- `RebornLauncher/LauncherSplashRenderer.h`
  - Added `RestartDockToPosition(const POINT&)`.
  - Added members for target override control.
- `RebornLauncher/LauncherSplashRenderer.cpp`
  - Implemented target-override dock animation (`center -> saved position`).
  - Kept one-shot dock animation and auto-disable logic.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 access counts.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for this task's compile-time scope/type mistake and fix.

# 4) Rationale
- Native `WM_NCLBUTTONDOWN` move path was unreliable in this launcher context; explicit drag handling is deterministic.
- Persisting `WindowX/WindowY` matches the user requirement for remembered position.
- Startup target override preserves visual behavior ("from center to target") while supporting custom user position.
- Work-area clamping prevents restoring to off-screen coordinates.

# 5) Risks + rollback
- Risk: high-frequency `WM_MOUSEMOVE` updates may feel sensitive on very slow machines.
  - Mitigation: uses only `SetWindowPos` with `SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER`.
- Risk: existing old/invalid INI values.
  - Mitigation: parse+clamp, fallback to center/corner startup path.
- Rollback:
  1. Revert `WM_LBUTTONDOWN/WM_MOUSEMOVE/WM_LBUTTONUP` custom drag block.
  2. Revert `WindowX/WindowY` load/save helpers and startup override call.
  3. Revert `RestartDockToPosition` additions in splash renderer.

# 6) Follow-ups / TODO
- Optional: add a config toggle for startup mode (`center->saved`, `center->corner`, or `no animation`).
