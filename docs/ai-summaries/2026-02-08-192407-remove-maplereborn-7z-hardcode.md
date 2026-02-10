# AI Summary - Remove MapleFireReborn.7z Hardcode

## 1) User request
> "MapleFireReborn.7z 也不要用硬编码，根据URL的名字来。"

## 2) What was done
- Removed hardcoded base package archive name usage in download/extract flow.
- Added URL filename extraction helper and used it as local archive filename.
- Updated base package flow to:
  - download using URL-derived filename
  - extract that downloaded archive
  - remove that downloaded archive after extract
- Removed hardcoded fallback injection of `MapleFireReborn.7z` when bootstrap JSON omits base package URL.
- Added explicit bootstrap validation failure when `base_package_urls` / `base_package_url` is missing.
- Built `RebornLauncher` Release successfully.

## 3) Changes
- `RebornLauncher/WorkThread.cpp`
  - Added `GetFileNameFromUrl(...)`.
  - `DownloadBasePackage()` now uses URL-derived `localArchivePath` instead of fixed `MapleFireReborn.7z`.
  - `Extract7z(...)` and post-extract remove now use the actual downloaded filename.
  - `FetchBootstrapConfig()` now fails if no base package URL is provided (instead of hardcoded fallback).
- `docs/agent-meta/hot-files-index.md`
  - Updated file access count.

## 4) Rationale
- Archive naming should follow server-provided package URL and not depend on project-specific fixed names.
- Hardcoded fallback can silently point to wrong package name for other projects.

## 5) Risks + rollback instructions
- Risk: old bootstrap payloads that do not provide base package URL will now fail base package phase.
- Rollback:
  - `git checkout -- RebornLauncher/WorkThread.cpp`
  - `git checkout -- docs/agent-meta/hot-files-index.md`

## 6) Follow-ups / TODO
- Optional: add explicit JSON field for launcher executable name to remove remaining exe-name assumptions in process launch logic.
