# Phase 2 — invite is safe

**Status:** done. Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- `rate.c`: 5/min/key and 20/hour global (gold + hour roll)
- `safety.c`: order-independent code from two 64-hex public keys
- `qr.c`: `invite.qr` writes a PNG via `/usr/bin/qrencode`; `zbarimg` reads it back
- Token expiry on redeem (`invite_expired`) and on the receiver after `e`
- `invite.revoke` clears the live token; a later redeem does not emit `request`
- `nospam.rotate` changes the Tox address and voids every open invite
- `contact.remove` deletes the friend; `safety.get` after accept matches on both sides
- Panel: create/copy invite, save QR, revoke, redeem, accept/decline, safety code, remove, rotate-ID confirm
- `make verify-2`; `.phase` is 2

## How to check

```text
make verify-2
```

## Measured

- Helper RSS during phase2 two-home run: **6072 kB** (under 1.5× the phase-1 baseline of 6648 kB)

## Stays out

- No copy to `~/.config/omarchy/plugins/`
- No AUR upload
- Groups (phase 3)
- Cards, pin, sounds, themes (phase 4)
