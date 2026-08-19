# Current stand — 2026-08-19

This file is the snapshot for a new session. Product contract (German): [`../../Prompt-Uebergabe/OmaQ.md`](../../Prompt-Uebergabe/OmaQ.md). How we build: [`PLAN.md`](PLAN.md).

**Commit:** `dd403e3` on `main` (private `HANCORE-linux/OmaQ`). Reviewer + verifier 2026-08-19 on `852ba90`; confirmed bugs 1–8 and gate holes 9–11, 15 are fixed in this tree.  
**`.phase`:** 8  
**Plugin id:** `hancore.omaq`  
**Live bar:** off. Do not write `~/.config/omarchy/plugins/` except one announced install. No silent `omarchy restart shell`.  
**AUR:** registration off — no `verify-7`, no upload.

Chat with the owner is German. Repo, UI, and this file are English.

## Phases

| Phase | What | Verify | Status |
|---|---|---|---|
| 0 Harness | Makefile, gold driver, licenses | `verify-0` | done |
| 1 1:1 chat | one helper, Tox, invite | `verify-1` | done |
| 2 Invite safety | QR, revoke, nospam, safety, rate | `verify-2` | done |
| 3 Groups | owner > admin > member | `verify-3` | done |
| 4 Surfaces | cards, pin, sounds, themes | `verify-4` | done |
| 5 Daily | export/import, search | `verify-5` | done |
| 6 File + 1:1 audio | Tox file 8 MiB, ToxAV audio | `verify-6` | done |
| 7 AUR package | PKGBUILD, namcap, enable path | `verify-7` | **halted** |
| 8 Double Ratchet | Signal payload on 1:1, 50 MB cap | `verify-8` | **done** |

## What works (tested in temp homes)

- Invite link + QR token, one-use, 24 h, revoke, nospam voids invites
- 1:1 and group text (same chat page)
- Optional `toxencryptsave` lock on `tox.save`
- File send (paused until accept, dest `$OMAQ_HOME/files/<conv>/`)
- 1:1 call **signaling** (start/answer/stop). No microphone PCM yet.
- Direct messages: Double Ratchet over Tox (`OQB1` / `OQR1`, invite `rk=`)
- Two local helpers exchange a ratchet plaintext; `make verify-8` green

## Security (honest)

| Protected | Not this product |
|---|---|
| Tox E2E on the wire; relays do not read content | No SimpleX-style missing user id |
| Direct text: Signal Double Ratchet; `rk=` in the QR | Groups / files / calls not ratcheted |
| `tox.save` optional passphrase (`toxencryptsave`) | History JSONL is `0600` plaintext |
| Token + rate limit on a leaked QR | One durable Tox address; friends can see IP |
| 50 MB RSS cap, measured | No Tor child (would blow the cap) |

KCI on the Tox handshake does not decrypt ratchet text once `rk` came from the invite. That is not “as metadata-private as SimpleX”.

## Memory (measured)

| Gate | kB |
|---|---|
| Idle (two-home, with ratchet) | 11216 |
| Call peak | 26032 |
| Ratchet 1:1 (`phase8`) | **11324** |
| Product cap | **51200** (50 MB) |

## Packages (Arch extra, owner-approved)

- `toxcore` 1:0.2.22-2
- `libsignal-protocol-c` 2.3.3-2 (2026-08-19)
- `libcrypto` (OpenSSL 3) already on the box, only in `ratchet_adapt.c`

## Next — only after an explicit go

1. Live plugin on the bar
2. Two real machines over the internet
3. Phase 7 AUR when registration is on
4. Later extras (group ratchet, file/call ratchet, Tor) only if RSS still ≤ 50 MB
