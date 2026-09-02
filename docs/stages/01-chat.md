# Phase 1 — 1:1 chat

**Status:** done (offline + tox). Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- Offline modules + gold + lock election + `omarchy plugin validate`
- `toxcore` 1:0.2.22-2 (Arch extra), helper built with `-DHAVE_TOX`
- `tests/two-homes.sh`: invite + accept + one `ping` between two temp identities
- Singleton: lock owner binds `$OMAQ_STATE/omaq.sock` (0600); second starter exits 2
- `tests/two-clients.sh`: two Unix-socket clients, one helper, fan-out status
- Token-gated friend request; `invite.revoke` clears the token; accept consumes it
- JSON-escape on events and JSONL; history emits `items` (last 50, including `messages.jsonl.1`)
- Atomic `tox.save.tmp` + fsync + rename; compiled-in public bootstrap + TCP relays
- `Service.qml`: always exec; exit 2 → Socket; other death → 200ms/1s/5s backoff (cap 30s)
- Sanitizers on `tests/omaq_test` only
- `Service.qml` / `Panel.qml` stub (badge)

## How to check

```text
make verify-1-offline
make verify-1-tox
```

## Measured

- Helper idle RSS (process A during two-homes, no ASan): **6648 kB**
- This is the historical phase-1 measurement, not the current gate. The current single-helper limit is 51,200 kB in [`../PLAN.md`](../PLAN.md).
- The earlier 29,048 kB figure used an ASan-linked helper, not the packaged shape.

## Stays out

- No copy to `~/.config/omarchy/plugins/`
- No AUR upload
- Chat UI beyond the badge stub (list/chat pages still later)
- Phase 2 (QR PNG, nospam UI, safety code)
