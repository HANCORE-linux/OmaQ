# Current status — 2026-08-25

This is the current product snapshot. It intentionally contains no historical phase notes or discarded ideas.

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.7.0`
- **Source:** `/home/hancore/Projects/omaq`
- **Live plugins:** `~/.config/omarchy/plugins/hancore.omaq` on machine and `/home/drdeltree/.config/omarchy/plugins/hancore.omaq` on machine2
- **Source commit:** `52262ad` is committed and pushed on `main`; protocol-7 follow-up work is currently uncommitted
- **Live machine:** uncommitted protocol-7 working tree; helper SHA-256 `6ea1c74c32a5a6d6c0ac024198368fa9f1fbd2199ae62e806a4c4c01c2053720`
- **Live machine2:** same uncommitted protocol-7 working tree at `192.168.2.108`; helper SHA-256 `6ea1c74c32a5a6d6c0ac024198368fa9f1fbd2199ae62e806a4c4c01c2053720`
- **AUR:** paused; no registration or upload
- **User guide:** [`USER-GUIDE.md`](USER-GUIDE.md)

## Working functionality

- **Pairing:** `Invite` creates a one-use, time-limited QR/link. The recipient uses `Add`; the sender explicitly accepts the request.
- **Direct chat:** floating chat window, Signal Double Ratchet, no plaintext fallback.
- **Messages:** send, request-correlated delivery failures with safe Resend, compact receipt spacing, a borderless Send action, `Show Tools`/`Hide Tools`, persisted message-only text scaling, hover reactions, hover and keyboard editing, confirmed deletion, formatting, emoji, keyboard navigation, unread badges, and `New messages` divider.
- **Files:** paused offer, explicit Accept/Decline, cancelable outgoing transfers, default downloads under `~/Downloads/omaq/`, and in-chat playback for received audio files without image or video previews.
- **Notifications:** the per-conversation action reads `Auto-off` while enabled and `Auto-open` while disabled, with the on/off state repeated in its tooltip; unread counts remain until the relevant chat is actively opened.
- **Connection state:** Panel and chat distinguish `Connecting…`/`Reconnecting…` from an online service with an offline contact. Tox uses TCP relays with UDP discovery and hole punching disabled so contacts do not learn each other's IP addresses.
- **Calls:** direct chats provide microphone/speaker audio through an interruptible PulseAudio event loop, a looping `phone.oga` tone while incoming or outgoing calls ring, explicit Answer/Decline/Hang up controls, a call timer, and a `color01` pulsing incoming-call bar icon that opens the caller's chat without answering. Ringing audio stops on answer or termination. Calls never appear in group chats.
- **Identity:** local `tox.save`, optional passphrase encryption, versioned Export bundles with private group mappings, Import, and rollback-protected Replace.
- **Groups:** named private groups are capped at 10 members. Existing contacts receive group tokens through their Signal-encrypted direct session before the native private invite is shown for explicit acceptance, including a fresh invite after removal. The panel and group-chat header provide contact invitation; group chats expose names, roles, online/offline state, role-aware member moderation, reactions, replies, edits, deletes, formatting, unread state, and receipts. Group file transfer remains unavailable because Tox NGC has no group file-transfer primitive.

## Security boundaries

- The private Tox identity is local; there is no central identity server.
- The passphrase protects the local identity file, not JSONL chat history.
- Private identities, the private-group registry, ratchet state, and local history must never be synchronized unintentionally.
- Direct call audio uses bounded in-memory PCM rings and the local PulseAudio client API; no audio is persisted.

## Open points

### Implemented locally and deployed live; pending commit

- The helper transactionally prunes unread entries that no loaded friend or active group can expose, so unavailable groups cannot keep a non-actionable widget badge alive.
- Protocol 7 makes read state helper-authoritative with an fsync-backed recovery journal, persistent bounded receipt outbox, capability-gated application acknowledgements, batched retries, restart recovery, authoritative history unread snapshots, and a bounded legacy path.
- A removed group member can receive, see, and accept a fresh targeted invitation; the recipient proactively removes an unregistered stale native group with the same stable chat ID before authorizing the reinvite.
- Group chats provide an unframed `Add member` selector, plain member rows with presence and role icons, a concise self entry (`You`), correlated invite feedback, and a confirmed `logout` action for every role.
- Chat message text has a persisted five-step `90%`–`140%` scale and a settings preview; composer, controls, receipts, and member text do not scale with it.
- The panel uses a tightly cropped transparent, high-contrast SVG connection banner, three frames around the banner, combined You/Friends/Groups area, and icon rail. The two lower frames share an aligned bottom edge. Buttons remain compact and non-bold; status shows `YOU · ONLINE`, friend counts use online/total, `color01` contact badges sit left of names, five contacts fill each left/center/right column, and a fading vertical scrollbar appears after 15 contacts.
- The helper continues to bind `Remove contact` and targeted group invitations to the selected contact's stable public key, which it rechecks immediately before destructive or transport actions.
- A process-wide owner loops the bundled `phone.oga` progress tone for incoming and outgoing ringing, stops it on answer, decline, hangup, or terminal state, and prevents duplicate playback across monitor surfaces.

### Existing validation gaps

1. A complete 1:1 test over separate networks is still missing, including presence, typing, delivery, unread badge, and the `New messages` divider.
2. Native Quickshell/Wayland end-to-end validation has not been completed.
3. `qmlcachegen Panel.qml` still fails with parser/import errors; `qmllint` can exit with code 255 without diagnostics in this environment.
4. Phase 7/AUR remains paused until registration and an explicit go.

## Latest validation

Commit `52262ad` passed the complete build/integration matrix and two independent final reviews with `0 findings` before commit and push. The current protocol-7 follow-up passed unit/IPC builds, native helper compilation, architecture and no-Signal builds, phases 2–6 and 8, Ratchet restart, asset verification, core QML lint, plugin validation, Panel runtime smokes, and two independent final reviews with `0 findings`. Both live installations passed checksum and plugin validation and restarted on protocol 7. Native Wayland interaction and the separate-network acceptance test remain open.

## Next order

1. Commit the staged protocol-7 read/reinvite/UI work only after fresh explicit approval.
2. Push only after a separate explicit approval.
3. Run the separate-network 1:1 acceptance test.
4. Complete native Quickshell/Wayland validation and investigate the QML toolchain failures.
5. Resume Phase 7/AUR only after registration and a new explicit go.
