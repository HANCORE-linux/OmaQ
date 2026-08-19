# Current stand — 2026-08-19

OmaQ is a Quattro bar plugin. Chat is German with the owner; repo and UI strings are English. Plugin id `hancore.omaq`. Private repo `HANCORE-linux/OmaQ`. Live plugin is **off the bar**. No silent shell restart. AUR registration is off.

## Phases

| Phase | What | Status |
|---|---|---|
| 0 Harness | Makefile, gold driver, licenses | done |
| 1 1:1 chat | singleton helper, Tox, invite | done |
| 2 Invite safety | QR, revoke, nospam, safety code, rate | done |
| 3 Groups | owner > admin > member | done |
| 4 Surfaces | cards, pin, sounds, themes | done |
| 5 Daily | export/import, search | done |
| 6 File + voice | Tox file 8 MiB, 1:1 audio | done |
| 7 AUR | PKGBUILD / upload | **halted** (no AUR account) |
| 8 Double Ratchet | Signal payload on 1:1, 50 MB cap | **done** (`verify-8`) |

## Security (as built)

- Transport: Tox (libsodium). Relays do not read chat.
- Identity at rest: optional `toxencryptsave` passphrase.
- Direct messages: Signal Double Ratchet (`libsignal-protocol-c`) inside Tox. Invite `rk=` binds the ratchet identity. Stolen Tox handshake (KCI) does not decrypt ratchet text once `rk` came from the QR.
- Not SimpleX: one durable Tox id, no Tor child, groups/file/call not ratcheted. 50 MB forbids those extras.
- History JSONL is still `0600` plaintext.

## Memory (measured, not invented)

| Gate | kB |
|---|---|
| Idle (phase 1 two-home) | 6648 |
| Call peak (phase 6) | ~21540 |
| Ratchet 1:1 (phase 8) | **11252** |
| Product cap | 51200 (50 MB) |

## Packages

- `toxcore` 1:0.2.22-2 extra
- `libsignal-protocol-c` 2.3.3-2 extra (2026-08-19)
- `libcrypto` (OpenSSL 3) already on the system, used only in `ratchet_adapt.c`

## Next (needs a new go)

- Live plugin on the bar
- Two real machines over internet
- Phase 7 AUR when registration is on
- Optional later: group ratchet, file/call ratchet, Tor — only if 50 MB still holds
