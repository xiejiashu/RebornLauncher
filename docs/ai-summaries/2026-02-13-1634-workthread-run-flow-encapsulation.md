# 1) User request (quote)
> “嗯，继续，在继续之前先提交一下。”

# 2) What was done
- Committed the current refactor baseline first as requested.
- Continued optimization by refactoring `WorkThread::Run()` into smaller private phase methods while preserving logic order and error handling.
- Kept behavior unchanged: same message boxes, same `Stop()` paths, same self-update exit flow, same client launch/monitor lifecycle.
- Rebuilt `RebornLauncher` in both Debug and Release after refactor.

# 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.h`
  - Added private phase-method declarations:
    - `InitializeDownloadEnvironment`
    - `EnsureBasePackageReady`
    - `LoadLocalVersionState`
    - `RefreshRemoteManifestIfChanged`
    - `HandleSelfUpdateAndExit`
    - `PublishMappingsAndLaunchInitialClient`
    - `MonitorClientsUntilShutdown`
- `RebornLauncher/WorkThread.cpp`
  - Added implementations for the above phase methods.
  - Simplified `Run()` to orchestrate these methods in the original sequence.
  - Left `DownloadRunTimeFile` and `DownloadBasePackage` logic intact.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-file access counts.

# 4) Rationale
- `Run()` had multiple responsibilities mixed together (bootstrap, manifest sync, update execution, self-update, client lifecycle).
- Splitting into focused internal methods improves maintainability and reviewability without changing runtime behavior.
- This prepares the next pass (moving additional pieces into dedicated units) with lower regression risk.

# 5) Risks + rollback
- Risk: small helper extraction mistakes can alter ordering if not validated.
- Mitigation: retained original method call order and performed full Debug/Release build verification.
- Rollback:
  1. Revert `RebornLauncher/WorkThread.cpp` and `RebornLauncher/WorkThread.h` to previous commit state.
  2. Rebuild with:
     - `cmake --build _builds --config Debug --target RebornLauncher`
     - `cmake --build _builds --config Release --target RebornLauncher`

# 6) Follow-ups/TODO (opt)
- Continue by moving `Run` phase methods into a dedicated `WorkThreadRunFlow.cpp` so `WorkThread.cpp` focuses on construction and download entrypoints only.
