# Current status: 2026-09-08

This page is the current product and release snapshot. Completed phase and follow-up history lives in the [stage notes](stages/README.md).

## Unreleased branch work

- History append now checks flush, file sync, close, and bottom-up directory sync before returning success and publishing the message ID in its index. Failures remain ambiguous and are not safe-resend signals. See [history append durability](stages/history-append-durability.md) for tests and outstanding native validation. This is branch work, not a released capability.

## Snapshot

- **Project:** OmaQ, plugin id `hancore.omaq`
- **Branch:** `main`
- **Manifest version:** `0.9.0-beta.2`, Protocol 16
- **AUR:** paused; no registration or upload
- **Documentation:** the task-based [documentation index](README.md) links the illustrated guide, security model, installation lifecycle, and historical notes

## Working functionality

- **Pairing:** **Invite** creates a one-use 24-hour QR code and link. The recipient uses **Add contact**, and the sender explicitly accepts the request. Protocol 16 binds each direct request to the requester's Tox public key, shows both participants a byte-identical pre-acceptance safety code, warns when a different valid claimant uses the same link, and retires the one-use invitation after either decision. Protocol 15 helpers retain the code-free request and redemption cards. A successful redemption remains visible until the user edits the invitation field again. Safety codes support Tox-identity comparison through another trusted channel but do not authenticate the Signal Ratchet identity.
- **Direct chat:** Direct messages add the Signal Double Ratchet to Tox transport and never fall back to plaintext. Each chat owns its window, saved size, placement, search state, unread state, history, files, and preferences. Tox friend-name events refresh every open DirectChat after native iteration, without reopening the panel or restarting the shell.
- **Messages:** Enter sends, modified Enter inserts a line break, and delivery failures distinguish safe Resend from an unknown result. Chats support formatting, arbitrary emoji, reactions, inline Reply, editing, confirmed deletion, exact text selection and Copy, message scaling, keyboard navigation, local history timestamps, per-chat search, receipts, unread badges, and a **New messages** divider. The reaction picker pages through its full existing emoji set with arrows or the mouse wheel; long composer text scrolls to keep the cursor visible. Clear Chat is immediate rather than reconnect-queued, uses an explicitly advertised request-correlated helper capability, and applies a result only to its exact current Direct or Group conversation.
- **Files and images:** incoming transfers remain paused until acceptance and default to `~/Downloads/omaq/`. Outgoing transfers can be canceled. Received audio supports playback, while validated PNG, JPEG, and WebP images use a 56×56 preview that opens the complete local file. A selected or dropped image keeps a visible **Send image** action beside its preview. DirectChat and GroupChat share the non-call attachment workflow; video remains a normal download. Generic history errors never reuse an unrelated remembered file path.
- **Groups:** private groups support up to 10 members, owner, admin, and member roles, invitation of existing contacts, moderation, complete sender names, join and leave notices, typing, reactions, replies, edits, deletes, receipts, files, and images. A same-helper status refresh replays a still-pending native group invitation, and live per-member receipts advance visibly without regressing `Read` to `Delivered`. A `|` separates group receipts from their timestamp when they share a line; narrow layouts stack them without a separator. Profile nickname changes also send the name to existing native groups; partial updates retain the saved profile and show an explicit warning. Group peer-name events refresh member rows, message senders, and typing labels in every open GroupChat. Calls remain unavailable in GroupChat.
- **Calls:** DirectChat provides audio calls through PulseAudio with Answer, Decline, and Hang up actions, a call timer, one process-wide ringing tone, and a pulsing incoming-call bar icon. Protocol 15 keeps the UI in **Ending…** until the helper confirms that local capture, its audio thread and buffers, and the old ToxAV transport are closed. The controlling IPC socket has a renewable lease; socket loss or lease expiry ends the call locally. Correlated action failures restore an incoming ringing tone when the call remains authoritative, stale owner results are rejected, and unavailable controls report failure immediately. Audio stays in bounded memory and is not recorded. Network loss can prevent the peer from receiving the cancellation, so OmaQ confirms only the local stop.
- **Identity:** `tox.save` remains local and can use passphrase protection. Export bundles include Tox savedata and private group mappings but exclude Ratchet sessions, local history, avatars, receipts, and files. **Validate bundle** does not activate a bundle; **Import identity** is separately confirmed and rollback-protected.
- **Notifications and appearance:** the fixed 400-pixel panel follows Omarchy, while chat windows support the system palette and bundled themes. Message size, formatting tools, the bundled Knock notification, other sounds, custom bounded PCM WAV imports, global Mute, per-conversation **Pop up: On/Off**, badges, and notification surfaces remain configurable. Floating windows keep their full drag area without displaying a drag glyph.
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
- Normal source installation uses Omarchy to clone, validate, install, and rescan without requested enablement. The installed root script requires OmaQ to be disabled, then installs dependencies, builds the helper, enables OmaQ, and waits for reactive plugin activation and any in-flight watched-tree reload to expose working OmaQ IPC and matching helper images. It does not force a second shell exit while asynchronous plugin Loaders may still be finalizing. The optional exact-commit bootstrap retains the external atomic no-replace installer, while updates retain the external atomic no-copy tree exchange. The external installer retries only the exact transient `omarchy-shell is not responding` response tuple from plugin-list or shell-config IPC within its bounded readiness and post-rollback absence loops. A process running as the same user remains inside the documented cooperative trust boundary.
- Source-update retention keeps eight active trees and up to 56 older archived trees. Before staging against a full active store, the updater validates the bounded inventory and atomically archives the oldest tree without deleting it.
- GitHub CI uses a digest-pinned official Arch container, a commit-pinned checkout action, read-only permissions, and an unprivileged test account. Its `make test-ci` target excludes native Quickshell and Omarchy shell fixtures, which remain local acceptance gates.

