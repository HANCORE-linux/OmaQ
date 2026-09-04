<h1 align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="640">
</h1>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar: no account, phone number, or public user search.<br>
  <sub>Create your identity locally, then connect with a one-time private invitation link or QR code.</sub>
</p>

## Security

Tox encrypts transport end to end, and direct text messages add the Signal Double Ratchet. OmaQ disables direct UDP discovery and hole punching, so contacts do not receive each other's IP addresses.

OmaQ runs no servers and has no operator access to your identity, contacts, or messages. Public Tox bootstrap and relay nodes can forward encrypted packets but cannot read message contents. Local filesystem permissions protect Ratchet state, avatars, receipts, preferences, and chat history that are not covered by the optional `tox.save` passphrase.

## How OmaQ works

![How OmaQ messages travel](docs/images/omaq-message-flow.png)

Toxcore transports encrypted packets between peers. Direct messages receive an additional Signal Double Ratchet layer, while identity and history remain in local storage.

## Install

> [!IMPORTANT]
> OmaQ is not published in the Arch User Repository (AUR) yet. Until an OmaQ package is available, use the source installation below. This path does not install an OmaQ package through Pacman.

```bash
omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode &&
source_root=$(mktemp -d "$HOME/.omaq-source-install.XXXXXX") &&
git clone --branch main --single-branch -- \
  https://github.com/HANCORE-linux/OmaQ.git "$source_root" &&
"$source_root/scripts/install-omaq.sh" --yes
```

The final command builds the omitted Signal-enabled `helper/omaq` outside the monitored plugin directory and moves the complete checkout to `~/.config/omarchy/plugins/hancore.omaq`. Direct messaging remains unavailable when that helper is missing. See the [installation lifecycle](docs/INSTALLATION.md) for private-repository authentication, exact-commit pinning, verification, and recovery.

## Update

Run the shell-off updater from the installed Git checkout:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh --yes
```

The updater returns without staging or stopping the shell when the installed commit already matches `origin/main`. Otherwise, it clones and builds outside the monitored plugin directory, proves the atomic exchange is available, stops the shell supervisor, verifies that Quickshell and its watcher are gone, and exchanges the complete Git checkout. If private groups keep the old helper running, it reports `update-pending: old helper, new tree` without weakening the group-safe shutdown rule.

Use the [installation lifecycle](docs/INSTALLATION.md) to bootstrap installations that predate this command, pin an expected commit, inspect the retained source backup, or recover a helper activation.

## Uninstall

Run the verified wrapper:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

Keep retained data if you may reinstall OmaQ or still need the identity or history. Private data, local state, received files, backups, and dependency packages remain available for manual inspection or removal.

## Documentation

- [Documentation index](docs/README.md)
- [Illustrated user guide](docs/USER-GUIDE.md)
- [Installation lifecycle](docs/INSTALLATION.md)
- [Security and privacy](docs/SECURITY.md)
- [Current status](docs/CURRENT.md)
- [Architecture plan](docs/PLAN.md)
- [Third-party components](THIRD_PARTY.md)

## License

The QML plugin is licensed under the [MIT License](LICENSE.MIT). OmaQ's helper source is GPL-3.0-or-later; the distributed helper binary is [GPL-3.0-only](LICENSE.GPL-3) because it also links to `libsignal-protocol-c`. See [Third-party components](THIRD_PARTY.md) for dependency and asset licenses.
