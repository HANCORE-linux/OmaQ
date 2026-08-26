# Current status — 2026-08-25

This is the current product snapshot. It intentionally contains no historical phase notes or discarded ideas.

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.7.0`
- **Source:** `/home/hancore/Projects/omaq`
- **Live plugins:** `~/.config/omarchy/plugins/hancore.omaq` on machine and `/home/drdeltree/.config/omarchy/plugins/hancore.omaq` on machine2
- **Source commit:** `2394692` is committed, pushed, and live on both machines; the fixed-header, group-presence, and receipt-display follow-up is currently uncommitted
- **Live machine:** `2394692`; helper SHA-256 `8649f86d65913d7232901319c4046ebf9d3881396a78dd546d10ac707c88af8e`
- **Live machine2:** `2394692` at `192.168.2.108`; helper SHA-256 `8649f86d65913d7232901319c4046ebf9d3881396a78dd546d10ac707c88af8e`
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
- **Identity:** local `tox.save`, optional passphrase encryption with an 8-character minimum for new passphrases, file-picked version-2 Export bundles with private group and friend-binding mappings, non-destructive staged Import validation, and separately confirmed rollback-protected Replace. Version-1 bundles remain importable. Identity outcomes are request-correlated and passphrases are never queued while the helper is unavailable.
- **Groups:** named private groups are capped at 10 members. Existing contacts receive group tokens through their Signal-encrypted direct session before the native private invite is shown for explicit acceptance, including a fresh invite after removal. The panel and group-chat header provide contact invitation; group chats expose complete sender names, persisted join/leave notices, names, roles, online/offline state, role-aware member moderation, reactions, replies, edits, deletes, formatting, unread state, and receipts. Group file transfer remains unavailable because Tox NGC has no group file-transfer primitive.

## Security boundaries

- The private Tox identity is local; there is no central identity server.
- The passphrase protects the local identity file, not JSONL chat history.
- Private identities, the private-group registry, ratchet state, and local history must never be synchronized unintentionally.
- Direct call audio uses bounded in-memory PCM rings and the local PulseAudio client API; no audio is persisted.

## Open points

### Committed and deployed in `ca109a9`

- The helper transactionally prunes unread entries that no loaded friend or active group can expose, so unavailable groups cannot keep a non-actionable widget badge alive.
- Protocol 7 makes read state helper-authoritative with an fsync-backed recovery journal, persistent bounded receipt outbox, capability-gated application acknowledgements, batched retries, restart recovery, authoritative history unread snapshots, and a bounded legacy path.
- A removed group member can receive, see, and accept a fresh targeted invitation; the recipient proactively removes an unregistered stale native group with the same stable chat ID before authorizing the reinvite.
- Group chats provide an unframed `Add member` selector, plain member rows with presence and role icons, a concise self entry (`You`), correlated invite feedback, and a confirmed `logout` action for every role.
- Chat message text has a persisted five-step `90%`–`140%` scale and a settings preview; composer, controls, receipts, and member text do not scale with it.
- The panel uses a fixed self header with avatar, nickname, `YOU · <STATE>`, and compact pending-request controls. A fixed support frame above the right rail links the supplied HANCORE icon to GitHub and a Ko-fi icon to `https://ko-fi.com/hancore`. Friends/Groups move to the top of the lower-left frame; its base bottom aligns with the fixed icon rail. Buttons remain compact and non-bold, friend counts use online/total, five contacts fill each left/center/right column, names use `color03` on hover/focus, and a fading vertical scrollbar appears after 15 contacts.
- The helper continues to bind `Remove contact` and targeted group invitations to the selected contact's stable public key, which it rechecks immediately before destructive or transport actions.
- A process-wide owner loops the bundled `phone.oga` progress tone for incoming and outgoing ringing, stops it on answer, decline, hangup, or terminal state, and prevents duplicate playback across monitor surfaces.

### Implemented locally; pending deployment

