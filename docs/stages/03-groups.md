# Phase 3 — groups

**Status:** done through Protocol 12; Protocol-13 restart/parity remediation is under validation.
**Date:** 2026-08-28

## Landed

- Header probe: `docs/stages/03-toxcore.md` (private NGC, no dissolve primitive, founder-only moderator promotion)
- Stable conversation IDs use the Tox group chat ID as `g:<64-lowercase-hex>`; process-local group numbers never cross IPC or persistence boundaries
- Helper-authoritative group cache, private `groups.tsv` registry, member table, roles, online state, and a maximum of 10 members
- Wire operations: `group.create`, `group.dissolve`, `group.leave`, `group.member.setRole`, and `group.member.remove`
- Group invites are member-only and require a matching group URL; promotion is a separate owner action using the member's stable 64-hex public key
- Group messages use the normal chat history, replies, edits, deletes, reactions, unread counts, per-member receipts, and stable-member typing presentation
- Protocol-13 group-list snapshots bind helper instance, request, generation, and exact group/member counts; incomplete snapshots retain the last good projection and retry
- Calls remain direct-chat-only. Protocol 12 adds explicit-accept group files and images through bounded lossless private NGC packets because NGC has no native file primitive.
- The panel supports group creation, selection, member inspection, inviting a selected contact, role changes, removal, leave, and founder dissolve
- Dissolve removes peers allowed by the Tox role rules and then leaves; no native Tox dissolve primitive exists
- Private group mappings are included in versioned identity export bundles and reconciled against the imported Tox saved state; a missing private membership is reported and pruned, never recreated through public Chat-ID join

## How to check

```text
make verify-3
```

## Stays out

- No unapproved live plugin copy of the Protocol-13 follow-up
- Public Chat-ID join
- Group calls
