# AI Summary - Launcher output dirs unified

## 1) User request (quote)
> 把Launcher的调试目录，运行目录，和生成目录都放到一起。

## 2) What was done
- Updated CMake target properties for `RebornLauncher` so its build outputs (exe/pdb) and Visual Studio debugger working directory all point to the RebornLauncher sub-build directory (e.g. `_builds/RebornLauncher/`).
- Ran a Debug build of the `RebornLauncher` target to verify the output location.

## 3) Changes
- RebornLauncher/CMakeLists.txt
  - Set `RUNTIME_OUTPUT_DIRECTORY_*`, `PDB_OUTPUT_DIRECTORY_*`, `COMPILE_PDB_OUTPUT_DIRECTORY_*` (plus archive/library) to `${CMAKE_CURRENT_BINARY_DIR}`.
  - Set `VS_DEBUGGER_WORKING_DIRECTORY` to `${CMAKE_CURRENT_BINARY_DIR}`.
- docs/agent-meta/hot-files-index.md
  - Bumped access counts for the CMake files touched/read.

## 4) Rationale
- Keeping the launcher’s output (exe/pdb), run directory, and VS debug working directory in the same folder simplifies local testing/debugging, especially when the launcher depends on files placed next to the executable (config/data/resources).

## 5) Risks + rollback
- Risk: Debug/Release (and other configs) now output to the same directory, so building multiple configs can overwrite `RebornLauncher.exe/.pdb`.
- Rollback: Revert the changes in `RebornLauncher/CMakeLists.txt`, then re-run CMake generation (or `build_vs2022.bat`) and rebuild.

## 6) Follow-ups / TODO (optional)
- If you want to avoid config overwrites, switch to per-config subfolders under `_builds/RebornLauncher/` and keep the VS debugger working directory at the shared root (where `Data/` and `config.ini` live).
