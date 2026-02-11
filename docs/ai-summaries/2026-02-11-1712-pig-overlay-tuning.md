# AI Summary

## 1) User request (quote)
> 微调一下。你不需要在猪底下画一条跑道的，直接让猪在窗口上面跑，并且把猪缩小一点不要那么大。并且猪的线条为啥是紫色的。不要额外给猪加轮廓线条。猪如果没有下载状态的时候。让它在最左边。并让它保持与游戏窗口是同一层级的。游戏窗口被遮挡它也会被遮挡。游戏窗口激活它也会被调上来（但是不能遮挡游戏窗口)

## 2) What was done
- Removed the runway rendering under pigs.
- Reduced pig render size for overlay mode.
- Changed idle behavior: pig now stays at the left-most position when not downloading.
- Removed extra visual line elements around pig display (no runway/outline drawing).
- Tuned layered transparency key/background to avoid purple fringe appearance.
- Changed overlay z-order strategy:
  - removed permanent topmost style
  - during follow mode, overlay is inserted relative to tracked game window z-order each refresh
  - keeps pig non-blocking on gameplay area while still following game activation order behavior.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Transparency key changed from magenta to dark key color.
  - Added `FindTopTrackedGameWindow(...)` helper for z-order anchoring.
  - Updated `RefreshPigOverlayState(...)`:
    - tighter overlay bounds above game windows
    - no fixed topmost usage
    - set z-order relative to tracked game windows.
  - Updated `DrawSplashScene(...)`:
    - removed runway pen and line drawing
    - pig size reduced
    - idle position set to left
    - file/progress label shown only while downloading
    - interpolation mode tuned.
  - Removed `WS_EX_TOPMOST` from launcher overlay window creation.
- `docs/agent-meta/hot-files-index.md`
  - Updated hot-file access counts.

## 4) Rationale
- Runway/outline removal and smaller sprite directly match requested visual style.
- Left-anchored idle state makes “not downloading” state deterministic and clear.
- Per-refresh z-order anchoring to game windows avoids global always-on-top behavior and better matches “same layer dynamics” with game windows.

## 5) Risks + rollback
- Single-overlay design still aggregates all pigs into one transparent window; z-order is now game-relative, but strict per-client independent z-order is limited by the single-window architecture.
- If further strictness is needed, migrate to per-client overlay windows.
- Rollback by reverting `RebornLauncher/RebornLauncher.cpp` to previous commit.

## 6) Follow-ups / TODO
- If you still see any edge fringe on specific DPI/scaling setups, next step is upgrading overlay to per-pixel alpha (`UpdateLayeredWindow`) for fully clean edges.
