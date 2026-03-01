# AI Summary - CryptoLoader Version Map Worker + Async MD5 Dispatch

## 1) User request (quote)
> “先还源到仓库。HookedGetFileAttributesA 以下的代码保持不变。增加 StartVersionMapRefreshWorker 定时更新 kVersionMapMappingName 映射。已存在但 MD5 不同走异步下载投递，不等待；本地不存在必须同步下载。HandleHookedFileCheck 先判断映射里有没有文件。日志函数保留。”

## 2) What was done
- Restored `CryptoLoader/dllmain.cpp` to repository `HEAD` first.
- Refactored logic only above `HookedGetFileAttributesA` while keeping `HookedGetFileAttributesA` and below unchanged.
- Added a background worker entry `StartVersionMapRefreshWorker()` that periodically refreshes MD5 map data from shared memory key `MapleFireReborn.VersionFileMd5Map`.
- Reworked `HandleHookedFileCheck` flow to check map membership first and skip processing when entry is missing.
- Enforced dispatch split:
- Local file missing -> synchronous launcher request (`mode` omitted), wait path retained.
- Local file exists but MD5 mismatch -> asynchronous launcher queue request (`mode=async`), no blocking wait.
- Kept a logging helper function for future diagnostics and used it for worker startup failures.
- Built `CryptoLoader` in both Release and Debug successfully.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`: added `StartVersionMapRefreshWorker`, periodic mapping refresh worker proc, map lookup helper returning hit/miss, cleaned file-check dispatch logic, removed large-file MD5 skip branch, fixed URL encoding to strict `%HH`.
- `docs/agent-meta/hot-files-index.md`: incremented access counts for touched high-frequency files.

## 4) Rationale
- Periodic worker decouples map refresh from hot hook path and aligns with launcher-shared mapping design.
- “Map hit first” prevents unnecessary MD5/hash/download work for files not managed by manifest mapping.
- Missing-file sync is required to prevent runtime resource crashes in embedded game process.
- Existing-file mismatch async queue avoids blocking `GetFileAttributesA` call path.

## 5) Risks + rollback
- Risk: if launcher mapping is temporarily unavailable, async mismatch handling can be delayed until next refresh retry.
- Risk: asynchronous dispatch still depends on launcher local web service availability.
- Rollback:
- `git restore --source=HEAD -- CryptoLoader/dllmain.cpp docs/agent-meta/hot-files-index.md`
- Delete this summary file if needed.

## 6) Follow-ups/TODO (optional)
- Optionally add explicit log lines for sync/async request outcomes if you want easier现场排障.
- Optionally add a tiny integration check that validates map-hit/missing-file/mismatch-file branches against a mocked local launcher endpoint.