- Normal file cancellation is projected to both peers as `file.canceled`, with a persistent dismissible status instead of a silent clear or generic failure. Correlated group-invite success remains visible as sent and waiting for acceptance.
- Direct history, avatars, Ratchet pins, Signal identities, Signal sessions, unread counts, and receipt debt use the contact's canonical Tox public key instead of the unstable numeric friend handle. A versioned private binding file authorizes legacy migration before Ratchet startup and after unlock; unbound numeric state is archived with a reinvite warning, while collisions, malformed input, oversized friend lists, and symlinked paths fail closed.
- The uninstall wrapper unloads OmaQ without deleting user data, explicitly lists every retained location including `~/Downloads/omaq/`, and explains that each directory can be inspected and irreversibly removed manually later.
- The fixed self/request and support frames use the former compact 48-pixel height. The transparent HANCORE GitHub image and monochrome Ko-fi glyph have no per-icon button border, change to `color03` on hover or keyboard focus, and match the cell size, glyph scale, and spacing of the rail below. Every rail selection, confirmation, and submenu remains enclosed by the fixed thin lower-left frame.
- Protocol 8 gates the new Identity and group-binding contracts so a protocol-7 helper cannot accept unsupported actions from the updated UI. Identity uses an aligned two-column action grid, native file pickers, request-correlated outcomes, immediate-only handling for passphrase-bearing IPC, non-destructive staged Import validation, separately confirmed Replace, and an 8-character minimum for new passphrases.
- Unread contacts use a `color03` underline beneath the name instead of a numeric badge. New self nicknames are limited to 18 valid characters, while every displayed name remains safely elided inside its column. Nickname submissions use immediate, request-correlated outcomes so timeout retries cannot be completed by stale responses.
- Rail menus keep the fixed header, support frame, and right icon rail stationary. The panel extends only to the active menu's natural height when it fits the screen; only the lower-left menu area scrolls under a screen-height constraint, and keyboard-focused controls remain visible. Shared token buttons expose accessible button names/actions.
- GroupChat bubbles reserve enough width to show the complete cached sender name, and helper-persisted system rows identify members who join or leave. Member entries use full-size, crisply rendered role icons and middle-dot separators. Mouse focus no longer leaves the group-members tooltip persistent. An invite-secret proof binds each group-specific peer key to the invited contact's stable direct key, so Add member lists only absent contacts and the helper rejects duplicate-member invite races while still allowing Remove → Reinvite. A durable pre-accept transaction removes a group after a crash unless the member-binding proof debt was committed; committed proof debts survive offline periods until acknowledged and can be reconstructed from a fresh authenticated member descriptor plus group-key possession proof. The `OMAQGF1` friend-binding sidecar rejects malformed, duplicate, unknown, or over-limit group mappings while permitting explicitly pruned orphan cleanup. Group removal uses a recoverable two-file transaction so `groups.tsv` and `group-friends.tsv` cannot remain split after a crash.
- Group-name and Search inputs now use the same full width and standard field height as the Identity bundle-path input, with their actions below. Search results and safety-code display/copy remain bound to the explicitly user-selected conversation, unaffected by background messages or delayed helper responses. The Invite view shows the helper-issued 24-hour expiry as a live countdown and provides confirmed Revoke and sequential Revoke → New link actions. Receipt projection is monotonic, so delayed or replayed `delivered` events cannot downgrade a message already shown as `Read`. Sound choices use a uniform three-column grid with complete labels, and Show safety code has a short identity-verification explanation.

### Existing validation gaps

1. A complete 1:1 test over separate networks is still missing, including presence, typing, delivery, unread badge, and the `New messages` divider.
2. Native Quickshell/Wayland end-to-end validation has not been completed.
3. `qmlcachegen Panel.qml` still fails with parser/import errors; `qmllint` can exit with code 255 without diagnostics in this environment.
4. Phase 7/AUR remains paused until registration and an explicit go.

## Latest validation

Commit `a8a5af5` passed the full unit/IPC, helper, architecture, lock/multi-client/multi-home, phase 2–6 and 8, EncryptSave, Ratchet-restart, no-Signal, core-QML, plugin, syntax, and focused Wayland matrix. The local live preview includes `a8a5af5`; Machine2 remains on `8e36ab6`, and both deployed helpers run protocol 8. The current direct-state/file-status/uninstall follow-up passes the same full matrix plus stable-key migration, receipt restart, removal/add journal, malformed-state, cancellation-on-both-peers, per-chat dismissal, helper-restart, persistent-warning, and uninstall-wrapper regressions. Iterative independent AppSec and QML reviews drove all reported findings to fixes; the final confirmation rerun is temporarily blocked by the external reviewer quota and remains a delivery gate. Native separate-network acceptance remains open; `qmllint Panel.qml` still exits 255 without diagnostics.

## Next order

1. Commit, push, and deploy only after fresh approvals for each delivery phase.
2. Run the separate-network 1:1 acceptance test.
3. Investigate the remaining Panel QML toolchain failure.
4. Resume Phase 7/AUR only after registration and a new explicit go.
