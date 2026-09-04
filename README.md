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
omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode &&
/usr/bin/env -i HOME="$HOME" PATH=/usr/bin:/bin \
  /usr/bin/python3 -I -c \
  'import os,sys;h=os.environ["HOME"];sys.exit(0 if os.path.isabs(h) and os.pathsep not in h else "HOME must be absolute and contain no Git path-list separator")' &&
mkdir -m 700 -- "$HOME/.omaq-source-install" &&
mkdir -m 700 -- "$HOME/.omaq-source-install.network-home" &&
/usr/bin/env -i -C "$HOME/.omaq-source-install.network-home" \
  HOME="$HOME/.omaq-source-install.network-home" PATH=/usr/bin:/bin \
  LANG=C.UTF-8 GIT_ATTR_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null \
  GIT_CONFIG_NOSYSTEM=1 GIT_NO_REPLACE_OBJECTS=1 GIT_OPTIONAL_LOCKS=0 \
  GIT_TERMINAL_PROMPT=0 \
  GIT_CEILING_DIRECTORIES="$HOME" \
  /usr/bin/git -c core.hooksPath=/dev/null -c core.fsmonitor=false \
  -c credential.helper= -c http.extraHeader= -c http.sslVerify=true \
  -c http.followRedirects=false -c protocol.file.allow=never \
  clone --no-hardlinks --branch main --single-branch -- \
  https://github.com/HANCORE-linux/OmaQ.git "$HOME/.omaq-source-install" &&
/usr/bin/rmdir -- "$HOME/.omaq-source-install.network-home" &&
"$HOME/.omaq-source-install/scripts/install-omaq.sh" --section right --yes
```

The final command builds the omitted Signal-enabled `helper/omaq` outside the monitored plugin directory, moves the complete checkout to `~/.config/omarchy/plugins/hancore.omaq`, and places OmaQ in the right bar section. Change `--section right` to `left` or `center` if preferred. Shibumi V2 follows the same layout without a separate OmaQ installation path. Direct messaging remains unavailable when the helper is missing. See the [installation lifecycle](docs/INSTALLATION.md) for exact-commit pinning, verification, and recovery.

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
