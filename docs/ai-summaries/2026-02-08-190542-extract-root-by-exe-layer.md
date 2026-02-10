# AI Summary - Extract Root By EXE Layer

## 1) User request
> "exe not always MapleFireReborn.exe. It should work for various projects. There may be multiple exe files, and only one directory layer contains exe files. Use that layer as extraction root."

## 2) What was done
- Removed hardcoded `MapleFireReborn.exe` root detection in archive extraction logic.
- Implemented generic detection for all `.exe` entries in archive paths (case-insensitive).
- Determined extraction root from the common parent path of exe-containing directories.
- Kept current extraction behavior to output into current directory from that detected root.
- Rebuilt `RebornLauncher` Release to verify.

## 3) Changes
- `RebornLauncher/WorkThread.cpp`
  - Added helper functions:
    - `SplitArchivePath(...)`
    - `JoinArchivePath(...)`
    - `GetLowerAscii(...)`
    - `DetermineExeRootPrefix(...)`
  - Updated `ScanArchive(...)` to set `m_extractRootPrefix` using generic `.exe` scan result.
  - Removed obsolete hardcoded exe-name comparison helper.
- `docs/agent-meta/hot-files-index.md`
  - Updated file access count.

## 4) Rationale
- Hardcoding one executable name breaks portability across different projects.
- Detecting the exe-containing layer directly from archive entries generalizes extraction root behavior while preserving your requested directory flattening rule.

## 5) Risks + rollback instructions
- Risk: if an archive has exe files spread across unrelated branches, the common parent may be higher than expected.
- Rollback:
  - `git checkout -- RebornLauncher/WorkThread.cpp`
  - `git checkout -- docs/agent-meta/hot-files-index.md`

## 6) Follow-ups / TODO
- Optional: add a bootstrap JSON override for extraction root if some packages need explicit control.
