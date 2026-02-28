# AI Summary - Hooked GetFileAttributes Existence Reuse

## 1) User request (quote)
> "In `DWORD WINAPI HookedGetFileAttributesA(LPCSTR lpFileName)`, call `g_originalGetFileAttributesA` first to determine file existence instead of repeated `std::filesystem::exists`. Convert attributes to a BOOL and continue optimizing."

## 2) What was done
- Reviewed current `CryptoLoader/dllmain.cpp` state including user's partial update.
- Refactored the hook flow to derive `BOOL fileExists` directly from original `GetFileAttributesA` result.
- Updated `HandleHookedFileCheck` to consume `fileExists` and avoid `std::filesystem::exists` in the hot path.
- Kept existing behavior for large-file MD5 skip (>500 KB), missing-file download trigger, and cache checks.
- Built `CryptoLoader` Release target to verify compile success.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Signature change: `HandleHookedFileCheck(const CHAR*, BOOL fileExists)`.
  - `HookedGetFileAttributesA` now computes:
    - `attributes = g_originalGetFileAttributesA(lpFileName)`
    - `fileExists = (attributes != INVALID_FILE_ATTRIBUTES && !directory)`
  - Replaced `std::filesystem::exists` fallback logic with `fileExists`-driven flow.
  - Metadata (`ReadFileMetadataFast`) is now only queried when file exists and expected MD5 is present.
  - Added race guard: if metadata later reports missing after original attributes check, force download.
- `docs/agent-meta/hot-files-index.md`
  - Updated access count for `CryptoLoader/dllmain.cpp`.

## 4) Rationale
- `GetFileAttributesA` is already in the hook path; reusing its result avoids an extra existence syscall and duplicate path work.
- Deferring metadata reads until actually needed reduces overhead for files that do not require MD5 decisions.
- This preserves correctness while reducing hot-path latency during frequent `.img` checks.

## 5) Risks + rollback
- Risk: race between original attribute read and later metadata read can still occur; handled by forcing download if metadata says missing.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp` to previous `HandleHookedFileCheck` implementation.

## 6) Follow-ups/TODO (optional)
- Consider moving launcher download request out of hook thread into a non-blocking queue to reduce stutter in worst-case cache misses.
