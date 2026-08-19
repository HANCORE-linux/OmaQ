# Phase 6 — file, then voice

**Status:** done. Live plugin is not on the bar.  
**Date:** 2026-08-19

## Landed

- `file.send` / `file.accept` / `file.cancel` over Tox `TOX_FILE_KIND_DATA`
- Incoming transfer stays paused until `file.accept`; dest `$OMAQ_HOME/files/<conv>/<name>`, `0600`, cap 8 MiB
- `call.start` / `call.answer` / `call.stop`: 1:1 ToxAV signaling (48 kbit, video 0). PCM capture/playback is not wired; `verify-6` asserts start/stop, not audible audio.
- Empty audio receive callback registered so `toxav_answer` can init the codec
- Groups (`g…`) return `forbidden` for file and call
- Chat page: file path, accept/decline, small image preview of the dest, call/answer/hang up
- `make verify-6`; `.phase` is 6
- Optional `toxencryptsave` on `tox.save` (`identity.protect` / `unlock` / `unprotect`). Default stays plaintext. Does not change the chat handshake.

## How to check

```text
make verify-6
```

Measured call-peak RSS: **21544 kB** (`.rss-call-kb`). Gate is 40960 kB (OmaQ.md ≤ 40 MB).

## Stays out

- No bar install
- No inbound firewall change
- No video, no group AV
- AUR package (phase 7)
