## 1) User request (quote)
> "G:\\MapleStoryStudio\\Fire\\UpdateForge\\RebornLauncher 里面的文件改下名。很多文件名为啥是WorkThread开头。删除掉这个开头。比如WorkThreadArchive.cpp 改成 Archive.cpp等。只留WorkThread.h/cpp这个类吧。或者这个类命名也改一下。就命名合理一点。"

## 2) What was done
- Renamed `RebornLauncher` helper files that started with `WorkThread` by removing the prefix.
- Kept `WorkThread.h` and `WorkThread.cpp` as requested.
- Updated all affected `#include` references to new header names.
- Reconfigured CMake and rebuilt `RebornLauncher` (Debug) to verify rename integrity.

## 3) Changes (paths + brief)
- `RebornLauncher/WorkThreadArchive.cpp` -> `RebornLauncher/Archive.cpp`
- `RebornLauncher/WorkThreadChunkDownload.cpp` -> `RebornLauncher/ChunkDownload.cpp`
- `RebornLauncher/WorkThreadChunkExecutor.cpp` -> `RebornLauncher/ChunkExecutor.cpp`
- `RebornLauncher/WorkThreadChunkExecutor.h` -> `RebornLauncher/ChunkExecutor.h`
- `RebornLauncher/WorkThreadChunkState.cpp` -> `RebornLauncher/ChunkState.cpp`
- `RebornLauncher/WorkThreadChunkState.h` -> `RebornLauncher/ChunkState.h`
- `RebornLauncher/WorkThreadClientState.cpp` -> `RebornLauncher/ClientState.cpp`
- `RebornLauncher/WorkThreadCrypto.cpp` -> `RebornLauncher/Crypto.cpp`
- `RebornLauncher/WorkThreadDownloadResume.cpp` -> `RebornLauncher/DownloadResume.cpp`
- `RebornLauncher/WorkThreadHttpSession.cpp` -> `RebornLauncher/HttpSession.cpp`
- `RebornLauncher/WorkThreadHttpSession.h` -> `RebornLauncher/HttpSession.h`
- `RebornLauncher/WorkThreadLocalVersionLoader.cpp` -> `RebornLauncher/LocalVersionLoader.cpp`
- `RebornLauncher/WorkThreadLocalVersionLoader.h` -> `RebornLauncher/LocalVersionLoader.h`
- `RebornLauncher/WorkThreadLogging.cpp` -> `RebornLauncher/Logging.cpp`
- `RebornLauncher/WorkThreadManifest.cpp` -> `RebornLauncher/Manifest.cpp`
- `RebornLauncher/WorkThreadMapping.cpp` -> `RebornLauncher/Mapping.cpp`
- `RebornLauncher/WorkThreadNetUtils.cpp` -> `RebornLauncher/NetUtils.cpp`
- `RebornLauncher/WorkThreadNetUtils.h` -> `RebornLauncher/NetUtils.h`
- `RebornLauncher/WorkThreadResumeDownload.cpp` -> `RebornLauncher/ResumeDownload.cpp`
- `RebornLauncher/WorkThreadResumeDownload.h` -> `RebornLauncher/ResumeDownload.h`
- `RebornLauncher/WorkThreadRunCoordinator.cpp` -> `RebornLauncher/RunCoordinator.cpp`
- `RebornLauncher/WorkThreadRunCoordinator.h` -> `RebornLauncher/RunCoordinator.h`
- `RebornLauncher/WorkThreadRuntimeUpdater.cpp` -> `RebornLauncher/RuntimeUpdater.cpp`
- `RebornLauncher/WorkThreadRuntimeUpdater.h` -> `RebornLauncher/RuntimeUpdater.h`
- `RebornLauncher/WorkThreadWebService.cpp` -> `RebornLauncher/WebService.cpp`
- `RebornLauncher/WorkThreadWindowTracking.cpp` -> `RebornLauncher/WindowTracking.cpp`
- `RebornLauncher/WorkThread.cpp` - include paths updated to renamed headers.
- `docs/agent-meta/hot-files-index.md` - updated top-15 entries to renamed file paths and refreshed counts.

## 4) Rationale
- The rename removes redundant `WorkThread` filename prefix from helper modules while preserving the main `WorkThread` class entry points (`WorkThread.h/.cpp`), matching the request and reducing filename noise.

## 5) Risks + rollback
- Risk: Existing IDE/cached build files may still reference old filenames.
- Mitigation: Ran `cmake -S . -B _builds` then `cmake --build _builds --config Debug --target RebornLauncher` successfully.
- Rollback: `git restore --source=HEAD --worktree --staged RebornLauncher` (or restore only specific renamed files) to revert to pre-rename state.

## 6) Follow-ups / TODO (optional)
- Optional: If desired, helper class names (e.g. `WorkThreadRunCoordinator`) can also be renamed to drop `WorkThread` prefix for full naming consistency.
