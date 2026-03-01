# AI Summary - 2026-02-28 21:30 (Asia/Tokyo)

## 1) User request (quote)
> "是不是API勾子里有短路。有短路就处理下。"

## 2) What was done
- Interpreted latest logs as likely early-exit during initialization stage, before stable hook traffic.
- Refactored initialization to keep `DllMain` minimal and avoid heavy work under loader lock.
- Moved path setup, flag checks, logging, map worker startup, and hook-attach loop into a dedicated init thread.
- Simplified detach path in `DllMain` to avoid logging/waiting under loader lock.
- Rebuilt CryptoLoader x86 Release successfully.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `CryptoLoaderInitThreadProc`.
  - `DLL_PROCESS_ATTACH` now only:
    - `DisableThreadLibraryCalls`
    - `CreateThread(CryptoLoaderInitThreadProc)` and close handle.
  - Moved heavy attach work into init thread:
    - resolve hook target + log
    - read disable/passive flags
    - start version-map worker
    - attach detour with retry loop
  - `DLL_PROCESS_DETACH`:
    - no logger writes
    - no blocking wait in map worker stop
    - best-effort detour detach only.
- `docs/agent-meta/hot-files-index.md`
  - Updated `CryptoLoader/dllmain.cpp` access count.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for heavy DllMain init risk.
- `docs/ai-summaries/2026-02-28-2130-cryptoloader-dllmain-minimal-init-thread.md`
  - Added this summary.

## 4) Rationale
- Loader-lock unsafe work in `DllMain` can cause non-deterministic startup failures and quick exits.
- Minimal `DllMain` plus deferred init thread is the safer pattern for hook DLLs.

## 5) Risks + rollback
- Risk: hook attach now starts slightly later after process attach.
- Rollback:
  - revert `CryptoLoader/dllmain.cpp`
  - rebuild and redeploy prior DLL.

## 6) Follow-ups/TODO (optional)
- Re-test and compare whether logs now continue beyond attach/worker start into hook-call/dispatch traces.
