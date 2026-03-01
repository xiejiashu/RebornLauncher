# AI Summary - 2026-02-28 21:16 (Asia/Tokyo)

## 1) User request (quote)
> "看下，这些都怎么回事呢。"

## 2) What was done
- Interpreted logs: hook target resolution/attach are successful and first `.img` check is reached before process exit.
- Added a passive hook mode for fast A/B isolation:
  - hook is installed
  - hook logic is skipped (returns original result directly)
- Added dispatch elapsed-time logging for `.img` handling.
- Rebuilt CryptoLoader x86 Release.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `g_passiveHookMode` and flag reader:
    - `CryptoLoader.passive_hook` in process directory
  - `dll_attach` log now includes `passive_hook=0/1`.
  - `HookedGetFileAttributesA` now early-returns original behavior when passive mode is enabled.
  - Added `.img` dispatch timing log: `img_check dispatch_result=... elapsed_ms=...`.
- `docs/agent-meta/hot-files-index.md`
  - Updated `CryptoLoader/dllmain.cpp` access count.
- `docs/ai-summaries/2026-02-28-2116-cryptoloader-passive-hook-ab-test.md`
  - Added this summary.

## 4) Rationale
- Current evidence suggests process exits after entering hook path but before useful update dispatch traces.
- Passive mode keeps detour mechanics but removes heavy hook logic, isolating whether failure is from:
  - detour presence itself, or
  - hook body logic.

## 5) Risks + rollback
- Passive mode disables update interception behavior while enabled (diagnostic use only).
- Rollback:
  - remove passive flag file; normal behavior resumes.
  - or revert code if needed.

## 6) Follow-ups/TODO (optional)
- Run one test with passive mode OFF and one with passive mode ON, then compare startup outcome and log tail.
