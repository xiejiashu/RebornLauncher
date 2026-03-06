# AI Task Summary - Single Instance HTTP Recovery

## 1) User request (quote)
> 添加一个功能。就是如果玩家双击图标启动时。这时候已经有一个运行的实例了。那会通知目标去启动一个游戏进程。但是如果目标没应答，证明HTTP坏掉了。如果HTTP服务坏掉了。看看能不能直接叉掉目标。。。如果玩家是双击的猪，而不是图标或exe来启动额外的戏进程时。如果HTTP服务检测到坏掉了无法再处理 \RunClient 那偿试恢复HTTP服务。。。

## 2) What was done
- Added resilient `/RunClient` request logic with retries and returned HTTP status/error code.
- Added stale-instance recovery path for external second-launch (icon/exe): terminate stale launcher process and retry acquiring single-instance mutex.
- Added in-process recovery path for internal new-game trigger (window double-click/menu): request web service recovery and retry `/RunClient`.
- Kept previous web-service recovery support active in coordinator/web thread and wired it into launcher entry points.
- Built and verified `RebornLauncher` in both Debug and Release.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added `DebugLogFmt(...)` (fmt-style wide logging helper).
  - Enhanced `RequestRunningLauncherRunClient(...)` with retry/timeout and status+error outputs.
  - Added `TerminateOtherLauncherProcesses()` and `AcquireSingleInstanceMutexWithRetry(...)`.
  - Updated `RequestNewGameWithError(...)` to recover HTTP service and retry instead of killing processes.
  - Updated `wWinMain` single-instance branch to recover from stale/broken instance by kill + mutex takeover.
- `RebornLauncher/LauncherUpdateCoordinator.h`
  - Added web-service recovery state fields and `RequestWebServiceRecovery()` declaration.
- `RebornLauncher/LauncherUpdateCoordinator.cpp`
  - Implemented `RequestWebServiceRecovery()` to stop active HTTP listener and trigger restart.
- `RebornLauncher/WebService.cpp`
  - Tracked active HTTP server instance.
  - Supported forced listener restart when recovery is requested.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts and order.

## 4) Rationale
- Existing single-instance forwarding depended on `/RunClient`. If local HTTP service is stuck, forwarding fails and user cannot start new client.
- External second-launch should prefer takeover when target instance is stale; internal trigger should prefer service recovery without destructive process kill.
- Retry + timeout + structured status/error improves diagnosability and operational behavior.

## 5) Risks + rollback
- Risk: false-positive process termination if another same-path launcher process should not be killed (mitigated by same exe name + path match).
- Risk: mutex takeover timing race if target exits slowly; mitigated with retry loop.
- Rollback:
  - Revert `RebornLauncher/RebornLauncher.cpp` to previous single-instance and request flow.
  - Revert recovery-tracking changes in `LauncherUpdateCoordinator.*` and `WebService.cpp`.

## 6) Follow-ups / TODO (optional)
- Add integration test for:
  - external second-launch with healthy HTTP (`/RunClient` success),
  - external second-launch with broken HTTP (kill + takeover),
  - internal new-game trigger with broken HTTP (recovery + retry).
- Consider surfacing recovery state in UI status text for user-visible diagnostics.
