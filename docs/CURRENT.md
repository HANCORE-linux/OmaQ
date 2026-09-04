# Current status: 2026-09-04

This page is the current product and release snapshot. Completed phase and follow-up history lives in the [stage notes](stages/README.md).

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.8.1-beta.2`, Protocol 14
- **Accepted Machine 2 source base:** normal Omarchy plugin installation, helper build, technical checks, and visible appearance passed at `a93076d66d7e9712def196a2c08ae87fa10eaa6d`; all required packages were already installed
- **Historical Machine 1 source base:** last recorded at `ff9a97bbcee66502eae47a831d4606fd01cbe628`; not changed or revalidated during the Machine 2 cycle
- **Machine 2 live helper:** Protocol 14, SHA-256 `ee43637be9ac9880bb465408a87c8ace94015c217a1df25dc79ce088308a1fba`
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
- The uninstaller verifies process, executable bytes, socket, instance, group state, and acknowledgement before removal. It accepts an inode relocated by a source-only exchange only when descriptor-bound SHA-256 checks match the live helper. Runtime-rule cleanup is descriptor-relative and refuses symlinks, hardlinks, unexpected names, unsafe modes, or changed directory identity.
- Normal source installation uses Omarchy to clone, validate, install, and rescan without requested enablement. The installed root script requires OmaQ to be disabled, then installs dependencies, builds the helper, enables OmaQ, explicitly restarts the shell, and verifies matching helper images. The optional exact-commit bootstrap retains the external atomic no-replace installer, while updates retain the external atomic no-copy tree exchange. The external installer retries only the exact transient `omarchy-shell is not responding` plugin-list result within its bounded readiness loop. A process running as the same user remains inside the documented cooperative trust boundary.

## Open points

### Existing validation gaps

1. Complete native three-peer group-attachment injection, mixed recipient outcomes, acknowledgement loss, sender history-write failure, and transfer-ID ledger crash-injection checks.
2. Complete native separate-network checks for presence, typing, delivery, unread state, and the **New messages** divider. Phase 6 still depends on public bootstrap and relay availability, but it now distinguishes network state from encrypted-message failure.
3. Complete native Quickshell and Wayland acceptance for themes, images, multiple monitors, and floating versus tiled windows.
4. Investigate the `qmlcachegen Panel.qml` parser and import failures. In the installed environment, `qmllint Panel.qml` can still exit 255 without diagnostics; the other QML lint targets and runtime fixtures remain the supported gates.
5. Keep AUR phase 7 paused until registration and a separate approval; when packaging resumes, align `PKGBUILD` with the linked helper binary's GPL-3.0-only scope before building.

## Latest validation

The release-audit follow-up passes the full `make test` aggregate, `make verify-4`, `make helper`, `make arch`, phase 2, phase 8, Omarchy plugin validation, ShellCheck on every changed shell file, Qt parsing for all eight QML files, `qmllint` for ChatSurface, ChatPage, and Service, syntax checks, and `git diff --check`. The normal-plugin lifecycle change additionally passes the full `make test` aggregate, `make arch`, Omarchy plugin validation, focused install and uninstall regressions, ShellCheck, Python syntax compilation, and `git diff --check`. Panel runtime coverage verifies both semantic Omarchy theme keys and legacy `color0`–`color7` palettes, including deterministic legacy precedence in a mixed file.

Repeated phase 2 runs measured 13.2 to 15.2 MB helper RSS against the documented absolute 51,200 kB limit. Repeated phase 6 runs passed file, timestamp, call, and public-network diagnostics with 30 to 32 MB call RSS. Attachment checks wait for sender and receiver events plus both local history entries, then compare each event only with its matching local history timestamp.

Uninstall regressions cover current and byte-identical relocated helper inodes, changed relocated bytes, current and legacy rule names, interrupted temporary names, symlink and hardlink entries, unsafe root and rule-directory modes, unexpected files, individually declined data, confirmed data deletion, nested Yes/No and protected-path conflicts, writable-tree and mount-boundary refusal, configured external download paths, and the manual non-recursive package command. Update regressions cover source no-ops without a shell stop, private credential-free network homes and `.netrc` exclusion on remote resolution, monitored-path refusal, bounded external staging and descendant cleanup, complete Git checkout identity, literal root-level protocol compatibility, pre-stop exchange probing, delayed shell readiness, supervisor backoff and reappearance during rollback, restarted-shell identity, restart injection before exchange, same-filesystem atomic exchange, cross-device refusal, no copy fallback, reversible rollback, post-activation helper hashes and protocol, and an unchanged `.prev` during activation. Installation regressions cover the normal disabled Omarchy acquisition command, live validation, package/build/enable/restart fail-stop ordering, helper readiness, root entry-point arguments, Bash and Fish command parity, the external atomic no-replace path, exact enable-response loss, and bounded retries only for the exact observed plugin-list transition error.

The default UHOH notification uses `sounds/icq-message.mp3` at SHA-256 `14dcb321bb71e37bdd1cf7a9e2b3b3fbcf759e2043eeff1ad69885c13c244cf1`. The 48 kHz stereo clip runs for 3.077 seconds. The manifest records GPL-3.0-only for the distributed payload; README and `THIRD_PARTY.md` distinguish OmaQ's GPL-3.0-or-later helper source from the GPL-3.0-only linked helper binary imposed by `libsignal-protocol-c` 2.3.3.

No current test claims complete native Wayland or multi-monitor acceptance. Machine 2 updated from `3a90583156b848aead16e14e760d793cd4b31384` to `2ae4a7dd136cecd35fa19a3123927dc6b8417655`, then the official uninstaller stopped a byte-identical helper that had continued running from the retained checkout and removed the plugin. The public exact-commit source install subsequently placed `af8ee93184ca115115302cbceaa5acd8ad598e33`, enabled OmaQ in the right section, and passed plugin, IPC, Protocol 14, helper-hash, process, and journal checks. Its same-commit update no-op preserved the checkout inode and every bound process. The unchanged baseline of 50 covered Quickshell coredumps in the acceptance harness's scope. Six `python3.14` coredumps occurred earlier that day, all before installation; a later host- and timestamp-bound read-only query found no retained coredump record after the post-acceptance log timestamp. This cycle was remote consumer acceptance, not visible Wayland acceptance. A later normal `omarchy plugin add` plus helper build installed `a93076d66d7e9712def196a2c08ae87fa10eaa6d`; plugin validation, Protocol 14, a correlated non-persisting helper probe, executable identity, and helper SHA-256 passed, and the owner visibly confirmed OmaQ appeared. That narrower check did not cover the new activation-owning `install.sh`, package installation, uninstallation, or multi-monitor behavior.

Every retained data root remained present across the uninstall and reinstall, but the aggregate fingerprint changed because orderly helper shutdown rewrote save data, empty group-registry files, and stdout spool state. This is not a byte-for-byte preservation claim. Machine 1 was not changed or revalidated during this cycle. No private identity, Ratchet, group registry, or history data was synchronized between machines. The [update](stages/trigger-free-updates.md#deployment-validation) and [installation](stages/safe-source-install.md#deployment-validation) notes record the lifecycle boundaries and evidence.

## Next order

1. Recheck Machine 1 read-only, then update it only under a separate live approval.
2. Complete the remaining native multi-monitor, separate-network, and three-peer attachment checks.
3. Prepare any new tag or release only after separate target acceptance and release approval.
4. Keep packaging and AUR publication paused until separately approved.
