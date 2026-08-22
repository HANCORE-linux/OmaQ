<p align="center">
  <img src="assets/OmaQ_Final.png" alt="OmaQ" width="420">
</p>

<p align="center">
  OmaQ is invite-only chat for the Omarchy bar — no account, no phone, no user search.
</p>

No signup and no email: the identity is created on this machine. No phone number is asked or stored. There is no people list and no search — the only way in is a one-time invite you send as a link or QR.

## Security

Traffic is Tox end-to-end; 1:1 text also uses the Signal Double Ratchet. Relays cannot read messages. You can lock the identity file with a passphrase and revoke or rotate an invite if a link leaks.

## How to chat

Create a one-time invite in the bar panel and send the link or QR out of band. The other person redeems it, you Accept, then open the chat window and type.

## Install

The plugin source does not contain the ignored `helper/omaq` binary. Install `toxcore`, `libsignal-protocol-c`, and a C compiler, then build the helper in the clone before enabling the plugin:

```bash
omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --enable
cd ~/.config/omarchy/plugins/hancore.omaq
make helper
```

If the plugin was added from another checkout, run `make helper` in that checkout and copy the resulting `helper/omaq` into the plugin directory. Direct messages are refused unless the Signal Ratchet helper is available.

## Update

```bash
omarchy plugin update hancore.omaq --yes
```

## Uninstall

```bash
omarchy plugin remove hancore.omaq
```

[Documentation](docs/README.md)
