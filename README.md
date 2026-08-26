<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar — no account, no phone, no user search.
</p>

No signup and no email: the identity is created on this machine. No phone number is asked or stored. There is no people list and no search — the only way in is a one-time invite you send as a link or QR.

## Security

Traffic is Tox end-to-end; 1:1 text also uses the Signal Double Ratchet. Relays cannot read messages. OmaQ keeps Tox in TCP-relay privacy mode with direct UDP discovery and hole punching disabled, so contacts do not receive each other's IP addresses. You can lock the identity file with a passphrase and revoke or rotate an invite if a link leaks.

## How OmaQ works

![How OmaQ messages travel](docs/images/omaq-message-flow.png)

Toxcore transports packets between devices. Direct messages receive an additional Signal Double Ratchet encryption layer, while identity and history stay local on each device.

## How to chat

Create a one-time invite in the bar panel and send the link or QR out of band. The other person redeems it, you Accept, then open the chat window and type.

## Install

AUR packaging is currently halted. Until an AUR package is released, use this
transitional source installation. The repository intentionally does not contain
the generated `helper/omaq` binary; it is built locally so direct messages can
require the Signal Double Ratchet:

```bash
omarchy pkg add toxcore libsignal-protocol-c libpulse ttf-material-symbols-variable qrencode && omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --enable && make -C ~/.config/omarchy/plugins/hancore.omaq helper
```

The one-line command installs OmaQ's required packages, adds and enables the
plugin, then builds the helper in the plugin directory. Direct messages are
refused unless the Signal Ratchet helper is available.

## Update

Update the plugin, rebuild its local helper, and reload the plugin so the newly built helper replaces the running process:

```bash
omarchy plugin update hancore.omaq --yes && make -C ~/.config/omarchy/plugins/hancore.omaq helper && omarchy-shell shell rescanPlugins
```

## Uninstall

Use OmaQ's wrapper so the terminal also lists every user-data location that is
intentionally retained:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

This runs `omarchy plugin remove hancore.omaq` and unloads the plugin. Omarchy
deletes a Git-managed plugin folder, including local modifications inside it; a
plain plugin folder is moved to the exact hidden backup path printed by the
wrapper. Data outside the plugin folder remains in these locations:

- `~/.local/share/omaq/` — identity, contacts, groups, avatars, history, and Ratchet state
- `~/.local/state/omaq/` — preferences, unread state, receipts, surfaces, and recovery state
- `~/Downloads/omaq/` — received files
- `~/.local/state/omaq-deploy-backups/` — deployment backups, when present

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
really intend to erase. For a plain plugin folder, the wrapper also prints
safely quoted inspection and deletion commands for its exact backup path.

The dependency packages `toxcore`, `libsignal-protocol-c`, `libpulse`,
`ttf-material-symbols-variable`, and `qrencode` remain installed because other
applications may use them. The optional verification tool `zbar` also remains
when installed for testing. Inspect packages with `pacman -Qi` first. Only when
no other application needs them, remove them manually with `omarchy pkg drop`
as shown by the wrapper.

[Documentation](docs/README.md) · [Illustrated user guide](docs/USER-GUIDE.md)
