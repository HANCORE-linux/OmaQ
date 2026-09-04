<h1 align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="640">
</h1>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar: no account, phone number, or public user search. Create your identity locally, then connect with a one-time private invitation link or QR code.
</p>

<table>
  <tr>
    <td width="25%" align="center"><a href="docs/images/guide/15-direct-chat-overview.png"><img src="docs/images/guide/15-direct-chat-overview.png" alt="Direct chat" width="100%"></a></td>
    <td width="25%" align="center"><a href="docs/images/guide/20-direct-image-preview.png"><img src="docs/images/guide/20-direct-image-preview.png" alt="Image sharing" width="100%"></a></td>
    <td width="25%" align="center"><a href="docs/images/guide/22-group-chat-overview.png"><img src="docs/images/guide/22-group-chat-overview.png" alt="Group chat" width="100%"></a></td>
    <td width="25%" align="center"><a href="docs/images/guide/35-demo-window.png"><img src="docs/images/guide/35-demo-window.png" alt="Local demo" width="100%"></a></td>
  </tr>
  <tr>
    <td align="center">Direct chat</td>
    <td align="center">Image sharing</td>
    <td align="center">Group chat</td>
    <td align="center">Local demo</td>
  </tr>
</table>

## Security

Tox encrypts transport end to end, and direct text messages add the Signal Double Ratchet. OmaQ disables direct UDP discovery and hole punching, so contacts do not receive each other's IP addresses.

OmaQ runs no servers and has no operator access to your identity, contacts, or messages. Public Tox bootstrap and relay nodes can forward encrypted packets but cannot read message contents. Local filesystem permissions protect Ratchet state, avatars, receipts, preferences, and chat history that are not covered by the optional `tox.save` passphrase.

## How OmaQ works

<p align="center">
  <img src="docs/images/omaq-message-flow.png" alt="How OmaQ messages travel">
</p>

<p align="center">
  Toxcore transports encrypted packets between peers. Direct messages receive an additional Signal Double Ratchet layer, while identity and history remain in local storage.
</p>

## Install

> [!IMPORTANT]
> OmaQ is not published in the Arch User Repository (AUR) yet. Until an OmaQ package is available, use the source installation below. This path does not install an OmaQ package through Pacman.

```bash
mkdir -m 700 -- "$HOME/.omaq-source-install" &&
git clone --no-hardlinks --branch main --single-branch -- \
  https://github.com/HANCORE-linux/OmaQ.git "$HOME/.omaq-source-install" &&
"$HOME/.omaq-source-install/install.sh" --section right --yes
```

This installs OmaQ and its dependencies in the right bar section. Use `left` or `center` if preferred.

## Update

Run the shell-off updater from the installed Git checkout:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh --yes
```

When needed, the updater builds outside the monitored plugin tree, exchanges the checkout while the shell is stopped, and verifies the result. Active groups can defer helper activation. See the [installation lifecycle](docs/INSTALLATION.md) for older installations, exact-commit pinning, backups, and recovery.

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
