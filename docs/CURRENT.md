# Current status — 2026-08-23

This is the current product snapshot. It intentionally contains no historical phase notes or discarded ideas.

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`, synchronized with `origin/main`
- **Manifest version:** `0.6.0`
- **Source:** `/home/hancore/Projects/omaq`
- **Live plugin:** `~/.config/omarchy/plugins/hancore.omaq`
- **AUR:** paused; no registration or upload
- **User guide:** [`USER-GUIDE.md`](USER-GUIDE.md)

## Working functionality

- **Pairing:** `Invite` creates a one-use, time-limited QR/link. The recipient uses `Add`; the sender explicitly accepts the request.
- **Direct chat:** floating chat window, Signal Double Ratchet, no plaintext fallback.
- **Messages:** send, request-correlated delivery failures with safe Resend, compact hover reactions, hover and keyboard editing, confirmed deletion, formatting, emoji, keyboard navigation, unread badges, and `New messages` divider.
- **Files:** paused offer, explicit Accept/Decline, cancelable outgoing transfers, default downloads under `~/Downloads/omaq/`, and in-chat playback for received audio files without image or video previews.
- **Notifications:** `Auto-open` is per conversation; Settings groups Mute, Demo, Theme, and licensed sound presets; unread counts remain until the relevant chat is actively opened.
- **Connection state:** Panel and chat distinguish `Connecting…`/`Reconnecting…` from an online service with an offline contact.
- **Identity:** local `tox.save`, optional passphrase encryption, Export, Import, and rollback-protected Replace.
- **Groups:** UI and basic group operations exist; role management and cross-network behavior still need acceptance testing.

## Security boundaries

- The private Tox identity is local; there is no central identity server.
- The passphrase protects the local identity file, not JSONL chat history.
- Private identities, ratchet state, and local history must never be synchronized unintentionally.
- Calls are excluded from the user guide and are not product-ready; current support is signaling-level only.

## Open points

1. A complete 1:1 test over separate networks is still missing, including presence, typing, delivery, unread badge, and the `New messages` divider.
2. Native Quickshell/Wayland end-to-end validation has not been completed.
3. `qmlcachegen Panel.qml` still fails with parser/import errors; `qmllint` can exit with code 255 without diagnostics in this environment.
4. Phase 7/AUR remains paused until registration and an explicit go.

## Latest validation

The current code has passed the local helper, architecture, phase-8, two-client, test, and diff checks. Native Wayland UI validation and the separate-network test remain open.

## Next order

1. Run the separate-network 1:1 acceptance test.
2. Complete native Quickshell/Wayland validation.
3. Investigate the QML toolchain failures.
4. Resume Phase 7/AUR only after registration and a new explicit go.