## Open points

### Existing validation gaps

1. Complete native three-identity invite-conflict, three-peer group, and mixed-recipient attachment acceptance with an isolated third identity. Attachment injection, acknowledgement loss, sender history-write failure, and transfer-ID ledger crash-injection checks also remain open.
2. Complete the remaining separate-network checks for presence, typing, delivery, unread state, and the **New messages** divider. Earlier manual audio and separate-network checks do not cover this entire matrix. Phase 6 still depends on public bootstrap and relay availability, but it now distinguishes network state from encrypted-message failure.
3. Complete the remaining native theme and floating-versus-tiled coverage. Multi-monitor acceptance requires unavailable hardware; the image checks below do not establish complete Wayland coverage.
4. Investigate the `qmlcachegen Panel.qml` parser and import failures. In the installed environment, `qmllint Panel.qml` can still exit 255 without diagnostics; the other QML lint targets and runtime fixtures remain the supported gates.
5. Keep AUR phase 7 paused until registration and a separate approval; when packaging resumes, align `PKGBUILD` with the linked helper binary's GPL-3.0-only scope before building.

## Latest validation

### Reaction picker and composer (unreleased)

The [scrolling follow-up](stages/reaction-composer-scroll.md) adds local offscreen interaction coverage for reaction paging and long-text editing in DirectChat and GroupChat. It reproduces the original hidden-cursor failure and checks actual Qt key, pointer, and wheel events, all five message scales, narrow/wide resizing, selection, copy/paste, and send/reset behavior. Focused composer, message-action, transcript-layout, and PlainText gates pass, as do full `make test`, local `make test-ci`, helper hardening, architecture, plugin validation, and ShellCheck. `qmllint` exits 0 with import/type warnings; it is not warning-free. Native Wayland acceptance remains open; neither the live installations nor the beta.2 tag were updated.

### Material Symbols (unreleased)

