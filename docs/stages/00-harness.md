# Phase 0 — harness

**Status:** done (`make verify-0` exit 0)  
**Date:** 2026-08-18  
**toxcore:** not installed, not used

## Landed

- Root `Makefile`: `verify-0`, `arch`, sanitizer-built C driver
- `tests/omaq_test.c` — runs, 0 gold cases (allowed)
- `scripts/arch-check.sh` — architecture greps; no helper sources yet so checks are vacant-pass
- `packaging/PKGBUILD` stub (`bash -n` clean, empty `source`, no network)
- `LICENSE.MIT`, `LICENSE.GPL-3`, `THIRD_PARTY.md` (no compiled deps)
- `.phase` = `0`

## How to check

```text
make verify-0
```

Must print `verify-0: ok`.

## Stays out

- No `invite.c`, no helper binary, no QML, no `toxcore`
- No write to `~/.config/omarchy/plugins/`
- No AUR upload
- Phase 1 offline is the next go
