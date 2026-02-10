# AI Summary - UpdateForge JSON Encrypt/Decrypt UI

## 1) User request
> "UpdateForge 这边加密 URL，要改成加密 JSON 文件内容。换成选择 JSON 文件。加密后的内容显示在输出框。并且增加解密功能，用来确认加密解密是否对称。"

## 2) What was done
- Reworked the UpdateForge mini-tool area from URL input mode to JSON file mode.
- Added a JSON file picker button and wired it into the existing Win32 UI.
- Reused the same bootstrap crypto key/algorithm used by launcher config decrypt path (AES-256 via WinCrypt + SHA-256 derived key), then output hex text.
- Added reverse flow: decrypt hex payload back to plain text and display it in the same output box.
- Added JSON parse validation:
  - Encrypt path validates selected file is valid JSON before encrypting.
  - Decrypt path pretty-prints JSON if parsed successfully.
- Built `UpdateForge` in both Debug and Release to verify compile/runtime wiring.

## 3) Changes
- `UpdateForge/UpdateForge.h`
  - Replaced URL encrypt handler with:
    - `OnBrowseJsonFile()`
    - `OnEncryptJsonPayload()`
    - `OnDecryptJsonPayload()`
  - Added new control handles:
    - `m_btnJsonBrowse`
    - `m_btnDecryptPayload`
- `UpdateForge/UpdateForge.cpp`
  - Added new button IDs:
    - `IDC_BTN_JSON_BROWSE`
    - `IDC_BTN_JSON_ENCRYPT`
    - `IDC_BTN_JSON_DECRYPT`
  - Added helpers:
    - UTF-8 conversion and control text extraction.
    - `EncryptBootstrapPayloadToHex(...)`
    - `DecryptBootstrapPayloadFromHex(...)`
    - hex decode helper.
    - JSON file picker and file reader helpers.
  - UI updates:
    - Input row changed to `JSON File` + `Browse JSON`.
    - Action row changed to `Encrypt JSON` + `Decrypt Text`.
    - Payload output edit control changed to multiline so long hex/JSON is visible.
  - Command routing updated for new buttons.
  - Busy-state enable/disable updated for new controls.
- `docs/agent-meta/hot-files-index.md`
  - Refreshed Top 15 entries and counts.

## 4) Rationale
- This aligns tool behavior with the launcher bootstrap flow that now consumes encrypted JSON config instead of a single URL.
- Keeping crypto key/algorithm consistent with launcher decrypt path avoids mismatch risk.
- In-place decrypt output provides a direct symmetry check for your encrypted ReadMe payload workflow.

## 5) Risks + rollback instructions
- Risk: Existing users expecting the old "Encrypt URL" field behavior will now see JSON-file workflow.
- Risk: If payload is not hex or key/algorithm differs from launcher side, decrypt will fail by design.
- Rollback:
  - `git checkout -- UpdateForge/UpdateForge.h`
  - `git checkout -- UpdateForge/UpdateForge.cpp`
  - `git checkout -- docs/agent-meta/hot-files-index.md`

## 6) Follow-ups / TODO
- Optional: Add a "Copy payload" button for quick publish to `ReadMe.txt`.
- Optional: Cache last selected JSON file path in `UpdateForge.settings.ini`.
