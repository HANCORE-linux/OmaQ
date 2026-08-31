<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar — no account, no phone, no user search.
</p>

No signup and no email: the identity is created locally. No phone number is asked or stored. There is no public people search — the only way in is a one-time invite you send as a link or QR.

## Security

Traffic is Tox end-to-end; 1:1 text also uses the Signal Double Ratchet. Relays cannot read messages. OmaQ keeps Tox in TCP-relay privacy mode with direct UDP discovery and hole punching disabled, so contacts do not receive each other's IP addresses. You can lock `tox.save` with a passphrase and revoke or rotate an invite if a link leaks. That passphrase does not encrypt local Ratchet state, avatars, receipts, or chat history; those remain protected by private filesystem permissions.

OmaQ is not a service. There are no accounts, no OmaQ servers, and no operator: the author runs no infrastructure and cannot access, intercept, recover, or delete your messages, identities, or contacts. To join the Tox network and exchange traffic, OmaQ connects to a small set of public bootstrap and relay nodes run by Tox community volunteers, not by this project. These nodes help clients discover the network and forward end-to-end encrypted packets, but they cannot read message contents. Everything OmaQ stores remains in local storage. OmaQ supports lawful private communication between people who trust each other.

## How OmaQ works

![How OmaQ messages travel](docs/images/omaq-message-flow.png)

Toxcore transports packets between devices. Direct messages receive an additional Signal Double Ratchet encryption layer, while identity and history stay local on each device.

## How to chat

Create a one-time invite in the bar panel and send the link or QR out of band. The other person redeems it, you Accept, then open the chat window and type. While a DirectChat or GroupChat window is floating, drag the handle in its top toolbar to move it with the pointer; buttons and selectable message text remain independent from that handle. Message text can be selected with pointer or keyboard input; inline Reply starts the existing reply flow, and selection Copy copies only the selected text. The fixed message-size setting also applies to text typed in the composer. Use the chat-header search action or `Ctrl+F` to search that conversation without affecting another open chat. Message rows show the helper-persisted local timestamp, and search results include the complete local date and time.

## Install

AUR packaging is currently halted. Until an AUR package is released, use this
transitional source installation. The repository intentionally does not contain
the generated `helper/omaq` binary; it is built locally so direct messages can
require the Signal Double Ratchet:

```bash
omarchy pkg add toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode && omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --enable && make -C ~/.config/omarchy/plugins/hancore.omaq helper
```

The one-line command installs OmaQ's required packages, adds and enables the
plugin, then builds the helper in the plugin directory. Direct messages are
refused unless the Signal Ratchet helper is available.

## Update

Update the plugin, back up and rebuild its local helper, request group-safe activation, and restart the shell:

```bash
omarchy plugin update hancore.omaq --yes && ~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --activate && omarchy restart shell
```

The final command visibly restarts the shell. This update path requires an already running Protocol-9-or-newer helper; first installation continues to use the separate `make helper` command above. Before the normal Makefile build, the updater copies the image that is actually running from its verified `/proc/<pid>/exe` descriptor to `helper/omaq.prev`; it never mistakes an already replaced path for the active image. Its doctor compares the running and available SHA-256 values and confirms the running protocol through `helper.probe`; a failed build or synchronous post-build validation restores the available path from `.prev` without touching the running process. Explicit `--activate` requests only `helper.shutdown_if_no_groups`. A probe-capable older helper that lacks this operation remains running and reports `update-pending` with `activation_unsupported`; it is never stopped through a legacy fallback. Active, uncertain, or unsupported group-safe activation leaves the old helper running; retry a supported helper after leaving its groups, or let a later full session restart use the available binary. A group-free acknowledgement briefly takes OmaQ offline, after which the existing Service reconnect logic starts the current `helper/omaq` and the updater verifies its hash, protocol, and probe. Do not run another build or plugin replacement concurrently: the private lock serializes updater invocations, not unrelated write commands. Because Service opens the configured path rather than an updater-owned descriptor, an out-of-band replacement during shutdown/restart is detected by the post-check as `degraded`, not prevented as a hot-swap transaction. If that post-check fails, the command points to the retained `.prev` image for locked `--rollback` recovery instead of silently claiming success. In-flight messages become `delivery_unknown`, while active transfers, calls, and invitations fail visibly during this optional restart. No signal is used as a fallback. The UI remains compatible with Protocol-7 and newer helpers, and existing contacts are not projected as an empty replacement identity. OmaQ keeps identity and contact data outside the plugin directory. A private identity-presence record and an encrypted-or-plain recovery copy matching committed `tox.save` state are maintained under `~/.local/state/omaq/`. If the secondary copy cannot be updated, OmaQ keeps the committed primary identity active, marks the older recovery copy as stale, and shows a degraded-recovery warning. A stale copy is never restored. If an established primary identity unexpectedly disappears, the helper restores only its current valid recovery copy or stops visibly without creating a new identity. A confirmed Import can repair that stopped state only when the bundle has the exact protected public fingerprint.

## Uninstall

Use OmaQ's wrapper so the terminal also lists every user-data location that is
intentionally retained:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

The wrapper refuses to unload or remove OmaQ while the helper owns an active private group, native and registered group state disagree, group cleanup or identity loading is pending, or helper startup overlaps removal. It binds a running helper to the exact executable inode, requires a delivered correlated group-free shutdown acknowledgement, and holds the private state lock throughout plugin removal. Missing or malformed runtime evidence fails closed without sending a signal.

After a verified group-free shutdown, this runs `omarchy plugin remove hancore.omaq`. Omarchy deletes a Git-managed plugin folder, including local modifications inside it; a
plain plugin folder is moved to the exact hidden backup path printed by the
wrapper. Data outside the plugin folder remains in these locations:

- `~/.local/share/omaq/`: identity, contacts, groups, avatars, history, Ratchet state, and OmaQ-managed custom sound copies
- `~/.local/state/omaq/`: identity guard/recovery copy, preferences, unread state, receipts, surfaces, and recovery state
- `~/Downloads/omaq/`: received files
- `~/.local/state/omaq-deploy-backups/`: deployment backups, when present

Keep retained data if you may reinstall OmaQ or still need the identity or
history. You can permanently delete any retained directory later,
independently, after inspecting it. For example:

```bash
rm -rf -- "$HOME/.local/share/omaq"                 # private identity and chat data
rm -rf -- "$HOME/.local/state/omaq"                 # local state and preferences
rm -rf -- "$HOME/Downloads/omaq"                    # received files
rm -rf -- "$HOME/.local/state/omaq-deploy-backups"  # deployment backups
```

Each deletion is irreversible. Run only the individual commands for data you
intend to erase. For a plain plugin folder, the wrapper also prints
safely quoted inspection and deletion commands for its exact backup path.

The dependency packages `toxcore`, `libsignal-protocol-c`, `libpulse`,
`libpng`, `libjpeg-turbo`, `libwebp`, `ttf-material-symbols-variable`, and
`qrencode` remain installed because other
applications may use them. The optional verification tool `zbar` also remains
when installed for testing. Inspect packages with `pacman -Qi` first. Only when
no other application needs them, remove them manually with `omarchy pkg drop`
as shown by the wrapper.

[Documentation](docs/README.md) · [Illustrated user guide](docs/USER-GUIDE.md)
