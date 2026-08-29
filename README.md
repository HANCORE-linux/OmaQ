<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar — no account, no phone, no user search.
</p>

No signup and no email: the identity is created on this machine. No phone number is asked or stored. There is no people list and no search — the only way in is a one-time invite you send as a link or QR.

## Security

Traffic is Tox end-to-end; 1:1 text also uses the Signal Double Ratchet. Relays cannot read messages. OmaQ keeps Tox in TCP-relay privacy mode with direct UDP discovery and hole punching disabled, so contacts do not receive each other's IP addresses. You can lock `tox.save` with a passphrase and revoke or rotate an invite if a link leaks. That passphrase does not encrypt local Ratchet state, avatars, receipts, or chat history; those remain protected by private filesystem permissions.

OmaQ is not a service. There are no accounts, no OmaQ servers, and no operator: the author runs no infrastructure and cannot access, intercept, recover, or delete your messages, identities, or contacts. To join the Tox network and exchange traffic, OmaQ connects to a small set of public bootstrap and relay nodes run by Tox community volunteers, not by this project. These nodes help clients discover the network and forward end-to-end encrypted packets, but they cannot read message contents. Everything OmaQ stores remains on your machine. OmaQ supports lawful private communication between people who trust each other.

## How OmaQ works

![How OmaQ messages travel](docs/images/omaq-message-flow.png)

Toxcore transports packets between devices. Direct messages receive an additional Signal Double Ratchet encryption layer, while identity and history stay local on each device.

## How to chat

Create a one-time invite in the bar panel and send the link or QR out of band. The other person redeems it, you Accept, then open the chat window and type. Message text can be selected with pointer or keyboard input; inline Reply starts the existing reply flow, and selection Copy copies only the selected text. The fixed message-size setting also applies to text typed in the composer.

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

Update the plugin, rebuild its local helper, and reload the QML source:

```bash
omarchy plugin update hancore.omaq --yes && make -C ~/.config/omarchy/plugins/hancore.omaq helper && omarchy-shell shell rescanPlugins
```

The helper is detached, so this source-only command does not replace an already running process. Protocol-14 features remain disabled until a matching helper starts through a separately controlled lifecycle or a later group-free login. This branch intentionally does not provide an in-place helper updater; never terminate a helper while it owns an active private group. The UI remains compatible with Protocol-7 and newer helpers, and existing contacts are not projected as an empty replacement identity. OmaQ keeps identity and contact data outside the plugin directory. A private identity-presence record and an encrypted-or-plain recovery copy matching committed `tox.save` state are maintained under `~/.local/state/omaq/`. If the secondary copy cannot be updated, OmaQ keeps the committed primary identity active, marks the older recovery copy as stale, and shows a degraded-recovery warning. A stale copy is never restored. If an established primary identity unexpectedly disappears, the helper restores only its current valid recovery copy or stops visibly without creating a new identity. A confirmed Import can repair that stopped state only when the bundle has the exact protected public fingerprint.

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
