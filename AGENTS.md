# UpdateForge Agent Notes (minimal)

## Invariants
- C/C++ builds force `/utf-8` via top-level CMake → keep source/text files UTF-8 to avoid MSVC codepage warnings.

## RebornLauncher (UI + download)
- New standard window UI: P2P toggle, STUN server list (add/remove), status text, 2 progress bars (total queue + current file). Remove old layered/GDI+ scene.
- P2P uses WebRTC (libdatachannel) + optional HTTP signaling (default: `<download base>/signal`); fallback to HTTP if negotiation fails.
- Persist STUN list: `RebornLauncher/p2p_stun_servers.txt` (UTF-8).
- WorkThread checks UI P2P settings before each download: if enabled try P2P first, else HTTP resume logic.

## Windows build (VS2022 + vcpkg cache + Crypto++)
- Run `build_vs2022.bat` from repo root:
  - uses local `vcpkg_installed` cache (classic mode, manifest off)
  - runs `vcpkg install` only if cache missing
  - builds Release + Debug via VS2022 generator
- Crypto++ rebuilt locally with VS2022 static CRT:
  - libs: `vcpkg_installed/x64-windows-static/{lib,debug/lib}/cryptopp.lib`
  - headers: `vcpkg_installed/x64-windows-static/include/cryptopp`
  - config: `vcpkg_installed/x64-windows-static/share/cryptopp` (minimal)
  - script: `_deps/cryptopp-src/build_cryptopp.cmd`

## Agent workflow (keep this file small)
### A) AI Summary after EVERY task (mandatory)
- Write summary even if no files changed.
- Path: `docs/ai-summaries/`
- Name: `YYYY-MM-DD-HHMM-<short-title>.md` (24h; add seconds if needed)
- TZ: Asia/Tokyo
- Sections: (1) User request (quote) (2) What was done (3) Changes (paths + brief) (4) Rationale (5) Risks + rollback (6) Follow-ups/TODO (opt)
- No large diffs; if none, say “No changes” + why.

### B) Hot Files Index
- Track in `docs/agent-meta/hot-files-index.md`
- Top 15 paths only; sort by access count desc
- Exclude generated/vendor dirs (e.g. `node_modules/ dist/ build/ vendor/`).

### C) Encoding safety (mandatory)
- All edited text files UTF-8; C/C++ source UTF-8 (matches `/utf-8`).
- Prefer `apply_patch`; avoid PowerShell `Set-Content`/`Out-File` defaults.
- If scripting writes: UTF-8 no BOM (e.g. `.NET UTF8Encoding(false)`).
- Never blind-replace punctuation globally (e.g. replace all `?`).
- After edits scan mojibake (changed files):
  - `rg -n "[^\x00-\x7F]" <changed-files> -S`
  - `rg -n "\\xEF\\xBF\\xBD|\\?\\?\\?" <changed-files> -S`

### AI Mistake Log (≤1000 words)
- On detected error append ONE line: `Mistake → Cause → Fix`; merge dups; prune oldest.
- Common entries:
  - Misread req/constraints → skimmed/assumed → reread + restate reqs
  - Wrong arithmetic/count → mental slip → compute step-by-step + sanity check
  - Hallucinated facts → no sources → verify/cite/mark unknown
  - Outdated “latest/current” → ignored recency → web-check dates + prefer newest
  - Overconfident assumption → filled gaps → label assumptions; ask only if blocking
  - Missed edge-case wording → pattern-match → parse literally; test counterexamples
  - Tool misuse/wrong path → skipped tool docs → follow workflow; validate paths
  - Formatting/spec miss → forgot template → checklist; verify output
  - Language/verbosity mismatch → ignored preference → match language; keep concise