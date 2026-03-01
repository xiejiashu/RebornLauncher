# AI Summary - 2026-02-28 20:49 (Asia/Tokyo)

## 1) User request (quote)
> "你看下这日志有啥问题吗？跟DLL加载的顺序有啥关系吗？现在是报错是 -2147023504 (文件或目录损坏且无法读取。)。但是我用没加载DLL的不会报这个错。现在不管在哪里运行都报这错了。"

## 2) What was done
- Analyzed logs: hook attach + mapping refresh succeeded; process exits soon after first `.img` path check.
- Implemented a targeted hook correctness fix: preserve and restore `GetLastError` around `HookedGetFileAttributesA`.
- Added early-call diagnostics to log original `GetFileAttributesA` return value and original `GLE` for first few calls.
- Rebuilt CryptoLoader x86 Release.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - In `HookedGetFileAttributesA`:
    - capture `originalCallLastError` right after calling original API
    - restore with `SetLastError(originalCallLastError)` on all normal/early returns
    - for sync recheck branch, return refreshed attributes and refreshed `GLE` coherently
  - Added first-5-call logging:
    - `hook_call original_result attrs=... gle=...`
- `docs/agent-meta/hot-files-index.md`
  - Updated `CryptoLoader/dllmain.cpp` access count.
- `docs/ai-summaries/2026-02-28-2049-cryptoloader-preserve-last-error.md`
  - Added this summary.

## 4) Rationale
- The observed HRESULT `-2147023504` maps to `0x80070570` (`ERROR_FILE_CORRUPT`), and hook internals call APIs that can overwrite thread-local last error.
- If game logic reads `GetLastError` after `GetFileAttributesA`, leaked internal errors can create false fatal paths.
- Preserving original API `GLE` is a required hook hygiene fix and aligns behavior with no-hook baseline.

## 5) Risks + rollback
- Risk: if game was implicitly relying on leaked nonstandard `GLE` side effects (unlikely), behavior may change.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp`.
  - Rebuild CryptoLoader and redeploy previous DLL.

## 6) Follow-ups/TODO (optional)
- Re-test with updated DLL and compare `hook_call original_result` logs against the reported HRESULT.
