# UpdateForge Java Signaling Service Spec

## 1. Purpose
This document is for the Java backend team to implement the service required by `RebornLauncher` P2P-first download flow.

Current launcher strategy:
1. Try P2P endpoint first.
2. If P2P fails, fallback to HTTP CDN/base URL.

Important: current launcher client **does not yet open a real WebRTC data channel**. It currently calls HTTP endpoints on `signalEndpoint` and expects file bytes.

## 2. Current Client Contract (Must Support Now)

Launcher passes a **relative resource path** (`page + name`), examples:
- `Update/1736140000/Data/Base.wz`
- `MapleReborn.7z`

Client request candidates (same server):
1. `GET /signal?url=<relative-path>`
2. `GET /signal?path=<relative-path>`
3. `GET /signal?page=<relative-path>`
4. `GET /signal/<relative-path>`

Headers client may send:
- `X-UpdateForge-Relative-Path: <relative-path-without-leading-slash>`
- `X-UpdateForge-Relative-Url: /<relative-path>`

Success criteria expected by launcher:
- HTTP status: `200` or `206`
- Body: raw file bytes
- Content-Type: **NOT JSON** (JSON is treated as signaling response and skipped)

Failure behavior (acceptable):
- Return non-2xx, or JSON error, launcher will fallback to HTTP download.

## 3. Minimal Java Implementation (Recommended First)

Implement a "P2P relay" service compatible with current launcher behavior:

- Parse relative path from query/path/header (priority):
  1. `url`
  2. `path`
  3. `page`
  4. path suffix after `/signal/`
  5. `X-UpdateForge-Relative-Path`

- Normalize path:
  - Replace `\\` with `/`
  - Remove leading `/`
  - Reject `..`, absolute path, drive letters

- Resolve to file source:
  - Preferred: local shared update storage
  - Optional: proxy to upstream static host

- Return streaming bytes:
  - Support `Range` if possible (206)
  - Set `Content-Length`
  - Set `Content-Type: application/octet-stream`

### 3.1 Spring Boot API shape
- `GET /signal`
- `GET /signal/**`

Both should call same resolver.

## 4. Security Requirements

Must have:
- Path traversal defense (`..`, `%2e%2e`, absolute path, Windows drive path)
- Access control (token or signed URL) before exposing file content
- Rate limiting per IP/session
- Request timeout + response streaming timeout
- Audit log: request id, relative path, status, bytes, latency

Recommended:
- mTLS or internal network only
- Optional SHA-256 response header for diagnostics: `X-File-Sha256`

## 5. Optional Phase 2: Real WebRTC Signaling

If backend wants true peer-to-peer later, add WebSocket signaling service:

- `GET /ws/signal?sessionId=<id>&clientId=<id>&role=leecher|seeder`

Message envelope:
```json
{
  "type": "offer|answer|ice|request|ready|error",
  "sessionId": "string",
  "from": "string",
  "to": "string",
  "resource": "Update/1736140000/Data/Base.wz",
  "sdp": "...",
  "candidate": "...",
  "sdpMid": "0",
  "sdpMLineIndex": 0,
  "timestamp": 1736200000
}
```

Routing rules:
- Match peers by `sessionId + resource`
- Forward offer/answer/ice to counterpart
- TTL cleanup for stale sessions

Note:
- Launcher side must later be upgraded to actual `rtc::PeerConnection` + DataChannel for this phase.

## 6. Response Codes (Current HTTP Contract)

Suggested:
- `200`: full file
- `206`: range response
- `400`: invalid resource path
- `401/403`: unauthorized
- `404`: file not found
- `429`: throttled
- `500`: internal error

Error body should be plain text (or JSON if you intentionally want launcher fallback immediately).

## 7. Quick Self-Test Cases

1. `GET /signal?url=MapleReborn.7z` -> 200 + binary
2. `GET /signal?page=Update/1736140000/Data/Base.wz` -> 200 + binary
3. `GET /signal/Update/1736140000/Data/Base.wz` -> 200 + binary
4. `GET /signal?url=../../windows/system32/cmd.exe` -> 400/403
5. missing file -> 404
6. large file range request -> 206

## 8. Hand-off Notes for Java AI

- Implement **Section 2 + Section 3 first** to unblock launcher.
- Do not wait for full WebRTC phase.
- Keep endpoint path `/signal` stable, because launcher default is `<download-base>/signal`.
