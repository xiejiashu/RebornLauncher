# AI Summary - 2026-02-28 21:37 (Asia/Tokyo)

## 1) User request (quote)
> "服了，你在 HookedGetFileAttributesA 加这么多调用不短路才怪。你应该尽量减少干扰，只在isImgFile加功能。不是ImgFile又不需要处理不用管它的。"

## 2) What was done
- Reduced `HookedGetFileAttributesA` to a minimal-interference implementation.
- Removed non-essential hook-path diagnostics and counters from the hot path.
- Enforced strict passthrough behavior for non-`.img` files.
- Kept custom behavior only for `.img` files.
- Rebuilt CryptoLoader x86 Release successfully.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Removed hook-call telemetry globals used only by prior diagnostic logging.
  - Rewrote `HookedGetFileAttributesA`:
    - call original API
    - preserve original `GetLastError`
    - early passthrough for logger reentry / nested hook depth / disabled / passive mode
    - run extension check
    - if not `.img`, immediate passthrough return
    - only for `.img`, call `HandleHookedFileCheck`
    - preserve `GetLastError` semantics on all exits
  - Removed now-unused `ToHex` helper.
- `docs/agent-meta/hot-files-index.md`
  - Updated `CryptoLoader/dllmain.cpp` access count.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for over-instrumenting hook hot path.
- `docs/ai-summaries/2026-02-28-2137-cryptoloader-minimize-hook-interference.md`
  - Added this summary.

## 4) Rationale
- The hook target is a high-frequency API; non-target paths must be near-zero overhead.
- `.img` is the only required functional branch, so all extra behavior was confined to that branch.
- This minimizes side effects and reduces risk of startup regressions.

## 5) Risks + rollback
- Risk: reduced hook diagnostics may make future triage slower.
- Rollback:
  - revert `CryptoLoader/dllmain.cpp`
  - rebuild and redeploy previous DLL.

## 6) Follow-ups/TODO (optional)
- Re-test startup with this DLL and verify whether early exits disappear while `.img` update behavior remains intact.
