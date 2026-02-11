# AI Summary

## 1) User request (quote)
> 1、在客户端请求下载的时候，猪没跑。你全局都修一下。不管下载什么文件的时候。猪头顶的下载进度都要显示的。现在客户端请求下载时也没显示。。要显示当前下载的文件+下载百分比。

## 2) What was done
- Fixed per-client download state propagation so download animation is not dropped when pid mapping is missing.
- Added broadcast fallback behavior: when `pid=0` (or pid match fails), all tracked clients receive current download state.
- Ensured `DownloadWithResume(...)` always sets start/progress/end state with file name for overlay rendering.
- Added UI-side fallback in overlay refresh to use global current download info (file/progress/size) when per-client fields are incomplete.
- Forced `/download` path to initialize known size/progress from manifest metadata before transfer.
- Kept label format as `文件名 + 百分比` while downloading.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThread.cpp`
  - `MarkClientDownloadStart/Progress/Finished`: changed from strict pid-only to pid-or-broadcast fallback.
  - `DownloadWithResume`: now calls start/progress/end consistently and includes file-name setup.
  - `/download` handler: primes `m_nCurrentDownloadSize/m_nCurrentDownloadProgress` and pushes initial progress state.
- `RebornLauncher/RebornLauncher.cpp`
  - `RefreshPigOverlayState`: added global download fallback (`GetCurrentDownloadFile/Size/Progress`) for display robustness.
  - Download label fallback updated to always render `文件(或unknown) + 百分比` during download.
- `docs/agent-meta/hot-files-index.md`
  - Updated hot-file counters.

## 4) Rationale
- The original bug path was caused by strict pid binding; once pid was missing or unresolved, pig state never entered downloading mode.
- Broadcast + global UI fallback guarantees visible download movement/text for all download paths, including client-triggered downloads.

## 5) Risks + rollback
- Broadcast fallback means that when pid is unavailable, multiple pigs may show the same download progress simultaneously.
- Rollback by reverting:
  - `RebornLauncher/WorkThread.cpp`
  - `RebornLauncher/RebornLauncher.cpp`

## 6) Follow-ups / TODO
- Optional: add a server-side pid validation map to reduce fallback broadcast usage and make per-client mapping stricter.
