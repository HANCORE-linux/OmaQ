# Current status: 2026-09-02

This page is the current product and release snapshot. Completed phase and follow-up history lives in the [stage notes](stages/README.md).

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.8.1-beta.1`, Protocol 14
- **Live plugins:** both installations are clean Git-managed checkouts at `580c69cf7583ccca4461bd265334edc0a692b65d`
- **Live helper:** Protocol 14, SHA-256 `ee43637be9ac9880bb465408a87c8ace94015c217a1df25dc79ce088308a1fba`
- **AUR:** paused; no registration or upload
- **Documentation:** the task-based [documentation index](README.md) links the illustrated guide, security model, installation lifecycle, and historical notes

## Working functionality

- **Pairing:** **Invite** creates a one-use 24-hour QR code and link. The recipient uses **Add contact**, and the sender explicitly accepts the request. Safety codes support identity comparison through another trusted channel.
- **Direct chat:** Direct messages add the Signal Double Ratchet to Tox transport and never fall back to plaintext. Each chat owns its window, saved size, placement, search state, unread state, history, files, and preferences.
- **Messages:** Enter sends, modified Enter inserts a line break, and delivery failures distinguish safe Resend from an unknown result. Chats support formatting, arbitrary emoji, reactions, inline Reply, editing, confirmed deletion, exact text selection and Copy, message scaling, keyboard navigation, local history timestamps, per-chat search, receipts, unread badges, and a **New messages** divider.
- **Files and images:** incoming transfers remain paused until acceptance and default to `~/Downloads/omaq/`. Outgoing transfers can be canceled. Received audio supports playback, while validated PNG, JPEG, and WebP images use a 56×56 preview that opens the complete local file. DirectChat and GroupChat share the non-call attachment workflow; video remains a normal download.
- **Groups:** private groups support up to 10 members, owner, admin, and member roles, invitation of existing contacts, moderation, complete sender names, join and leave notices, typing, reactions, replies, edits, deletes, receipts, files, and images. Calls remain unavailable in GroupChat.
- **Calls:** DirectChat provides audio calls through PulseAudio with Answer, Decline, and Hang up actions, a call timer, one process-wide ringing tone, and a pulsing incoming-call bar icon. Audio stays in bounded memory and is not recorded.
- **Identity:** `tox.save` remains local and can use passphrase protection. Export bundles include Tox savedata and private group mappings but exclude Ratchet sessions, local history, avatars, receipts, and files. **Validate bundle** does not activate a bundle; **Import identity** is separately confirmed and rollback-protected.
- **Notifications and appearance:** the fixed 400-pixel panel follows Omarchy, while chat windows support the system palette and bundled themes. Message size, formatting tools, the lossless project-generated UHOH notification, other bundled sounds, custom bounded PCM WAV imports, global Mute, per-conversation Auto-open, badges, and notification surfaces remain configurable.
- **Connection state:** the panel and chat distinguish connecting or reconnecting service state from an offline contact. Tox uses TCP relays with UDP discovery and hole punching disabled. Startup and periodic retries register both bootstrap nodes and compiled TCP relays.

## Security boundaries

- The Tox identity, Ratchet state, private-group registry, and history stay local. They must never enter Git or unplanned source synchronization.
- A passphrase encrypts only `tox.save`. Private filesystem permissions protect Ratchet state, group metadata, avatars, receipts, preferences, and JSONL history.
- Helper IPC is private to the local user account, not isolated from another process already running as that user. The compatibility operation `helper.shutdown` remains group-ungated, while the updater and uninstaller use only `helper.shutdown_if_no_groups`.
- Helper operations, persistence, roles, timestamps, file validation, rates, and protocol decisions remain authoritative in C. QML presents state and performs interaction only.
- Incoming text requires strict UTF-8, exact decrypted length, no embedded NUL, and bounded controls. Direct and group traffic passes stable-sender and global admission budgets before durable work.
- Direct messages require Signal support at build time. No Ratchet session means no direct plaintext fallback.
- Ordinary QML text uses the PlainText-default `SafeText` boundary. Only the escaped chat header and escaped Markdown message renderer may use RichText.
- Group attachments use bounded sender-fair pending state, explicit acceptance, stable group and member binding, exact size and SHA-256 verification, and a durable accepted-ID ledger.
- The uninstaller verifies process, executable, socket, instance, group state, and acknowledgement before removal. Runtime-rule cleanup is descriptor-relative and refuses symlinks, hardlinks, unexpected names, unsafe modes, or changed directory identity.
- Source updates clone and build outside the monitored plugin directory. The updater stops Quickshell and `omarchy-launch-shell`, checks both again before an atomic no-copy tree exchange, validates the restarted plugin consumer, and keeps helper activation group-safe. A process running as the same user remains inside the documented trust boundary and must not restart the shell concurrently.

## Open points

### Existing validation gaps

1. Submit the correlated loader crash and the crash-handler Exit-255 relaunch symptom to Quickshell upstream.
2. Complete native three-peer group-attachment injection, mixed recipient outcomes, acknowledgement loss, sender history-write failure, and transfer-ID ledger crash-injection checks.
3. Complete native separate-network checks for presence, typing, delivery, unread state, and the **New messages** divider. Phase 6 still depends on public bootstrap and relay availability, but it now distinguishes network state from encrypted-message failure.
4. Complete native Quickshell and Wayland acceptance for themes, images, multiple monitors, and floating versus tiled windows.
5. Investigate `qmlcachegen Panel.qml` parser and import failures. `qmllint Panel.qml` can still exit 255 without diagnostics.
6. Decide whether to leave the retired message clip only in historical Git objects or plan a separately governed repository-history migration.
7. Keep AUR phase 7 paused until registration and a separate approval; when packaging resumes, align `PKGBUILD` with the linked helper binary's GPL-3.0-only scope before building.

## Latest validation

The release-audit follow-up passes the full `make test` aggregate, `make verify-4`, `make helper`, `make arch`, phase 2, phase 8, Omarchy plugin validation, ShellCheck on every changed shell file, Qt parsing for all eight QML files, `qmllint` for ChatSurface, ChatPage, and Service, syntax checks, and `git diff --check`. The known `qmllint Panel.qml` limitation remains exit 255 without diagnostics.

Repeated phase 2 runs measured 13.2 to 15.2 MB helper RSS against the documented absolute 51,200 kB limit. Repeated phase 6 runs passed file, timestamp, call, and public-network diagnostics with 30 to 32 MB call RSS. Attachment checks wait for sender and receiver events plus both local history entries, then compare each event only with its matching local history timestamp.

Uninstall regressions cover current and legacy rule names, interrupted temporary names, symlink and hardlink entries, unsafe root and rule-directory modes, and unexpected files. Update regressions cover source no-ops without a shell stop, monitored-path refusal, bounded external staging and descendant cleanup, complete Git checkout identity, literal root-level protocol compatibility, pre-stop exchange probing, supervisor backoff, restarted-shell identity, restart injection before exchange, same-filesystem atomic exchange, cross-device refusal, no copy fallback, reversible rollback, post-activation helper hashes and protocol, and an unchanged `.prev` during activation.

The default UHOH notification is now the project-generated `sounds/uhoh.wav` at SHA-256 `8a27ca4badca8aa1074e2e41e2ad8c2c591e5ac3628fb680252d2bd0308c9744`. It is lossless 48 kHz signed 16-bit stereo PCM, begins and ends at zero, has 24% peak amplitude, and has a maximum adjacent-sample delta of 679. The manifest records GPL-3.0-only for the distributed payload; README and `THIRD_PARTY.md` distinguish OmaQ's GPL-3.0-or-later helper source from the GPL-3.0-only linked helper binary imposed by `libsignal-protocol-c` 2.3.3.

No current test claims visible native Wayland or multi-monitor acceptance. A read-only preflight passed on the primary machine, but the updater has not performed a live shell stop, tree exchange, or restart. Both live installations remain on `580c69cf7583ccca4461bd265334edc0a692b65d`; no private identity, Ratchet, group registry, or history data was synchronized for this audit.

## Next order

1. Prepare the Quickshell upstream crash report.
2. Run the remaining native acceptance checks without silently changing the live plugin.
3. Investigate the Panel QML toolchain failure.
4. Decide separately whether a governed Git-history migration is warranted for the retired sound blob.
5. Capture a neutral image of the current sound picker.
6. Keep publication and cleanup as separately approved phases.
