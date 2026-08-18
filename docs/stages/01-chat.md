# Phase 1 — 1:1 chat

**Status:** offline green. Tox adapter is in tree but **not linked** — `toxcore` is not installed (needs `omarchy pkg add toxcore` on the machine).  
**Date:** 2026-08-18

## Landed

- Offline (verified): invite, roles, conversation, json_io, store, message, lock election, `omarchy plugin validate`
- `helper/tox_adapt.c` + `identity.c` behind `HAVE_TOX` (empty object without pkg-config)
- `Service.qml` + `Panel.qml` (badge stub, exec helper)
- `tests/two-homes.sh` — invite + one text + singleton check (needs linked tox)
- `THIRD_PARTY.md` names official extra `toxcore`

## How to check

```text
make verify-1-offline   # green now
omarchy pkg add toxcore # owner, sudo
make clean helper
make verify-1-tox       # two homes, one ping
```

## Stays out

- No live copy under `~/.config/omarchy/plugins/`
- No AUR upload
- Chat UI beyond badge stub
- `verify-1-tox` is **not** green until the package is installed
