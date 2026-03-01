# AI Summary

## 1) User request (quote)
> “你直接改CryptoLoader的刷新速度不行的。CryptoLoader 的流程很敏感的。。。除非这个刷新是异步的。不然很容易卡住主线程。因为主线程加载资源时是动态加载的。如果卡住了游戏就不流畅。。。”

## 2) What was done
- Converted CryptoLoader version-map refresh from hook-thread synchronous refresh to a background asynchronous worker.
- Kept hook path behavior: it still checks cache each call, but now it only schedules refresh and never performs shared-memory mapping I/O inline.
- Added worker lifecycle start/stop in `DllMain` process attach/detach.
- Built CryptoLoader x86 Debug and Release to verify.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added atomic state for async refresh control (`g_mapRefreshStopRequested`, `g_mapRefreshScheduled`, `g_lastMapRefreshTick`).
  - Added background refresh thread (`VersionMapRefreshThreadProc`).
  - Added worker control helpers (`StartVersionMapRefreshWorker`, `StopVersionMapRefreshWorker`).
  - Updated `RefreshVersionMapCacheIfNeeded()` to schedule refresh only (non-blocking hot path).
  - Started worker on `DLL_PROCESS_ATTACH`; stopped worker on `DLL_PROCESS_DETACH`.
  - Set background refresh cadence to 10 seconds, with 200ms worker tick.
- `docs/agent-meta/hot-files-index.md`
  - Updated access count/order for `CryptoLoader/dllmain.cpp`.

## 4) Rationale
- The previous synchronous `RefreshVersionMapFromSharedMemory()` on hook path can stall dynamic file-load hot loops.
- Moving mapping read/parse into a worker isolates mapping I/O and parsing from game main thread timing.
- Hook path now only reads already-cached map and posts refresh intent, minimizing per-call overhead.

## 5) Risks + rollback
- Risk: minor lock contention remains when worker swaps map while hook reads it (very short critical section).
- Risk: if async worker fails repeatedly (mapping unavailable), cache may remain stale until mapping appears.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp` to synchronous refresh behavior.
  - Rebuild `CryptoLoader` x86 Debug/Release.

## 6) Follow-ups/TODO (optional)
- If needed, add a lightweight generation/version value in mapping payload to skip map swap when content unchanged.
