# AI Summary - Transparent Pig Overlay Mode

## 1) User request
"我要怎么只显示这头猪，而不显示窗口呢。。。我记得是可以的。"

## 2) What was done
- Switched launcher window style from framed window to transparent popup overlay mode.
- Kept animated pig rendering, but removed solid/gradient background painting.
- Added color-key transparency so only pig sprite and percent text remain visible.
- Added basic usability for frameless mode: left-drag to move, right-click menu for Exit.
- Rebuilt Debug and Release `RebornLauncher` to verify the changes compile.

## 3) Changes
- `RebornLauncher/RebornLauncher.cpp`
  - Window size reduced for overlay mode (`360x280`).
  - Added transparent color key constant and layered window setup.
  - `InitInstance`: changed to `WS_EX_LAYERED | WS_EX_TOOLWINDOW` + `WS_POPUP` (no title bar/frame).
  - `WM_CREATE`: calls `SetLayeredWindowAttributes(..., LWA_COLORKEY)`.
  - `DrawSplashScene`: background now magenta color-key fill; removed visible panel/status line.
  - Added `WM_LBUTTONDOWN` drag behavior and `WM_RBUTTONUP` Exit menu.

## 4) Rationale
- The user wanted “pig only” visual output; framed window and painted background violate that.
- Color-key layered popup is the fastest, low-risk path in current Win32/GDI+ codebase.
- Small overlay window avoids large transparent hit area over desktop.

## 5) Risks + rollback instructions
- Risks:
  - Color-key transparency may hide pixels if exact key color appears in art (unlikely with current sprites).
  - Frameless popup has no standard close button; exit now relies on right-click menu/tray/Alt+F4.
- Rollback:
  1. Revert `RebornLauncher/RebornLauncher.cpp`.
  2. Rebuild target:
     - `cmake --build _builds --config Debug --target RebornLauncher`
     - `cmake --build _builds --config Release --target RebornLauncher`

## 6) Follow-ups / TODO
- Optional: add an INI/env switch to toggle between framed mode and pig-overlay mode at runtime.
