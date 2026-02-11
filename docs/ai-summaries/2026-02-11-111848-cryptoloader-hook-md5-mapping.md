# AI Summary

## 1) User request (quote)
> "完成实现CryptoLoader/dllmain.cpp下的Hook_sub_5081A080功能。如果lpFileName文件存在当前目录则判断文件MD5，如果不存在则直接发送请求到Launcher那边去下载,下载协议可以参考 Launcher下面WorkThread.cpp文件下的void WorkThread::WebServiceThread() 函数，所以得引入http模块到CryptoLoader中。并且实现一下 Launcher 项目中的 void WorkThread::WriteVersionToMapping(std::string& m_strRemoteVersionJson) 并调用一下，看看有没有错。然后在CryptoLoader中直接把这个内存读出来放在g_mapFiles中，用于通过映射完整的文件名(包括路径名)获取文件的MD5,你也可以把Launcher中拼接好路径的内存写入到共享内存中。也可以在CryptoLoader中拼接看下怎么实现方便，记得代码要规范。"

## 2) What was done
- Implemented `Hook_sub_5081A080` in `CryptoLoader` with:
  - shared-memory manifest map loading into `g_mapFiles`
  - local file existence check
  - local MD5 check against expected MD5 when file exists
  - HTTP request to launcher `/download?page=...` when missing or MD5 mismatch.
- Switched `CryptoLoader` HTTP to native `WinHTTP` (no vcpkg HTTP/JSON dependency), to keep x86 builds working.
- Restored x86/x64 hook signature branching in `CryptoLoader` to keep calling convention/argument compatibility.
- Implemented `WorkThread::WriteVersionToMapping(std::string& m_strRemoteVersionJson)` in `RebornLauncher`:
  - parses manifest JSON
  - builds full-path (absolute) -> MD5 map
  - writes compact text payload (`<full_path>\t<md5>\n`) to named shared memory.
- Called `WriteVersionToMapping(...)` from:
  - local `Version.dat` parse path in `Run()`
  - remote manifest refresh path in `RefreshRemoteVersionManifest()`.
- Updated `WebServiceThread()` `/download` endpoint to actually perform single-file download (and fallback refresh of manifest if key missing), instead of only existence check.
- Built and verified:
  - x64 `Debug` + `Release`: `CryptoLoader`, `RebornLauncher`
  - x86 `Debug` + `Release`: `CryptoLoader` (`_builds_cryptoloader_x86`).

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added hook logic, MD5 utilities, shared memory reader, `g_mapFiles`, launcher HTTP request logic, and x86/x64 signature-compatible hook wrappers.
- `CryptoLoader/CMakeLists.txt`
  - Linked `advapi32` + `winhttp`; updated target Windows version to `0x0A00`.
- `RebornLauncher/WorkThread.h`
  - Updated declaration to `void WriteVersionToMapping(std::string& m_strRemoteVersionJson);`.
- `RebornLauncher/WorkThread.cpp`
  - Added `WriteVersionToMapping` implementation.
  - Added calls to `WriteVersionToMapping` in local/remote manifest handling.
  - Updated `/download` endpoint to download requested file.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts and ordering.

## 4) Rationale
- Shared memory makes launcher-manifest MD5 data available to injected loader without extra network or file parsing overhead in the game process.
- Full-path key mapping in launcher side avoids path ambiguity in hook checks.
- Hook-side behavior follows requested rule: existing file -> verify MD5; missing file -> request download.
- Implementing actual `/download` file fetch ensures HTTP request from hook has real effect.

## 5) Risks + rollback
- Risks:
  - Hook path adds file/MD5 checks and HTTP calls; this may add latency when file APIs are hot.
  - `/download` now performs network I/O; if endpoint is called frequently, launcher workload increases.
  - Path normalization assumptions may differ for edge-case path encodings.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp` and `CryptoLoader/CMakeLists.txt` to remove hook-side verification/download logic.
  - Revert `RebornLauncher/WorkThread.{h,cpp}` changes to remove shared-memory version map and `/download` behavior change.

## 6) Follow-ups/TODO
- Consider adding lightweight in-memory throttle/debounce for repeated `/download` requests on same path within a short interval.
- Consider adding launcher-side mutex around on-demand `/download` if concurrent requests can target same file.
