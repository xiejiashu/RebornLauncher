# AI Summary - Encoding/Mojibake Fix

## 1) User request
> "There is garbled text again. Fix all mojibake and add rules in the agent docs to prevent encoding issues."

## 2) What was done
- Re-scanned launcher-related files and agent docs for mojibake/non-ASCII artifacts.
- Repaired corrupted quote text in `AGENTS.md` and normalized the file as clean UTF-8 text.
- Added explicit encoding prevention rules to `AGENTS.md` (edit method, UTF-8 policy, post-edit scan commands).
- Fixed a damaged comment made of placeholder question marks in `RebornLauncher/WorkThread.h` with a clear English comment.
- Re-ran scan checks to verify no remaining mojibake markers in target files.

## 3) Changes
- `AGENTS.md`: fixed corrupted lines; added section `### C) Encoding safety (mandatory)`.
- `RebornLauncher/WorkThread.h`: replaced corrupted comment above `WriteDataToMapping()` with a valid comment.
- `docs/agent-meta/hot-files-index.md`: refreshed top accessed files and counts.

## 4) Rationale
- The immediate issue was encoding corruption in documentation/comments. Fixing text and adding hard rules prevents repeat incidents.
- Keeping the prevention policy in `AGENTS.md` makes future agent edits deterministic (UTF-8 + safer edit workflow + scan gate).

## 5) Risks + rollback instructions
- Risk: replacing `AGENTS.md` content as a full-file rewrite may drop any unseen local manual edits made outside this task.
- Rollback:
  - `git checkout -- AGENTS.md`
  - `git checkout -- RebornLauncher/WorkThread.h`
  - `git checkout -- docs/agent-meta/hot-files-index.md`
  - Or selectively restore from `git diff`.

## 6) Follow-ups / TODO
- Optional: run a repo-wide scan before next release, e.g. `rg -n "[^\\x00-\\x7F]" RebornLauncher docs -S`.
