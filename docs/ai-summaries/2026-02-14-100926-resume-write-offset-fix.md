## 1) User request (quote)
> “在 Launcher下的WorkThreadResumeDownload.cpp 第121行是有问题的。这里下载后指针没向后移。然后每次写入文件都把之前的覆盖掉。这是AI写的，所以你记得标记下这们AI易错问题。并且修复它。”

## 2) What was done
- Fixed HTTP resume write logic to use explicit file write offsets instead of relying on implicit append behavior.
- Adjusted `Range` header end-byte handling to avoid requesting one byte past file end.
- Added one entry to the AI mistake log for this class of issue.
- Updated hot files index ordering/counts and replaced old resume filename entry.
- Ran encoding/mojibake safety scans on changed files.
- Built `RebornLauncher` Debug target to verify compile success.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadResumeDownload.cpp`
  - Replaced append-only stream open with read/write stream + create-if-missing fallback.
  - Added `nextWriteOffset` and explicit `seekp` before each chunk write.
  - Added write failure checks in callback.
  - Fixed `Range` end-byte from total size to `totalSize - 1` when known.
- `docs/agent-meta/ai-mistake-log.md`
  - Appended one line: resume chunk write pointer/offset handling mistake and corrective rule.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 ordering by count and swapped in `RebornLauncher/WorkThreadResumeDownload.cpp`.

## 4) Rationale
- Explicit offset tracking makes chunk writes deterministic and prevents accidental overwrite from ambiguous stream-position behavior.
- Correct byte-range upper bound reduces protocol/edge-case issues with servers that strictly validate `Range`.

## 5) Risks + rollback
- Risk: some environments may behave differently with `in|out` stream mode on unusual filesystems.
- Mitigation: create-if-missing fallback is included; build validation passed.
- Rollback: revert `RebornLauncher/WorkThreadResumeDownload.cpp`, `docs/agent-meta/ai-mistake-log.md`, and `docs/agent-meta/hot-files-index.md` to previous revisions.

## 6) Follow-ups/TODO (optional)
- Add an integration test for resumed HTTP download that verifies final file content hash and exact byte offsets.