The [icon follow-up](stages/material-symbols.md) explicitly loads the installed symbol font into Qt and uses codepoints instead of icon-name ligatures. A private Qt probe reproduced stale font availability after installation; registration restored glyphs without restarting that process. Source, native HarfBuzz, and isolated Quickshell checks cover mapping, loader readiness, bounded fallback, and recovery. The original [ARM report](https://github.com/HANCORE-linux/OmaQ/issues/55) still needs confirmation; no ARM root cause or Omarchy 4.0.3 compatibility fix is claimed.

### Native two-machine checks

On 2026-09-08, the maintainer completed the remaining two-machine checks on `3dbbffc4a898491b57429ef9708394ea14ba9e34`. DirectChat names refreshed immediately in both directions with the recipient panel closed. GroupChat checks covered existing sender labels in one direction, an open member list in the other, and typing after renames in both directions. The name-refresh checks used no status request, extra message, or chat/panel reopening as a refresh trigger.

On that same base, both machines projected Member → Admin → Member with the owner unchanged. Leave/reinvite restored membership in the same group without a duplicate, and the long invite error remained fully readable.

Earlier accepted implementation snapshots confirmed:

- Pending group-invite replay after status and one acceptance without a duplicate group
- Group read receipts, the inline `Read by 1 | timestamp` separator, image preview, drag-and-drop, and the visible **Send image** action
- Attachment-decline feedback on both machines, confirmed at the UI level only

Earlier checks confirmed direct and two-peer group messaging, post-acceptance Tox safety-code comparison, a byte-identical received file, real microphone/speaker operation, and separate-network use. These observations do not establish the remaining three-identity, network-matrix, or multi-monitor checks.

### Automated implementation checks

The live-name implementation passed full `make test`, local `make test-ci`, phase 3, helper hardening, architecture, plugin, no-Signal, and focused QML checks. Independent cumulative review found no remaining actionable findings. The [Arch main-CI run](https://github.com/HANCORE-linux/OmaQ/actions/runs/34240504394) passed for `3dbbffc4a898491b57429ef9708394ea14ba9e34`; it does not validate later release metadata edits.

Earlier runs failed a phase-3 post-kick cleanup assertion and an emoji-test process lookup with `ProcessLookupError`. Full reruns passed, but neither failure's cause was established. The beta.2 metadata/documentation preparation did not rerun those runtime suites.

### Earlier automated evidence

The Protocol 16 direct-invite snapshot passes the full `make verify-4` gate, Protocol 14 and 15 compatibility builds, no-Signal compilation, phases 2, 3, 6, and 8, Ratchet restart, exact IPC schemas, the key-bound stale-decision and busy-issue regressions, QML request/accessibility fixtures, helper hardening, and plaintext QML policy. Its three-identity Phase 6 run verifies the real redeemer key, byte-identical safety codes on both devices before acceptance and from `safety.get` afterward, same-key request re-announcement, conflict replay after status, unchanged pending state after a stale decision, and no second accepted contact. That automated three-identity run does not replace the remaining native acceptance listed above.

The release-audit follow-up passes the full `make test` aggregate, `make verify-4`, `make helper`, `make arch`, phase 2, phase 8, Omarchy plugin validation, ShellCheck on every changed shell file, Qt parsing for all eight QML files, `qmllint` for ChatSurface, ChatPage, and Service, syntax checks, and `git diff --check`. The normal-plugin lifecycle change additionally passes the full `make test` aggregate, `make arch`, Omarchy plugin validation, focused install and uninstall regressions, ShellCheck, Python syntax compilation, and `git diff --check`. Panel runtime coverage verifies both semantic Omarchy theme keys and legacy `color0`–`color7` palettes, including deterministic legacy precedence in a mixed file.

Repeated phase 2 runs measured 13.2 to 15.2 MB helper RSS against the documented absolute 51,200 kB limit. Repeated phase 6 runs passed file, timestamp, call, and public-network diagnostics with 30 to 32 MB call RSS. Protocol-15 phase 6 coverage also exercises request-correlated hangup, failed-action and stop-terminal replay, helper live-owner metadata during a same-socket status handshake, identity-mutation refusal during teardown, complete local media stop, ToxAV transport replacement, controlling-socket loss, lease expiry, and silence after each confirmed stop.

The Phase 6 tests use null sinks on a parent-death-bound private PipeWire/PulseAudio server outside the normal user device registry. SIGKILL regressions verify private-server teardown and exact cleanup of legacy Phase 6 orphans while retaining live owners and near matches.

The audio tests do not replace live microphone, speaker, or network acceptance. Attachment checks wait for sender and receiver events plus both local history entries, then compare each event only with its matching local history timestamp.

Uninstall regressions cover current and byte-identical relocated helper inodes, changed relocated bytes, current and legacy rule names, interrupted temporary names, symlink and hardlink entries, unsafe root and rule-directory modes, unexpected files, individually declined data, confirmed data deletion, nested Yes/No and protected-path conflicts, writable-tree and mount-boundary refusal, configured external download paths, and the manual non-recursive package command. Update regressions cover source no-ops without a shell stop, private credential-free network homes and `.netrc` exclusion on remote resolution, monitored-path refusal, bounded external staging and descendant cleanup, bounded non-deleting retention archival, name collisions, full archives, unsafe entries, complete Git checkout identity, literal root-level protocol compatibility, pre-stop exchange probing, delayed shell readiness, supervisor backoff and reappearance during rollback, restarted-shell identity, restart injection before exchange, same-filesystem atomic exchange, cross-device refusal, no copy fallback, reversible rollback, post-activation helper hashes and protocol, and an unchanged `.prev` during activation. Installation regressions cover the normal disabled Omarchy acquisition command, live validation, package/build/enable/readiness fail-stop ordering, delayed plugin IPC, readiness timeout without a forced restart, helper readiness, root entry-point arguments, Bash and Fish command parity, the external atomic no-replace path, exact enable-response loss, and bounded retries only for the exact observed shell-IPC transition tuple.

The default Knock notification uses the attributed `sounds/knock.wav`, distributed under CC0 1.0 at SHA-256 `8b54813baa31e51324e865aed8c5dfd6ecd674bab87236a8fb1df301cb92a7ae`. The manifest records GPL-3.0-only for the distributed payload; README and `THIRD_PARTY.md` distinguish OmaQ's GPL-3.0-or-later helper source from the GPL-3.0-only linked helper binary imposed by `libsignal-protocol-c` 2.3.3.

No current test claims complete native Wayland or multi-monitor acceptance.

## Next order

1. Complete the remaining native three-identity invite-conflict, three-peer group/attachment, separate-network, and display checks.
2. Prepare any new tag or release only after separate target acceptance and release approval.
3. Keep packaging and AUR publication paused until separately approved.
