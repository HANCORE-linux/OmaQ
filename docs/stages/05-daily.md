# Phase 5 — daily

**Status:** done. Live plugin is not on the bar.  
**Date:** 2026-08-19

## Landed

- `identity.export` writes `tox.save` (0600, tmp+rename)
- `identity.import` refuses if `tox.save` exists (`identity_exists`) unless `replace:true`
- `search` scans the open conversation's JSONL (case-insensitive, last 20 hits)
- Panel: search field, export button
- `make verify-5`; `.phase` is 5

## How to check

```text
make verify-5
```

## Stays out

- No bar install
- File send and voice (phase 6)
