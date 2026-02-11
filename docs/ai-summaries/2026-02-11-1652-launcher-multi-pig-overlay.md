# AI Summary

## 1) User request (quote)
> 当前这个项目,在流程上跑通了。但是还有几个问题。1、Launcher启动游戏后，可以循环检查一下进程是不是有窗口。有获取到目标进程有创建类名为 MapleStoryClass 那把窗口句柄赋值给 m_gameInfos 里面的结构成员。把Launcher的窗口(当前是只猪)，把猪吸附到目标进程的游戏窗口顶上播放动画，就不要再收缩起来了。。。如果玩家启动了多个游戏客户端，那就创建多只猪在相应的窗口上播放动画。。。。如果有客户端来请求下载资源。那让这只猪移动起来，从左到右。猪头顶显示资源文件名，并显示下载进度。跑道长度按游戏客户端窗口长度来计算进度。。

## 2) What was done
- Added process-window polling in `WorkThread` to continuously bind `MapleStoryClass` window handles into `m_gameInfos`.
- Added per-client download runtime state (file name, bytes, progress) in `m_gameInfos`.
- Changed launcher overlay behavior to follow game windows (no auto-minimize after launching client).
- Implemented multi-pig rendering: one pig per running game window, attached above each window.
- Implemented per-pig moving animation while downloading (left-to-right by percent on per-window runway).
- Displayed file name + percent above each moving pig.
- Added `pid` query parameter in `CryptoLoader` `/download` requests so launcher can map download activity to the correct client.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.h`
  - Extended `tagGameInfo` with per-client download state.
  - Added snapshot/window-refresh/download-marking methods.
  - Extended `DownloadWithResume` signature to accept optional owner process id.
- `RebornLauncher/WorkThread.cpp`
  - Added `EnumWindows` search for `MapleStoryClass` by process id.
  - Added `UpdateGameMainWindows()` polling and called it in worker loop.
  - Removed auto-minimize behavior after game launch and `/RunClient`.
  - Added pid-aware download state updates for `/download`.
  - Added per-client progress updates during HTTP/P2P download callbacks.
- `RebornLauncher/RebornLauncher.cpp`
  - Added pig overlay state and window-follow logic for multiple game windows.
  - Added per-window pig drawing + runway + label/progress rendering.
  - Added per-frame sync from `WorkThread::GetGameInfosSnapshot()`.
  - Preserved manual position only when not in follow mode.
  - Set launcher overlay window as topmost popup.
- `CryptoLoader/dllmain.cpp`
  - Appended `&pid=<GetCurrentProcessId()>` to `/download` request path.
- `docs/agent-meta/hot-files-index.md`
  - Updated hot file access counts/order.

## 4) Rationale
- Mapping downloads to a specific client requires a stable identity; using `pid` from `CryptoLoader` provides deterministic binding.
- Keeping per-client state in `m_gameInfos` keeps process/window/download data unified and easy to render.
- Following actual game window rectangles ensures pigs stay visually attached and supports multi-client naturally.

## 5) Risks + rollback
- Risk: overlay bounds/position updates are frequent; may cause visible jitter on some systems.
- Risk: if client side does not carry pid (older `CryptoLoader`), per-client download animation falls back to no specific pig mapping.
- Rollback:
  - Revert these files: `RebornLauncher/WorkThread.h`, `RebornLauncher/WorkThread.cpp`, `RebornLauncher/RebornLauncher.cpp`, `CryptoLoader/dllmain.cpp`.
  - Restore previous single-window/single-pig and global progress behavior.

## 6) Follow-ups / TODO
- Optionally make overlay click-through (`WS_EX_TRANSPARENT`) if top-edge mouse interaction conflicts with game window dragging.
- Optionally add stale-download timeout so pig auto-returns to idle if a request aborts unexpectedly.
