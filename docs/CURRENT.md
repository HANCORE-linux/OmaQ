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
- **Messages:** send, request-correlated delivery failures with safe Resend, compact receipt spacing, a borderless Send action and `Show Tools`/`Hide Tools`, hover reactions, hover and keyboard editing, confirmed deletion, formatting, emoji, keyboard navigation, unread badges, and `New messages` divider.
- **Files:** paused offer, explicit Accept/Decline, cancelable outgoing transfers, default downloads under `~/Downloads/omaq/`, and in-chat playback for received audio files without image or video previews.
- **Notifications:** the per-conversation action reads `Auto-off` while enabled and `Auto-open` while disabled, with the on/off state repeated in its tooltip; the panel uses a compact two-column icon rail, uniform larger action buttons, status dots beside friends, and a directly openable active-group list with member counts; unread counts remain until the relevant chat is actively opened.
- **Connection state:** Panel and chat distinguish `Connecting…`/`Reconnecting…` from an online service with an offline contact. Tox uses TCP relays with UDP discovery and hole punching disabled so contacts do not learn each other's IP addresses.
- **Calls:** direct chats provide microphone/speaker audio through an interruptible PulseAudio event loop, explicit Answer/Decline/Hang up controls, a call timer, and a `color01` pulsing incoming-call bar icon that opens the caller's chat without answering. Calls never appear in group chats.
- **Identity:** local `tox.save`, optional passphrase encryption, versioned Export bundles with private group mappings, Import, and rollback-protected Replace.
- **Groups:** named private groups are capped at 10 members. Existing contacts receive group tokens through their Signal-encrypted direct session before the native private invite is shown for explicit acceptance. The panel provides group selection, creation, contact invitation, opening, and confirmed leave. Group chats expose names, roles, online/offline state, role-aware member moderation, reactions, replies, edits, deletes, formatting, unread state, and receipts. Group file transfer remains unavailable because Tox NGC has no group file-transfer primitive.

## Security boundaries

- The private Tox identity is local; there is no central identity server.
- The passphrase protects the local identity file, not JSONL chat history.
- Private identities, the private-group registry, ratchet state, and local history must never be synchronized unintentionally.
- Direct call audio uses bounded in-memory PCM rings and the local PulseAudio client API; no audio is persisted.

## Open points

### Known runtime issue

- The widget on machine currently shows an unread count of `1` even though the active-group projection is empty. The persisted unread entry belongs to an unavailable local group, so no listed conversation can clear it. Prune non-actionable unread entries without exposing the private group identifier.

### Requested follow-up

1. Use a green round status marker for online friends and stepped gray markers for offline states.
2. Replace the current oversized, heavy panel buttons with one slimmer universal size that preserves every label without overlap.
3. Bind panel button radii to the active theme tokens.
4. Allow a removed group member to receive and see a fresh invitation when invited again.
5. Add an `Add member` icon to the group-chat header so contacts can be invited without returning to the panel.
6. Match group member/admin control and text sizing to the message composer.
7. Put one thin shared frame around both columns of the right-side panel icon rail.
8. Put a thin frame around the Friends/Groups list area.
9. Frame the OmaQ logo area and use equal spacing between the logo, list, and icon groups without wasting space.
10. Correct the Danger zone icon rendering in its selected/active state.
11. Make `Remove contact` require an explicit contact selection followed by a confirmation bound to that contact.
12. Audit and reduce excessive horizontal padding around the bar widget icon without harming its badge, focus target, or alignment.
13. Render the widget's new-message notification badge with palette `color01`.

### Existing validation gaps

1. A complete 1:1 test over separate networks is still missing, including presence, typing, delivery, unread badge, and the `New messages` divider.
2. Native Quickshell/Wayland end-to-end validation has not been completed.
3. `qmlcachegen Panel.qml` still fails with parser/import errors; `qmllint` can exit with code 255 without diagnostics in this environment.
4. Phase 7/AUR remains paused until registration and an explicit go.

## Latest validation

The current runtime passed helper/unit tests, architecture checks, no-Signal enforcement, phases 2, 3, 5, 6, and 8, Ratchet restart, core QML lint, plugin validation, Panel/Chat runtime smokes, deterministic bidirectional call-audio transport, remote hangup, and a final review with `0 findings`. Both live helpers run protocol 5 with identical binaries. Native Wayland interaction and the separate-network acceptance test remain open.

## Next order

1. Fix the stale, non-actionable widget unread badge and complete the requested panel refinements.
2. Fix and regress-test removed-member group reinvitation.
3. Add and validate the group-chat member invitation and sizing controls.
4. Run the separate-network 1:1 acceptance test.
5. Complete native Quickshell/Wayland validation and investigate the QML toolchain failures.
6. Resume Phase 7/AUR only after registration and a new explicit go.
