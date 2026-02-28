# AI Summary - g_mapFiles Miss Pass-Through

## 1) User request (quote)
> "还有，如果 g_mapFiles 里面不存在的文件，直接放过去，不要请求下载。"

## 2) What was done
- Updated `CryptoLoader` hook decision path so download dispatch is only evaluated when the file exists in `g_mapFiles` (has expected MD5 entry).
- If `g_mapFiles` has no entry for the file, the hook now directly passes through with no sync/async download request.
- Built `CryptoLoader` Release target to verify no compile regression.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Wrapped download/verification decision block with `if (!expectedMd5.empty())`.
  - Result: unknown files (not in `g_mapFiles`) are ignored by updater request logic.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 counts and ordering.

## 4) Rationale
- Request requires strict manifest ownership: only files explicitly tracked in shared mapping should trigger patch/update actions.
- This prevents unintended download requests for untracked local files.

## 5) Risks + rollback
- Risk: if mapping publication is delayed/incomplete, tracked files absent from `g_mapFiles` will be skipped until mapping is refreshed.
- Rollback:
  - Revert `if (!expectedMd5.empty())` guard and restore previous fallback behavior.

## 6) Follow-ups/TODO (optional)
- Add optional debug log/counter when an `.img` is skipped due to missing `g_mapFiles` entry to help diagnose mapping coverage.
