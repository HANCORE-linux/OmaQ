# toxcore NGC facts (phase 3 step 0)

Read from `/usr/include/tox/tox.h` (`toxcore` 1:0.2.22-2). These are facts, not product wishes.

## Roles toxcore actually has

| Tox | OmaQ | Privileges in the header |
|---|---|---|
| `FOUNDER` | owner | Kick anyone; set any role except founder; password, privacy, peer limit |
| `MODERATOR` | admin | Kick / set **user or observer** on peers **below**; may set topic. **May not** set other moderators or the founder. **May not** promote to moderator |
| `USER` | member | Speak |
| `OBSERVER` | unused in 0.x | Observe only |

Header quote (`tox_group_set_role`): “Only Founders may promote peers to the Moderator role.”

OmaQ policy (`roles.c`) still allows admin → admin. On the wire that call fails. `group.c` checks `role_may` first, then `tox_group_set_role`; a Tox permission miss is `forbidden`. We do not invent a second promotion protocol.

## Invite / join

- **Private** group (`TOX_GROUP_PRIVACY_STATE_PRIVATE`): join only via `tox_group_invite_friend` to an **existing friend**. No DHT directory. Chat ID join (`tox_group_join`) is the public path; OmaQ does not use it.
- Invite is valid only while the inviter is still in the group.
- Accept: `tox_group_invite_accept` (needs the invite blob from `group_invite`).
- OmaQ `omaq://…&k=group` is our token layer. After 1:1 exists, the helper must also call `tox_group_invite_friend`. `r=admin` is a **promotion after join** (member window), not a Tox invite role.

## Kick / leave

- `tox_group_kick_peer`: founder or moderator; does not fire `group_peer_exit` for the caller.
- `tox_group_leave`: deletes local group state, keys, roles. Not a network-wide wipe.

## Dissolve — not a primitive

There is no `tox_group_dissolve`. OmaQ dissolve = kick every peer `role_may` allows + `leave` + mark the conversation dissolved. The NGC group may linger for others. We do not promise it is gone.

## Peer list

No `tox_group_get_peer_list`. Peers are tracked from `group_peer_join` / `group_peer_exit` (gap in `group.c`).

## What `group.c` implements

Only the gap: conversation id `g<n>`, dissolve plan, peer table, OmaQ role_may before each Tox call, `r=admin` as post-join `setRole`. No second role engine. Observer never set.
