# Current status: 2026-09-03

This page is the current product and release snapshot. Completed phase and follow-up history lives in the [stage notes](stages/README.md).

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.8.1-beta.1`, Protocol 14
- **Live plugins:** the primary installation is a clean Git-managed checkout at `07f43f99f44ccfd80a6c31cbbe9d40234855e6be`; the second installation remains a clean Git-managed checkout at `580c69cf7583ccca4461bd265334edc0a692b65d`
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

The release-audit follow-up passes the full `make test` aggregate, `make verify-4`, `make helper`, `make arch`, phase 2, phase 8, Omarchy plugin validation, ShellCheck on every changed shell file, Qt parsing for all eight QML files, `qmllint` for ChatSurface, ChatPage, Service, and Panel, syntax checks, and `git diff --check`. Panel runtime coverage verifies both semantic Omarchy theme keys and legacy `color0`–`color7` palettes, including deterministic legacy precedence in a mixed file.

Repeated phase 2 runs measured 13.2 to 15.2 MB helper RSS against the documented absolute 51,200 kB limit. Repeated phase 6 runs passed file, timestamp, call, and public-network diagnostics with 30 to 32 MB call RSS. Attachment checks wait for sender and receiver events plus both local history entries, then compare each event only with its matching local history timestamp.

Uninstall regressions cover current and legacy rule names, interrupted temporary names, symlink and hardlink entries, unsafe root and rule-directory modes, and unexpected files. Update regressions cover source no-ops without a shell stop, private GitHub authentication without token arguments or checkout persistence, monitored-path refusal, bounded external staging and descendant cleanup, complete Git checkout identity, literal root-level protocol compatibility, pre-stop exchange probing, delayed shell readiness, supervisor backoff and reappearance during rollback, restarted-shell identity, restart injection before exchange, same-filesystem atomic exchange, cross-device refusal, no copy fallback, reversible rollback, post-activation helper hashes and protocol, and an unchanged `.prev` during activation.

The default UHOH notification is now the project-generated `sounds/uhoh.wav` at SHA-256 `8a27ca4badca8aa1074e2e41e2ad8c2c591e5ac3628fb680252d2bd0308c9744`. It is lossless 48 kHz signed 16-bit stereo PCM, begins and ends at zero, has 24% peak amplitude, and has a maximum adjacent-sample delta of 679. The manifest records GPL-3.0-only for the distributed payload; README and `THIRD_PARTY.md` distinguish OmaQ's GPL-3.0-or-later helper source from the GPL-3.0-only linked helper binary imposed by `libsignal-protocol-c` 2.3.3.

No current test claims visible native Wayland or multi-monitor acceptance. The first authorized primary-machine update attempt failed during its external remote preflight because the private origin had no noninteractive credential; it did not stop the shell or modify the live checkout. After authenticated acquisition was added, the shell-off exchange installed `7fde5c2c4b0e74c1717e8ac1baf2809694b2b393`, but the controller treated asynchronous shell startup as an immediate failure and entered rollback. Stopping that loading instance reproduced the known Quickshell `QQuickLoader`/`__dynamic_cast` crash, and a replacement supervisor appeared faster than the rollback stop handled it. The tree therefore remained on the new commit. The shell-readiness follow-up then installed `5a86d7ea49449934a499213e5d876f41f56d9325` with a clean restart, no new coredump, a bound previous tree, Protocol 14 helper continuity, complete consumer acceptance, and a same-commit no-op without process or staging changes. A later acceptance harness mistakenly imported the live controller and created an ignored Python bytecode cache, causing one hot reload without a crash. The README and panel-icon polish subsequently installed `07f43f99f44ccfd80a6c31cbbe9d40234855e6be` with another clean restart and removed that cache through the full atomic tree exchange; checkout, previous-tree, plugin-consumer, and unchanged Protocol 14 helper bindings passed without a new coredump. The second machine remains on `580c69cf7583ccca4461bd265334edc0a692b65d`; no private identity, Ratchet, group registry, or history data was synchronized.

## Next order

1. Merge and audit the system-theme compatibility fix.
2. Update the primary machine to that exact reviewed commit and complete visible theme-switch plus same-commit no-op acceptance.
3. Update the second machine only after separate approval and primary-machine acceptance.
4. Prepare the Quickshell upstream crash report.
5. Finalize the beta release notes.
6. Decide separately whether a governed Git-history migration is warranted for the retired sound blob.
