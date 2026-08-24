# Third-party

| Dependency | Used from | License | When |
|---|---|---|---|
| `toxcore` (Arch extra) | `helper/tox_adapt.c` | GPL-3.0-or-later | phase 1 tox — install with `omarchy pkg add toxcore` |
| `qrencode` 4.1.1-4 / `zbar` 0.23.93-7 | `helper/qr.c` + `zbarimg` in `verify-2` | LGPL | phase 2 |
| `libsignal-protocol-c` (Arch extra) | `helper/ratchet_adapt.c` | GPL-3.0 | phase 8 — `omarchy pkg add libsignal-protocol-c` |
| OpenSSL `libcrypto` (already on box) | `ratchet_adapt.c` crypto provider (HMAC-SHA256, AES) | Apache-2.0 | phase 8 |
| KDE Ocean Sound Theme, commit `13ad78d18e844d0b0458ca1d71aa692ea093c845` | Nine unmodified `.oga` notification presets under `sounds/` | CC BY-SA 4.0 | notification sound settings; see `sounds/ATTRIBUTION.md` |
| Wikimedia Commons source recordings | Five derived short `.wav` notification presets under `sounds/` | CC0 1.0 / CC BY 4.0 | notification sound settings; sources, authors, processing, and hashes in `sounds/ATTRIBUTION.md` |

Helper is GPL-3.0-or-later once linked against toxcore. QML/plugin stays MIT.

Installed here: `toxcore` 1:0.2.22-2; `libsignal-protocol-c` 2.3.3-2 (`omarchy pkg add libsignal-protocol-c`, 2026-08-19).
