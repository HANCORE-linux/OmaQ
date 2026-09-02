# Product hardening after the phased plan

**Status:** merged and deployed before the Protocol 11 through 14 follow-up

This note records completed product and security work that landed after the numbered phase notes. It is historical context, not the live product snapshot.

## Messaging and state

- The helper prunes unread entries that no loaded friend or active group can expose.
- Protocol 7 made read state helper-authoritative with a recovery journal, persistent receipt outbox, capability-gated acknowledgements, bounded retries, restart recovery, and authoritative history snapshots.
- Direct history, avatars, Ratchet pins, Signal identities and sessions, unread counts, and receipt debt use the contact's canonical Tox public key instead of its temporary friend number.
- A private binding file authorizes legacy numeric-state migration. Unbound state is archived with a reinvite warning; malformed input, collisions, oversized friend lists, and symlinked paths fail closed.
- One-time prekeys are peer-bound, persisted before publication, durably consumed, and replay-safe. Canonical `OQB2` requests and responses recover lost prekeys and half-open sessions.
- Normal file cancellation reaches both peers as `file.canceled`. Incoming and legacy avatars use exclusive no-follow staging, bounded full decoding, canonical PNG output, atomic installation, and crash cleanup.
- The helper binds contact removal and targeted group invitations to the selected contact's stable public key and rechecks it before the operation.

## Groups and identity

- A removed group member can accept a fresh targeted invitation after stale native group cleanup.
- Group chats added member selection, role and presence rows, correlated invitation feedback, confirmed leave, complete sender names, and persisted join and leave notices.
- An invite-secret proof binds a group peer key to the invited direct contact. Durable pre-accept and proof-debt records recover interrupted acceptance without authorizing an unproven member.
- `group-friends.tsv` rejects malformed, duplicate, unknown, and over-limit mappings. Group removal uses a recoverable transaction for `groups.tsv` and `group-friends.tsv`.
- Identity actions use one aligned grid with non-destructive **Validate bundle** and separately confirmed **Import identity**. Passphrase-bearing requests are immediate and never queued while the helper is unavailable.

## Interface and surfaces

- The fixed panel header shows either self state or one pending request. A fixed support frame and action rail remain stationary while lower-left menus expand or scroll.
- Friend names use an underline for unread state, submitted nicknames are limited to 18 valid characters, and delayed nickname outcomes cannot complete newer edits.
- Rail menus retain keyboard focus visibility. Shared token buttons expose accessible names and actions.
- Group bubbles reserve room for complete sender names. Member rows use clear role icons, presence indicators, and bounded text.
- Message search belongs to each chat page. Safety-code display remains tied to the selected direct contact and ignores delayed or mismatched responses.
- The Invite view shows the helper-issued expiry and provides confirmed Revoke and sequential Revoke then New link actions.
- Receipt projection is monotonic. Sound choices use a uniform grid with complete labels.
- One `SurfaceCoordinator` owns chat, demo, notification, and Hyprland-rule surfaces. First mapping may float a chat, while focus, reopen, and config reload preserve manual tiling.

## Removal boundary

The uninstall wrapper binds the helper PID, UID, start time, executable inode, socket, and instance. It requires a correlated group-free shutdown acknowledgement and holds the helper state lock during the exact Omarchy removal command. Active or uncertain groups, overlapping startup, incomplete markers, replaced runtime objects, unsupported safe shutdown, and malformed or missing acknowledgements fail closed without a signal. User data, downloads, deployment backups, dependencies, and any Omarchy plugin backup remain available for inspection.
