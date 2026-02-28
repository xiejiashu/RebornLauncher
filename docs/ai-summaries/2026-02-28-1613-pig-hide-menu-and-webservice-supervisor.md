# AI Summary - Pig Hide Menu And WebService Supervisor

## 1) User request (quote)
> “给RebornLauncher那边加个功能，就是让猪可以藏匿，右键增加藏匿菜单选项,直接会收缩到右下角的小图标去，也可以再次弹出。也可以用左键按住移动。。。。。还有如果RebornLauncher检测到本地Web服务停止。看看可不可以自动重启。。。”

## 2) What was done
- Added a new right-click menu action on the launcher pig window to hide to tray.
- Wired the new menu command to existing tray minimize behavior, so the launcher collapses to the bottom-right tray icon and can be restored from tray click/menu.
- Confirmed existing left-button drag behavior remains active (`WM_LBUTTONDOWN` + `WM_NCLBUTTONDOWN` flow).
- Added a web-service supervisor loop around `WebServiceThread()` startup path: if the web service thread exits unexpectedly while launcher is still running, launcher logs and restarts it automatically.
- Compiled updated sources (`RebornLauncher.cpp`, `WorkThread.cpp`) with `ClCompile` target.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added command id: `ID_HIDE_TO_TRAY = 5007`.
  - Added right-click popup menu item: `Hide To Tray`.
  - Added `WM_COMMAND` handler branch to call `g_trayIconManager.MinimizeToTray(...)`.
- `RebornLauncher/WorkThread.cpp`
  - Replaced one-shot detached web-service launch with supervisor loop:
    - `try/catch` around `WebServiceThread()`.
    - logs `UF-WS-EXCEPTION` on exception.
    - logs `UF-WS-SUPERVISOR` + short delay + restart when service stops unexpectedly.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for a detected edit mistake and corrective pattern.
- `docs/agent-meta/hot-files-index.md`
  - Updated top file access counts/order.

## 4) Rationale
- Hiding to tray matches “shrink to bottom-right icon” behavior without introducing new floating-window complexity.
- Reusing existing tray restore path provides immediate “can pop out again” capability.
- Supervisor restart path is resilient to unexpected web-service thread exit and avoids requiring manual relaunch.

## 5) Risks + rollback
- Risk: if web service repeatedly throws on startup, supervisor will retry in a loop (with 500ms delay), generating repeated logs.
- Risk: menu text is currently English (`Hide To Tray`), may need localization if Chinese-only UX is required.
- Rollback:
  - Remove `ID_HIDE_TO_TRAY` menu/command wiring in `RebornLauncher.cpp`.
  - Revert `WorkThread.cpp` supervisor loop to single detached `WebServiceThread()` call.

## 6) Follow-ups/TODO (optional)
- Optional: change menu text to Chinese (`藏匿到托盘`) and add explicit tray-menu “弹出主界面/藏匿” toggle labels.
- Optional: add restart-attempt throttling/backoff if web service fails continuously.
