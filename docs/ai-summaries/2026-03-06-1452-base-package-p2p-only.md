# 1) User request (quote)
> “现在的Launcher项目除了7z文件以外，其他的零散文件有可能走P2P吗？我要求的是只有base包走P2P，因为其他零散的包都太小了。有些都不够分块的。”

# 2) What was done
- Audited RebornLauncher download flow to confirm where P2P is used.
- Verified runtime/scattered file path (`DownloadWithResume`) previously attempted `TryP2P` before HTTP.
- Added an explicit `allowP2P` switch to `DownloadWithResume`, defaulting to `false`.
- Gated P2P call in `DownloadWithResume` behind `allowP2P`, so runtime/scattered downloads no longer use P2P by default.
- Kept base package P2P logic unchanged in `DownloadBasePackage()` (`ResumeDownloader::TryP2P` remains there).

# 3) Changes (paths + brief)
- `RebornLauncher/LauncherUpdateCoordinator.h`
  - Extended `DownloadWithResume(...)` signature with `bool allowP2P = false`.
- `RebornLauncher/DownloadResume.cpp`
  - Updated `DownloadWithResume(...)` definition to accept `allowP2P`.
  - Changed P2P branch to `if (allowP2P && downloader.TryP2P(...))`.
- `docs/agent-meta/hot-files-index.md`
  - Updated access counts and corrected order to descending.

# 4) Rationale
- Small fragmented files are poor P2P candidates and may not meet chunking assumptions.
- Restricting P2P to base package avoids negotiation overhead and unstable behavior on tiny files, while preserving large-package acceleration.

# 5) Risks + rollback
- Risk: Any caller that expected implicit P2P in `DownloadWithResume` now gets HTTP-first behavior unless it explicitly passes `allowP2P=true`.
- Rollback:
  - Revert `RebornLauncher/LauncherUpdateCoordinator.h` and `RebornLauncher/DownloadResume.cpp`, or
  - Keep current API and pass `allowP2P=true` at specific call sites if selective re-enable is needed.

# 6) Follow-ups/TODO (opt)
- Optionally wire `allowP2P=true` only for explicitly tagged large assets in manifest metadata if future mixed strategy is needed.
