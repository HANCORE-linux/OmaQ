# Phase 3 — groups

**Status:** done. Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- Header probe: `docs/stages/03-toxcore.md` (private NGC, no dissolve primitive, founder-only moderator promotion)
- `group.c`: conversation id `g<n>`, peer table, dissolve = kick-down + leave
- Wire: `group.create` / `dissolve` / `member.setRole` / `member.remove` / `leave`
- `invite.create` `kind=group` (URL + `tox_group_invite_friend` when `id` is a friend); raw NGC invites stay pending until the matching group URL is redeemed
- Same chat path: `msg.send` to `g0` uses NGC; history still JSONL via `store.h`
- Panel: create group, invite last contact, dissolve
- Observer unused. Admin → admin is `forbidden` on the wire (Tox)
- `make verify-3`; `.phase` is 3

## How to check

```text
make verify-3
```

## Stays out

- No live plugin copy
- Cards / pin / sounds / themes (phase 4)
- Public Chat-ID join
