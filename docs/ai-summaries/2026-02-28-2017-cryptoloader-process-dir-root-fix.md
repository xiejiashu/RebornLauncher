# AI Summary - 2026-02-28 20:17 (Asia/Tokyo)

## 1) User request (quote)
> "找到问题了，主要是CryptoLoader改出问题了，离开G:\\MapleStoryStudio\\Fire\\UpdateForge\\_builds\\RebornLauncher 目录运行就起不了。在当前目录可以起来。不知道啥原理。我更新到 D:\\MapleFireReborn 后死活起不来。但是我拿了一个没导入这个DLL的就能起来。"

## 2) What was done
- Investigated `CryptoLoader` path handling and found multiple `current_path()`-based decisions in the hook path.
- Changed hook path base-directory logic to use the process EXE directory (game install dir), not mutable current working directory.
- Kept `current_path()` only as a fallback when EXE path resolution fails.
- Built `CryptoLoader` x86 Release successfully.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added process-directory cache helpers:
    - `ResolveProcessDirectoryPath()`
    - `EnsureProcessDirectoryCache()`
    - `GetProcessDirectoryPath()`
  - `BuildDownloadPage(...)` now computes relative page path against process directory.
  - Relative file resolution in hook path now uses process directory.
  - Current-directory key cache logic replaced with process-directory cache logic.
  - Initialize process-directory cache during `DLL_PROCESS_ATTACH`.
- `docs/agent-meta/hot-files-index.md`
  - Updated tracked access counts.
- `docs/ai-summaries/2026-02-28-2017-cryptoloader-process-dir-root-fix.md`
  - Added this summary.

## 4) Rationale
- Startup behavior was implicitly coupled to launch working directory via `std::filesystem::current_path()`.
- Installing/running from `D:\\MapleFireReborn` can produce different runtime CWD behavior from local build folders.
- Using the process EXE directory as a stable root removes that environment dependency and keeps path matching/download dispatch deterministic across locations.

## 5) Risks + rollback
- Risk: if game intentionally expects non-EXE-root relative lookup in a rare edge case, hook dispatch scope may narrow or broaden differently.
- Fallback remains in place (`current_path`) to reduce total failure risk when EXE directory cannot be resolved.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp`.
  - Rebuild `CryptoLoader` and redeploy old DLL.

## 6) Follow-ups/TODO (optional)
- Add optional debug logging toggle in `CryptoLoader` to record resolved process dir, requested page, and download dispatch result for first N calls.
- Add a release packaging check to verify deployed `CryptoLoader.dll` timestamp/hash matches built artifact.
