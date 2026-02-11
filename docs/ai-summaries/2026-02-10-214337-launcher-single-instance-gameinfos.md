# AI Summary

## 1) User request (quote)
> "Keep Launcher single-instance. If user runs Launcher again, do not open another Launcher; instead ask the running Launcher to create a new game process, and replace old single game info handling with list-style `m_gameInfos` storage/operations."

## 2) What was done
- Implemented true single-instance behavior for `RebornLauncher` with a named mutex.
- On second Launcher start, the new process now exits quickly after requesting the running Launcher to execute `/RunClient`.
- Reworked game process tracking from fixed 2-slot arrays to vector-based list management via `m_gameInfos`.
- Unified launch path with reusable helpers (`LaunchGameClient`, cleanup/terminate helpers).
- Updated `/RunClient` to append a new game process entry each time instead of reusing fixed `Client1/Client2` slots.
- Built `RebornLauncher` (Release) to validate successful compilation.

## 3) Changes (paths + brief)
- `RebornLauncher/RebornLauncher.cpp`
  - Added named mutex single-instance gate (`Local\\MapleFireReborn.RebornLauncher.SingleInstance`).
  - Added retry-based `RequestRunningLauncherRunClient()` handoff on second launch.
  - Removed old INI PID-based single-instance and forced client-kill logic.
  - Added mutex cleanup on all exit paths.
- `RebornLauncher/WorkThread.h`
  - Added list-based process helper declarations:
    - `LaunchGameClient`
    - `CleanupExitedGameInfos`
    - `HasRunningGameProcess`
    - `TerminateAllGameProcesses`
    - `UpdateLegacyClientPidKeysLocked`
  - Added `m_gameInfos` mutex and inlined `tagGameInfo` struct to avoid external header dependency.
- `RebornLauncher/WorkThread.cpp`
  - Removed fixed array constructor initialization usage.
  - `Run()` now launches first client with `LaunchGameClient()` and monitors via `m_gameInfos`.
  - Added implementations for launch/cleanup/terminate list helpers.
  - `WebServiceThread()` `/RunClient` now: refresh manifest -> download runtime -> cleanup exited -> launch new client (append into list).
  - `/Stop` now terminates all tracked game processes through list helper.
  - Legacy compatibility keys `Client1PID`/`Client2PID` are now projected from first two list entries.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-file access counts.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for this task's scripting mistake and fix.

## 4) Rationale
- Mutex-based single-instance control is more reliable than INI PID checks.
- Re-launch handoff through local HTTP preserves existing architecture while preventing duplicate Launcher processes.
- `m_gameInfos` list model matches the request and removes fixed-slot constraints.

## 5) Risks + rollback
- Risk: Legacy INI compatibility projection still exports only first two PIDs (`Client1PID`/`Client2PID`) while internal tracking supports more.
- Rollback:
  - Revert these files:
    - `RebornLauncher/RebornLauncher.cpp`
    - `RebornLauncher/WorkThread.h`
    - `RebornLauncher/WorkThread.cpp`
    - `docs/agent-meta/hot-files-index.md`
    - `docs/agent-meta/ai-mistake-log.md`

## 6) Follow-ups / TODO (optional)
- If external consumers need full multi-client visibility, expose full PID list via a dedicated key or endpoint.
