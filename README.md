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
omarchy pkg add toxcore libsignal-protocol-c libpulse ttf-material-symbols-variable && omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --enable && make -C ~/.config/omarchy/plugins/hancore.omaq helper
```

The one-line command installs OmaQ's required packages, adds and enables the
plugin, then builds the helper in the plugin directory. Direct messages are
refused unless the Signal Ratchet helper is available.

## Update

Update the plugin and rebuild its local helper:

```bash
omarchy plugin update hancore.omaq --yes && make -C ~/.config/omarchy/plugins/hancore.omaq helper
```

## Uninstall

```bash
omarchy plugin remove hancore.omaq
```

[Documentation](docs/README.md) · [Illustrated user guide](docs/USER-GUIDE.md)
