# Phase 6 — file, then voice

**Status:** done. Live plugin is not on the bar.  
**Date:** 2026-08-19

## Landed

- `file.send` / `file.accept` / `file.cancel` over Tox `TOX_FILE_KIND_DATA`
- Incoming transfer stays paused until `file.accept`; default dest `~/Downloads/omaq/<name>`, `0600`, cap 8 MiB. An explicit destination override remains supported.
- `call.start` / `call.answer` / `call.stop`: direct-chat-only ToxAV signaling (48 kbit, video 0).
- `helper/av.c` uses bounded 48 kHz mono PCM rings. Capture and playback share one interruptible `libpulse` event-loop thread, while all ToxAV calls remain on the helper iteration thread.
- Incoming audio is downmixed from stereo when needed. Unsupported rates are dropped rather than interpreted incorrectly.
- `verify-6` requires both peers to reach `active`, keeps helper RSS below the existing bound, and fails if either local audio backend reports `audio_unavailable`.
- Calls remain Direct-only. Protocol 12 later extends `file.*`, but not `call.*`, to stable `g:<chat-id>` conversations through a helper-owned custom NGC transport.
- Chat page: file path, accept/decline, 56×56 image previews, call/answer/hang up; group chats expose the same non-call composer and attachment controls when Protocol 12 is active.
- `make verify-6`; `.phase` is 6
- Optional `toxencryptsave` on `tox.save` (`identity.protect` / `unlock` / `unprotect`). Default stays plaintext. Does not change the chat handshake.

## How to check

```text
make verify-6
```

The latest measured call-peak RSS is recorded in `.rss-call-kb`. Gate is 40960 kB (OmaQ.md ≤ 40 MB).

## Stays out

- No bar install
- No inbound firewall change
- No video, no group AV
- AUR package (phase 7)
