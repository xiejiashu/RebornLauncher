## 1) User request (quote)
> “现在这个CryptoLoader中的img下载是不是同步的。一定要同步。否则没下载完函数往下执行。游戏里就会判断没有这文件而退出。”

## 2) What was done
- Verified current behavior in `CryptoLoader/dllmain.cpp`: `.img` download requests were dispatched via `RequestDownloadFromLauncherAsync`, which used `_beginthreadex` and returned immediately.
- Confirmed launcher `/download` handler is blocking and only returns HTTP 200 after `DownloadWithResume(...)` completes.
- Switched CryptoLoader hook path to synchronous call so file-check flow waits for launcher response.
- Removed now-unused async thread helper functions and one unused header include.
- Built x86 CryptoLoader to verify compile success.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Deleted `DownloadRequestThreadProc` and `RequestDownloadFromLauncherAsync`.
  - Changed `.img` missing/mismatch branch to call `RequestDownloadFromLauncher(page)` directly (synchronous).
  - Added a short comment documenting required blocking behavior.
  - Removed unused `<memory>` include.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for async-request race in hook path.
- `docs/agent-meta/hot-files-index.md`
  - Incremented access counts for touched/read hot files.

## 4) Rationale
- Hook path must not continue before requested `.img` download is finished (or definitively failed), otherwise game-side file existence checks can race and force exit.
- Since launcher `/download` is already implemented as a blocking download endpoint, synchronous client call enforces the required ordering.

## 5) Risks + rollback
- Risk: synchronous network call can increase blocking time in file-hook path when launcher/network is slow.
- Mitigation: existing `ShouldRequestDownloadNow` cooldown still limits repeated requests.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp` to restore async dispatch.
  - Revert `docs/agent-meta/ai-mistake-log.md`.
  - Revert `docs/agent-meta/hot-files-index.md`.

## 6) Follow-ups/TODO (optional)
- Consider adding explicit timeout and retry policy in `RequestDownloadFromLauncher` so hook blocking duration is bounded and predictable.
