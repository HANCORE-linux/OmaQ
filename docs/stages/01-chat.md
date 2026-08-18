# Phase 1 — 1:1 chat

**Status:** done (offline + tox). Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- Offline modules + gold + lock election + `omarchy plugin validate`
- `toxcore` 1:0.2.22-2 (Arch extra), helper built with `-DHAVE_TOX`
- `tests/two-homes.sh`: invite + accept + one `ping` between two temp identities
- Singleton: third helper on the same home exits 2
- `Service.qml` / `Panel.qml` stub (badge)

## How to check

```text
make verify-1-offline
make verify-1-tox
```

## Measured

- Helper idle RSS (process A during two-homes): **29048 kB** (~28 MB)
- That is the phase-1 baseline. Later idle gate: > 1.5× this value fails.
- The old 20 MB figure was a target, not this measurement.

## Stays out

- No copy to `~/.config/omarchy/plugins/`
- No AUR upload
- Chat UI beyond the badge stub (list/chat pages still later)
- Phase 2 (QR PNG, nospam UI, safety code)
