<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar: no account, phone number, or public user search.
</p>

OmaQ creates your identity locally. Connect with another person by sharing a one-time private invitation as a link or QR code.

## Documentation

Use the task guides for setup, daily use, security, recovery, and removal. Each visual guide links its screenshots to the full-size original.

<table>
<tr>
<td width="33%" valign="top">
<a href="docs/USER-GUIDE.md"><img src="docs/images/guide/01-panel-home.png" alt="OmaQ panel home" width="260"></a><br>
<strong><a href="docs/USER-GUIDE.md">Illustrated user guide</a></strong><br>
Create invitations, chat, exchange files, manage groups, and protect your identity.
</td>
<td width="33%" valign="top">
<a href="docs/SECURITY.md"><img src="docs/images/omaq-message-flow.png" alt="OmaQ encrypted message flow" width="260"></a><br>
<strong><a href="docs/SECURITY.md">Security and privacy</a></strong><br>
Understand transport encryption, relay privacy, local storage, and passphrase limits.
</td>
<td width="33%" valign="top">
<a href="docs/INSTALLATION.md"><img src="docs/images/guide/15-direct-chat-overview.png" alt="OmaQ DirectChat" width="260"></a><br>
<strong><a href="docs/INSTALLATION.md">Install, update, or remove OmaQ</a></strong><br>
Follow the supported lifecycle commands, status checks, rollback, and retained-data guidance.
</td>
</tr>
</table>

Technical documentation:

- [Documentation index](docs/README.md)
- [Current product status](docs/CURRENT.md)
- [Architecture and verification plan](docs/PLAN.md)
- [Third-party components](THIRD_PARTY.md)

## Security

Tox encrypts transport end to end, and direct text messages add the Signal Double Ratchet. OmaQ disables direct UDP discovery and hole punching, so contacts do not receive each other's IP addresses.

OmaQ runs no servers and has no operator access to your identity, contacts, or messages. Public Tox bootstrap and relay nodes can forward encrypted packets but cannot read message contents. Read the [security and privacy model](docs/SECURITY.md) for storage boundaries and metadata limits.

## How OmaQ works

[![How OmaQ messages travel](docs/images/omaq-message-flow.png)](docs/SECURITY.md)

Toxcore transports encrypted packets between peers. Direct messages receive an additional Signal Double Ratchet layer, while identity and history remain in local storage.

## Install

Arch User Repository (AUR) packaging remains paused. Follow the [source installation guide](docs/INSTALLATION.md#install-omaq) to add the plugin, build its Signal-enabled helper, and verify both components.

## Update

Update the source, restart the shell, then request group-safe helper activation:

```bash
omarchy plugin update hancore.omaq --yes &&
omarchy restart shell &&
~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --activate
```

The immediate shell restart clears plugin hot-reload state before helper activation; the [update guide](docs/INSTALLATION.md#update-omaq) covers status, pending groups, and rollback.

## Uninstall

Run the verified wrapper; when the helper is running, removal requires confirmed durable group-free state and lists every retained path:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

Keep retained data if you may reinstall OmaQ or still need the identity or history. The [removal guide](docs/INSTALLATION.md#remove-omaq) lists retained paths, irreversible deletion commands, and optional package cleanup.
