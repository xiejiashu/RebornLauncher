# 1) User request (quote)
> "RebornLauncher has an incoherent flow: sometimes it launches the game without updating, repeated launches should still update, and if the HTTP service stops it should restart."

# 2) What was done
- Hardened startup update gate: launcher now requires manifest refresh success before initial client launch.
- Hardened `/RunClient` flow: now checks manifest refresh result and blocks launch on refresh/download failure.
- Serialized update+launch flow using a shared mutex to avoid overlapping update/launch operations.
- Added HTTP service auto-restart loop: web service now relistens if it stops unexpectedly while launcher is still running.
- Built `RebornLauncher` Release target to validate compilation after changes.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.h`: added `m_launchFlowMutex` for launch/update flow serialization.
- `RebornLauncher/WorkThreadRunCoordinator.cpp`: replaced soft manifest check with strict `RefreshRemoteVersionManifest()` check, wrapped update+launch steps with flow mutex.
- `RebornLauncher/WorkThreadWebService.cpp`: `/RunClient` now validates manifest refresh success, uses flow mutex, and web service now auto-restarts when listener exits unexpectedly.
- `docs/agent-meta/hot-files-index.md`: incremented access counts and kept top-15 ordering.

# 4) Rationale
- The previous `/RunClient` path called manifest refresh but ignored its return value, allowing launch to proceed on stale/empty update state.
- A strict update gate prevents "skip update then launch" behavior.
- Serializing launch/update operations reduces race conditions between initial launch flow and later `/RunClient` requests.
- Auto-restart in web service thread addresses "HTTP service stopped" cases without requiring manual launcher restart.

# 5) Risks + rollback
- Risk: if remote manifest is temporarily unavailable, client launch is now blocked (intentional for update-first policy).
- Risk: stricter gating may surface more 502 responses instead of launching with stale state.
- Rollback: revert these files to previous behavior:
  - `RebornLauncher/WorkThread.h`
  - `RebornLauncher/WorkThreadRunCoordinator.cpp`
  - `RebornLauncher/WorkThreadWebService.cpp`

# 6) Follow-ups/TODO (optional)
- Consider adding bounded retry/backoff for manifest refresh in `/RunClient` response path with clearer UI status.
- Consider adding a lightweight `/health` endpoint and startup wait in `RequestRunningLauncherRunClient()` for smoother handoff.