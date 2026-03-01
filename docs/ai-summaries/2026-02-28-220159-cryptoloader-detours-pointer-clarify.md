# AI Summary - 2026-02-28 22:01:59 (Asia/Tokyo)

## 1) User request (quote)
> "you hooked yourself ... `g_originalGetFileAttributesA` and `GetFileAttributesA` are the same address."

## 2) What was done
- Reworked hook state to separate `target` and `original(trampoline)` pointers clearly.
- Updated Detours attach/detach to canonical form and added explicit self-reference guard.
- Added hook-state logs so target/original/detour addresses are visible in startup logs.
- Rebuilt `CryptoLoader` x86 Release successfully.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `g_targetGetFileAttributesA`.
  - `SetFileAttributesHook(true)` now resets `g_originalGetFileAttributesA = g_targetGetFileAttributesA` before attach.
  - Switched to canonical Detours calls:
    - `DetourAttach(&(PVOID&)g_originalGetFileAttributesA, (PVOID)HookedGetFileAttributesA)`
    - `DetourDetach(&(PVOID&)g_originalGetFileAttributesA, (PVOID)HookedGetFileAttributesA)`
  - Added guard to reject self-reference (`target/original == detour`) and log failure.
  - Added runtime guard inside hook to block accidental self-recursion.
  - Init thread now logs both `target` and `detour` addresses.
- `docs/agent-meta/hot-files-index.md`
  - Incremented `CryptoLoader/dllmain.cpp` access count.
- `docs/agent-meta/ai-mistake-log.md`
  - Added one line for unavailable Windows error-code constant.

## 4) Rationale
- `target == GetFileAttributesA` is normal before attach; the critical guarantee is `target/original != detour`.
- Splitting target/original reduces ambiguity and prevents accidental recursion from bad pointer state.

## 5) Risks + rollback
- Risk: if external inject/load order mutates function pointers unexpectedly, hook can still fail at runtime.
- Rollback: revert `CryptoLoader/dllmain.cpp` to previous commit and rebuild `CryptoLoader` x86 Release.

## 6) Follow-ups / TODO
- Validate new log keys:
  - `set_hook target_resolved ... target=0x... detour=0x...`
  - `set_hook attach_committed ... current_original=0x...`
  - `set_hook failed_self_reference ...` (should not appear in healthy flow)
