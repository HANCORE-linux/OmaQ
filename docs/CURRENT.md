# Current status — 2026-08-25

This is the current product snapshot. It intentionally contains no historical phase notes or discarded ideas.

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.6.0`
- **Source:** `/home/hancore/Projects/omaq`
- **Live plugins:** `~/.config/omarchy/plugins/hancore.omaq` on machine and `/home/drdeltree/.config/omarchy/plugins/hancore.omaq` on machine2
- **Live runtime commit:** `db74f08` on both machines; helper SHA-256 `dc5ca90ab9848e6d7075659398c5bb3a83b6a251bfdcb8bf3c811da94fb68d71`
- **AUR:** paused; no registration or upload
- **User guide:** [`USER-GUIDE.md`](USER-GUIDE.md)

## Working functionality

- **Pairing:** `Invite` creates a one-use, time-limited QR/link. The recipient uses `Add`; the sender explicitly accepts the request.
- **Direct chat:** floating chat window, Signal Double Ratchet, no plaintext fallback.
- **Messages:** send, request-correlated delivery failures with safe Resend, compact receipt spacing, a borderless Send action, `Show Tools`/`Hide Tools`, persisted message-only text scaling, hover reactions, hover and keyboard editing, confirmed deletion, formatting, emoji, keyboard navigation, unread badges, and `New messages` divider.
- **Files:** paused offer, explicit Accept/Decline, cancelable outgoing transfers, default downloads under `~/Downloads/omaq/`, and in-chat playback for received audio files without image or video previews.
- **Notifications:** the per-conversation action reads `Auto-off` while enabled and `Auto-open` while disabled, with the on/off state repeated in its tooltip; the framed panel uses a compact two-column icon rail, slim theme-radius buttons, green/gray friend status dots, and a directly openable active-group list with member counts; unread counts remain until the relevant chat is actively opened.
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

### Implemented locally; pending live deployment

- The helper transactionally prunes unread entries that no loaded friend or active group can expose, so unavailable groups cannot keep a non-actionable widget badge alive.
- A removed group member can receive, see, and accept a fresh targeted invitation; the integration test covers removal and rejoin.
- Group chats provide an `Add member` action with contact selection and correlated invite feedback. Member/admin controls use the composer text size.
- Chat message text has a persisted five-step `85%`–`120%` scale; composer, controls, and member text do not scale with it. The font-size rail action replaces Identity's old position, and Identity now sits directly below Danger zone.
- The panel uses green online dots, stepped gray AFK/offline dots, slimmer non-bold universal buttons, theme-token radii, a corrected active Danger icon, and thin frames around the logo, Friends/Groups area, and both icon columns.
- Protocol 6 binds `Remove contact` and targeted group invitations to the selected contact's stable public key, which the helper rechecks immediately before destructive or transport actions. The bar icon has tighter optical spacing, and new-message badges use palette `color01`.
- A process-wide owner loops the bundled `phone.oga` progress tone for incoming and outgoing ringing, stops it on answer, decline, hangup, or terminal state, and prevents duplicate playback across monitor surfaces.

### Existing validation gaps

1. A complete 1:1 test over separate networks is still missing, including presence, typing, delivery, unread badge, and the `New messages` divider.
2. Native Quickshell/Wayland end-to-end validation has not been completed.
3. `qmlcachegen Panel.qml` still fails with parser/import errors; `qmllint` can exit with code 255 without diagnostics in this environment.
4. Phase 7/AUR remains paused until registration and an explicit go.

## Latest validation

The live runtime passed helper/unit tests, architecture checks, no-Signal enforcement, phases 2, 3, 5, 6, and 8, Ratchet restart, core QML lint, plugin validation, Panel/Chat runtime smokes, deterministic bidirectional call-audio transport, remote hangup, and a final review with `0 findings`. Both deployed helpers still run protocol 5 with identical binaries. The pending protocol-6 source has passed the same build/unit baseline, phases 2, 3, 4, 5, 6, and 8, Ratchet restart, asset integrity, core QML lint, plugin validation, Panel/Group/Chat runtime smokes, ringtone lifecycle checks, stale-key rejection, stale-unread pruning, remove/reinvite/rejoin coverage, and two independent final reviews with `0 findings`. Native Wayland interaction and the separate-network acceptance test remain open.

## Next order

1. Perform live visual acceptance of the panel, group-chat invitation flow, message scaling, ringtone lifecycle, and stale-badge cleanup on both machines.
2. Run the separate-network 1:1 acceptance test.
3. Complete native Quickshell/Wayland validation and investigate the QML toolchain failures.
4. Resume Phase 7/AUR only after registration and a new explicit go.
