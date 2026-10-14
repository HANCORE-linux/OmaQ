# Phase 8 — Double Ratchet on 1:1

**Status:** done. Live plugin is not on the bar.  
**Date:** 2026-08-19

## Landed

- Arch extra `libsignal-protocol-c` 2.3.3-2 (owner: `omarchy pkg add libsignal-protocol-c`)
- `ratchet_adapt.c` is the only file that includes Signal headers; OpenSSL 3 `libcrypto` is the crypto provider (HMAC/AES already on the box)
- Direct `msg.send` is Double Ratchet over Tox. `OQB2|q|` / `OQB2|r|` distinguish exact, request-correlated, durably replayable bundle requests and responses, legacy `OQB1` remains the compatibility path, and `OQR1` carries ciphertext. Per-peer one-time prekeys are random, persisted before publication, peer-bound on consumption, recoverable after an interrupted consume, and durable across restart. Lost and half-open sessions rotate stale bootstrap material and request a fresh peer-bound prekey without plaintext fallback. Chat events are decrypted plaintext; successful sends emit a confirmed outgoing `message` event.
- A direct `msg.send` before the session exists no longer fails with `ratchet_pending`. The helper holds it in a bounded queue (`send_queue.c`, 64 messages and 16 per contact), bound to the contact's stable key, the own identity and the IPC client that sent it, and sends the queue in order once the session exists. It sends one bundle request per waiting phase, because every `OQB2|q|` request rotates the bootstrap prekey. A message that still waits after 5 minutes fails with `ratchet_timeout`; a contact that goes offline, is removed or changes key, an identity change and a helper shutdown fail it at once. When its client disconnects, the helper drops it without an event, because that client already shows it as `delivery_unknown`. Sent messages are never sent again, and a full queue still answers `ratchet_pending`. `tests/phase8.sh` sends two messages before the session and requires both in order without a failure; the helper of 1.1.2 fails both with `ratchet_pending`.
- Direct invite `rk=` (64 hex identity) and the token-authenticated peer pin are persisted; bundles without the expected pin are rejected. Groups unchanged (no `rk`).
- Ratchet identity, expected pins and session records persist below `$OMAQ_HOME/ratchet/` with `0600` files.
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
