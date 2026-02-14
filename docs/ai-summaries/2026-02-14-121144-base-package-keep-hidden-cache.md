## 1) User request (quote)
> "嗯，之前下载完基础包，是不是会把基础包和分片信息给删除掉？可以不要删除。隐藏在当前文件夹下就行。开着P2P服务。供其他玩家下载。"

## 2) What was done
- Kept base package archive after extraction (no deletion).
- Kept chunk state file (`.chunks.json`) after chunk download completion (no deletion).
- Added hidden-file attribute handling so the kept archive/state files are hidden in the current folder.
- Preserved existing base package download flow as `P2P first -> HTTP chunk resume fallback`.
- Built `RebornLauncher` Release target to verify compile success.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.cpp`
  - Added helper to mark a file hidden via Windows file attributes.
  - Replaced post-extract deletion with hidden-marking for downloaded base package archive.
- `RebornLauncher/WorkThreadChunkDownload.cpp`
  - Added helper to mark files hidden.
  - On already-complete archive, now marks archive and chunk-state files hidden.
  - On successful chunk completion, keeps `.chunks.json` and marks both archive and state file hidden.
- `docs/agent-meta/hot-files-index.md`
  - Updated top-15 access counts.
- `docs/agent-meta/ai-mistake-log.md`
  - Appended one line for a scope/order compile mistake encountered and fixed during this task.

## 4) Rationale
- Keeping base archive and chunk metadata supports reuse and avoids redundant large downloads.
- Hidden attribute keeps current-folder cache files out of normal view while retaining availability.
- Keeping existing fallback behavior preserves robustness when P2P is unavailable.

## 5) Risks + rollback
- Risk: hidden files may still be visible if Explorer is configured to show hidden items.
- Risk: retained cache consumes disk space over time.
- Rollback:
  - Revert `RebornLauncher/WorkThread.cpp` and `RebornLauncher/WorkThreadChunkDownload.cpp` to restore old delete behavior.
  - Revert docs files if metadata rollback is needed.

## 6) Follow-ups/TODO (optional)
- Optionally add a launcher setting/button to clear hidden base-package cache files.
