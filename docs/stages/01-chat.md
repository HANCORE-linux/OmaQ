# Phase 1 — 1:1 chat

**Status:** offline done. Tox not enabled (needs a separate go + `toxcore` package).  
**Date:** 2026-08-18

## Landed

- Pure modules: `invite.c`, `roles.c`, `conversation.c`
- IO: `json_io.c` (closed scanner), `store.c` / `store.h`, `message.c` via store
- Gold: invite grammar (direct/group/errors), roles matrix, JSON shape, mutation corpus
- Lock election: `helper/omaq --hold`, second process exit 2 (`tests/lock-elect.sh`)
- `manifest.json` v0.1.0 + `Panel.qml` stub; `omarchy plugin validate .` green
- `Model.js` UX parse only (not used to decide redeem)
- `make arch` green (include/IO greps)
- Tests built with `-fsanitize=address,undefined -Wall -Werror`

## How to check

```text
make verify-1-offline
```

`make verify-1` runs offline and prints that tox is not enabled.

## Stays out

- No `toxcore`, no `tox_adapt.c`, no live plugin copy
- No QML list/chat/badge beyond the stub `Panel.qml`
- Group redeem still `unsupported`
- Identity export/import still `unsupported`
- Next go: `toxcore` approval, then singleton helper on the wire + real 1:1 send
