# AI Summary - CryptoLoader Large File MD5 Skip

## 1) User request (quote)
> "Players report lag when opening the world map. I think CryptoLoader file verification causes this; large file MD5 may be too slow. If file size is over 500KB, skip verification. If file is missing, still download. If file exists, verification can be skipped. Please also analyze and optimize CryptoLoader."

## 2) What was done
- Traced runtime file-check path in `CryptoLoader/dllmain.cpp` and confirmed hot path computes file MD5 during hooked `.img` access.
- Implemented size-threshold optimization: existing files larger than 500 KB now skip MD5 verification in the hook path.
- Kept missing-file behavior unchanged: when file does not exist, launcher download request is still triggered.
- Built `CryptoLoader` Release target to verify no compile regression.
- Ran encoding safety scans on changed source file.

## 3) Changes (paths + brief)
- `CryptoLoader/dllmain.cpp`
  - Added `kMd5SkipSizeThresholdBytes = 500 * 1024`.
  - In hooked file check flow, if metadata read fallback is used, file size is now fetched for existing files.
  - Added branch: for existing files with expected MD5 and `size > 500KB`, skip MD5 compute and treat as up-to-date.
  - Preserved original MD5+cache behavior for small files and missing-file download behavior.
- `docs/agent-meta/hot-files-index.md`
  - Updated counts and ordering (top 15 sorted by access count descending).

## 4) Rationale
- Opening large map assets triggers frequent `.img` file checks.
- Full-file MD5 hashing on large files in a hook path is expensive and can cause visible frame hitching/stutter.
- The requested policy is performance-first: for large existing files, existence is enough; integrity check is skipped to avoid blocking.
- Missing files still trigger launcher download, preserving critical availability behavior.

## 5) Risks + rollback
- Risk: corrupted/tampered files larger than 500 KB may no longer be detected by MD5 in CryptoLoader hook path.
- Rollback:
  - Revert `CryptoLoader/dllmain.cpp` to previous logic, or
  - Set threshold to `0` (effectively no skip), or
  - Replace condition with stricter policy (e.g., whitelist paths only).

## 6) Follow-ups/TODO (optional)
- Add lightweight telemetry/counters for hook latency and MD5 skip hits to validate stutter reduction.
- Consider moving download trigger to asynchronous/non-blocking path in hook context to reduce worst-case stalls.
