<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar: no account, phone number, or public user search.
</p>

OmaQ creates your identity locally. Connect with another person by sharing a one-time private invitation as a link or QR code.

## Security

Tox encrypts transport end to end, and direct text messages add the Signal Double Ratchet. OmaQ disables direct UDP discovery and hole punching, so contacts do not receive each other's IP addresses.

OmaQ runs no servers and has no operator access to your identity, contacts, or messages. Public Tox bootstrap and relay nodes can forward encrypted packets but cannot read message contents. Local filesystem permissions protect Ratchet state, avatars, receipts, preferences, and chat history that are not covered by the optional `tox.save` passphrase.

## How OmaQ works

![How OmaQ messages travel](docs/images/omaq-message-flow.png)

Toxcore transports encrypted packets between peers. Direct messages receive an additional Signal Double Ratchet layer, while identity and history remain in local storage.

## Install

Arch User Repository (AUR) packaging remains paused. Install the dependencies, add the plugin while disabled, build its local Signal-enabled helper, and then enable it:

```bash
omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode &&
omarchy plugin add \
  https://github.com/HANCORE-linux/OmaQ.git --yes &&
make -C ~/.config/omarchy/plugins/hancore.omaq helper &&
omarchy plugin enable hancore.omaq
```

The repository intentionally omits the generated `helper/omaq` binary. Building before enablement avoids writing that binary into an active monitored plugin, and direct messaging remains unavailable when the Signal Ratchet helper is missing.

## Update

Update the source and helper, then attempt one complete shell restart as the final step:

```bash
(
  finish_update() {
    update_status=$?
    restart_status=0
    trap - EXIT
    omarchy restart shell || restart_status=$?
    ((update_status == 0)) || exit "$update_status"
    exit "$restart_status"
  }
  trap finish_update EXIT

  omarchy plugin update hancore.omaq --yes &&
    ~/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh --activate
)
```

> **Known host-reload risk:** Updating or building inside the monitored plugin tree can trigger a visible Quickshell loader crash and automatic shell restart. The detached helper, private groups, and persisted data remain available, but the interface briefly disappears. The final restart guard does not prevent this trigger; a trigger-free workflow is pending.

The guarded final restart is attempted after every source, build, and activation write, including failed update paths. If that restart reports an error, rerun `omarchy restart shell` successfully before checking status or changing plugin files. The update documentation covers status, pending groups, and rollback.

## Uninstall

Run the verified wrapper:

```bash
~/.config/omarchy/plugins/hancore.omaq/scripts/uninstall-omaq.sh
```

Keep retained data if you may reinstall OmaQ or still need the identity or history. Private data, local state, received files, backups, and dependency packages remain available for manual inspection or removal.

## Documentation

[Documentation index](docs/README.md) · [Illustrated user guide](docs/USER-GUIDE.md) · [Installation lifecycle](docs/INSTALLATION.md) · [Security and privacy](docs/SECURITY.md) · [Current status](docs/CURRENT.md) · [Architecture plan](docs/PLAN.md) · [Third-party components](THIRD_PARTY.md)
