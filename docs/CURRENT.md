# Current status: 2026-09-04

This page is the current product and release snapshot. Completed phase and follow-up history lives in the [stage notes](stages/README.md).

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.8.1-beta.2`, Protocol 14
- **Accepted live source base:** both installations passed exact-commit and same-commit no-op acceptance at the `v0.8.1-beta.2` target `5a8cfaafc4f053294b6e50e7dc5f0028a59c2e50`
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
- **Notifications and appearance:** the fixed 400-pixel panel follows Omarchy, while chat windows support the system palette and bundled themes. Message size, formatting tools, the bundled UHOH notification, other sounds, custom bounded PCM WAV imports, global Mute, per-conversation Auto-open, badges, and notification surfaces remain configurable.
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
- Source installation and updates clone and build outside the monitored plugin directory. First installation uses an atomic no-replace rename before startup discovery and enablement; updates use an atomic no-copy tree exchange. Both stop Quickshell and `omarchy-launch-shell`, bind the watcher and restarted consumer, and keep helper handling fail-closed. A process running as the same user remains inside the documented trust boundary and must not restart the shell concurrently.

## Open points

### Existing validation gaps

1. Complete native three-peer group-attachment injection, mixed recipient outcomes, acknowledgement loss, sender history-write failure, and transfer-ID ledger crash-injection checks.
2. Complete native separate-network checks for presence, typing, delivery, unread state, and the **New messages** divider. Phase 6 still depends on public bootstrap and relay availability, but it now distinguishes network state from encrypted-message failure.
3. Complete native Quickshell and Wayland acceptance for themes, images, multiple monitors, and floating versus tiled windows.
4. Investigate the `qmlcachegen Panel.qml` parser and import failures. In the installed environment, `qmllint Panel.qml` can still exit 255 without diagnostics; the other QML lint targets and runtime fixtures remain the supported gates.
5. Keep AUR phase 7 paused until registration and a separate approval; when packaging resumes, align `PKGBUILD` with the linked helper binary's GPL-3.0-only scope before building.

## Latest validation

The release-audit follow-up passes the full `make test` aggregate, `make verify-4`, `make helper`, `make arch`, phase 2, phase 8, Omarchy plugin validation, ShellCheck on every changed shell file, Qt parsing for all eight QML files, `qmllint` for ChatSurface, ChatPage, and Service, syntax checks, and `git diff --check`. Panel runtime coverage verifies both semantic Omarchy theme keys and legacy `color0`–`color7` palettes, including deterministic legacy precedence in a mixed file.

Repeated phase 2 runs measured 13.2 to 15.2 MB helper RSS against the documented absolute 51,200 kB limit. Repeated phase 6 runs passed file, timestamp, call, and public-network diagnostics with 30 to 32 MB call RSS. Attachment checks wait for sender and receiver events plus both local history entries, then compare each event only with its matching local history timestamp.

Uninstall regressions cover current and legacy rule names, interrupted temporary names, symlink and hardlink entries, unsafe root and rule-directory modes, and unexpected files. Update regressions cover source no-ops without a shell stop, private credential-free network homes and `.netrc` exclusion on remote resolution, monitored-path refusal, bounded external staging and descendant cleanup, complete Git checkout identity, literal root-level protocol compatibility, pre-stop exchange probing, delayed shell readiness, supervisor backoff and reappearance during rollback, restarted-shell identity, restart injection before exchange, same-filesystem atomic exchange, cross-device refusal, no copy fallback, reversible rollback, post-activation helper hashes and protocol, and an unchanged `.prev` during activation. Installation regressions cover the root entry point's argument and dependency fail-stop behavior, Bash and Fish command parity, stale shell configuration, build-before-visibility ordering, atomic no-replace placement, a raced destination, one startup discovery followed by one enable, pre-enable rollback, and post-enable disable-with-retention.

The default UHOH notification uses `sounds/icq-message.mp3` at SHA-256 `14dcb321bb71e37bdd1cf7a9e2b3b3fbcf759e2043eeff1ad69885c13c244cf1`. The 48 kHz stereo clip runs for 3.077 seconds. The manifest records GPL-3.0-only for the distributed payload; README and `THIRD_PARTY.md` distinguish OmaQ's GPL-3.0-or-later helper source from the GPL-3.0-only linked helper binary imposed by `libsignal-protocol-c` 2.3.3.

No current test claims complete native Wayland or multi-monitor acceptance. Both installations completed shell-off updates through `5a8cfaafc4f053294b6e50e7dc5f0028a59c2e50` with bound previous trees, accepted plugin consumers, unchanged Protocol 14 helpers, clean restarts, no new coredumps, and same-commit no-ops. A visible primary-machine switch to the semantic Tokyo Night palette confirmed the System swatches and rail hover colors while Safety code and Identity remained outlined when selected; the prior Oxocarbon theme was restored afterward. No private identity, Ratchet, group registry, or history data was synchronized between machines. The source installer still requires a separately authorized live uninstall/reinstall acceptance; offline tests do not substitute for that check. The [trigger-free update history](stages/trigger-free-updates.md#deployment-validation) records the earlier failed attempts and their mitigations.

## Next order

1. Review and merge the root source-install entry point without combining that delivery with a live deployment.
2. Capture Machine 2 recovery evidence and retained-data fingerprints, then perform the separately authorized uninstall/reinstall cycle and compare every retained path byte for byte.
3. Complete the remaining native multi-monitor, separate-network, and three-peer attachment checks.
4. Decide separately whether a governed Git-history migration is warranted for the retired sound blob.
5. Keep packaging and AUR publication paused until separately approved.
