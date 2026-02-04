# UpdateForge Agent Notes

- All C/C++ builds force `/utf-8` via top-level CMake; keep source files in UTF-8 to avoid MSVC codepage warnings.
- RebornLauncher ships a new standard window UI: P2P toggle, STUN server list (add/remove), status text, and dual progress bars (total queue and current file). Old layered/GDI+ scene is removed.
- P2P downloads use WebRTC (libdatachannel) with optional HTTP signaling (`<download base>/signal` by default) and fall back to HTTP if negotiation fails. STUN list persists in `RebornLauncher/p2p_stun_servers.txt` (UTF-8).
- WorkThread consults UI-driven P2P settings before each download; if enabled, it attempts P2P first, otherwise HTTP resume logic.
- After each AI change, write a short summary markdown log (see docs/ai-summaries/) to keep an audit trail.
