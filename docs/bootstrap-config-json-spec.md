# Bootstrap Config JSON Spec

This document defines the JSON payload to encrypt and publish at:

- `/MengMianHeiYiRen/MagicShow/raw/master/ReadMe.txt`

Launcher flow:

1. Download `ReadMe.txt`.
2. Decode hex payload to bytes.
3. AES decrypt bytes (same key as current launcher).
4. Parse decrypted plaintext as JSON.
5. Use JSON fields to drive signal/STUN/download behavior.

## Recommended JSON Structure

```json
{
  "schema_version": 1,
  "content": {
    "version_manifest_url": "https://cdn.example.com/game/live/Version.dat",
    "update_package_root_url": "https://cdn.example.com/game/live/",
    "base_package_urls": [
      "https://mirror-a.example.com/game/MapleReborn.7z",
      "https://mirror-b.example.com/game/MapleReborn.7z"
    ]
  },
  "p2p": {
    "signal_url": "https://signal.example.com/signal",
    "signal_auth_token": "replace-with-token",
    "stun_servers": [
      "stun:127.0.0.1:3478",
      "stun:stun.l.google.com:19302"
    ]
  },
  "meta": {
    "channel": "live",
    "generated_at": "2026-02-08T13:50:00+09:00"
  }
}
```

## Field Notes

- `schema_version`:
  - required
  - current value: `1`

- `content.version_manifest_url`:
  - required
  - full URL to `Version.dat`

- `content.update_package_root_url`:
  - optional but strongly recommended
  - full URL root used for runtime downloads (`Update/<time>/<file>`)
  - if omitted, launcher derives root from `version_manifest_url`

- `content.base_package_urls`:
  - optional array
  - base package mirrors (launcher tries in order)
  - if omitted, launcher falls back to `<update_package_root_url>/MapleReborn.7z`

- `p2p.signal_url`:
  - optional
  - if launcher/UI didn’t set signal endpoint locally, this value is used

- `p2p.signal_auth_token`:
  - optional
  - used as `Authorization: Bearer <token>` and `X-Signal-Auth-Token`

- `p2p.stun_servers`:
  - optional array
  - merged into local STUN list (de-duplicated, remote list priority)

## Compatibility

- `content` missing:
  - launcher also checks `download` object as fallback.

- `version_manifest_url` missing:
  - launcher checks `version_dat_url` fallback key.

## Publishing Format

- Encrypt JSON string with existing launcher algorithm.
- Hex-encode encrypted bytes.
- Publish hex text as `ReadMe.txt` body.
