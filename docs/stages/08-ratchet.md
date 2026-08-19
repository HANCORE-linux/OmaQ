# Phase 8 — Double Ratchet on 1:1

**Status:** done. Live plugin is not on the bar.  
**Date:** 2026-08-19

## Landed

- Arch extra `libsignal-protocol-c` 2.3.3-2 (owner: `omarchy pkg add libsignal-protocol-c`)
- `ratchet_adapt.c` is the only file that includes Signal headers; OpenSSL 3 `libcrypto` is the crypto provider (HMAC/AES already on the box)
- Direct `msg.send` is Double Ratchet over Tox. Wire prefix `OQB1` (bundle) / `OQR1` (ciphertext). Chat events are decrypted plaintext.
- Invite `rk=` (64 hex identity). Groups unchanged (no `rk`).
- One helper, one Tox identity. No Tor process.
- `make verify-8`; measured helper RSS **11252 kB** (cap 51200 kB)

## How to check

```text
make verify-8
```

## Stays out

- No bar install
- No Tor, no per-contact Tox instance
- Group messages still Tox-native
- File and call still Tox-file / ToxAV
- AUR (phase 7 halted)
