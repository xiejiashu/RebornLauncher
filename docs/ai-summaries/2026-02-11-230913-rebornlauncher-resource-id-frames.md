# AI Summary - RebornLauncher resource ID frames

## 1) User request (quote)
> 代码里有点问题，直接用了资源的路径。而不是用资源的ID。。。我要求的是使用资源的ID。而不是直接使用路径。

## 2) What was done
- Removed runtime animation frame loading that scanned disk paths like `Texture\\PiaoPiaoPig` and loaded PNG files by filename.
- Replaced it with resource-ID-based loading of the embedded PNG resources (`IDB_PIG1`..`IDB_PIG7`).
- Kept the existing fallback drawing path if resource loading fails (no new UI behavior added beyond the requested switch).

## 3) Changes (paths + brief)
- RebornLauncher/RebornLauncher.cpp
  - Added in-module helper `LoadPngFromResource()` using `FindResourceW/LoadResource/CreateStreamOnHGlobal`.
  - Updated `EnsureAnimationFramesLoaded()` to load frames only from resource IDs.
  - Deleted the filesystem search/path-based PNG loading helpers for animation frames.
- RebornLauncher/ResourceManager.cpp
  - Fixed cleanup to `delete` `Gdiplus::Bitmap*` instead of `DeleteObject`.
  - Fixed `LoadPngFromResource()` buffer ownership to avoid double-free when `CreateStreamOnHGlobal(..., TRUE, ...)` is used.
  - Switched to `FindResourceW` + `MAKEINTRESOURCEW` for explicit wide-char resource lookup.
- docs/agent-meta/hot-files-index.md
  - Updated access counts/order.

## 4) Rationale
- The runtime use of file paths makes resource loading depend on the install directory layout; using compiled-in resource IDs ensures the launcher always loads the intended assets regardless of external files.

## 5) Risks + rollback
- Risk: if the PNG resources are missing/mis-typed in the `.rc` (type must be `PNG`), animation frames will not load and the fallback rendering will be used.
- Rollback: revert RebornLauncher/RebornLauncher.cpp to the previous filesystem-based loader (or reintroduce path scanning) and/or revert the resource loader helper.

## 6) Follow-ups/TODO (opt)
- Consider removing the post-build step that copies `Texture/` to output if the launcher no longer needs any runtime textures.
