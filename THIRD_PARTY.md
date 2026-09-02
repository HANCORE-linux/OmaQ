# Third-party components

| Dependency | Used from | License | Purpose |
|---|---|---|---|
| `toxcore` | `helper/tox_adapt.c` | GPL-3.0-or-later | Direct and group transport |
| `qrencode` and `zbar` | `helper/qr.c` and `zbarimg` | LGPL | Invitation QR creation and verification |
| `libsignal-protocol-c` | `helper/ratchet_adapt.c` | GPL-3.0-only | Signal Double Ratchet for Direct messages |
| OpenSSL `libcrypto` | `helper/ratchet_adapt.c`, `helper/omaq.c` | Apache-2.0 | Signal HMAC and AES plus group-attachment SHA-256 |
| `libpng` | `helper/avatar.c` | libpng-2.0 | Bounded PNG decoding and canonical output |
| `libjpeg-turbo` | `helper/avatar.c` | BSD-3-Clause and IJG | Bounded JPEG decoding |
| `libwebp` | `helper/avatar.c` | BSD-3-Clause | Bounded WebP decoding |
| PulseAudio client library (`libpulse`) | `helper/av.c` | LGPL-2.1-or-later | Direct voice-call capture and playback |
| Material Symbols Variable | QML action glyphs | Apache-2.0 | Installed system font; not bundled |
| Simple Icons Ko-fi glyph | Derived `assets/kofi-mono.svg` | CC0-1.0 | Monochrome support link; Ko-fi name and logo remain Ko-fi trademarks |
| KDE Ocean Sound Theme | Four unmodified `.oga` files under `sounds/` | CC BY-SA 4.0 | Bundled notification sounds; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| Wikimedia Commons recordings | Two derived `.wav` files under `sounds/` | CC0 1.0 | Bundled notification sounds; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| Pixabay asset 223780 by `u_bfmec9l9lj` | Derived `sounds/phone.oga` | Pixabay Content License | Call-progress tone; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| Project-generated UHOH notification | `sounds/uhoh.wav` | MIT | Original lossless two-tone default notification; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |

Install runtime dependencies with the commands in the [installation lifecycle guide](docs/INSTALLATION.md). `zbar` is optional and used only for verification.

OmaQ's helper source is GPL-3.0-or-later. The distributed helper also links to [`libsignal-protocol-c` 2.3.3](https://github.com/signalapp/libsignal-protocol-c/tree/v2.3.3), whose upstream README states GPLv3 without an or-later grant, so the combined helper binary is distributed under GPL-3.0-only. The QML plugin remains MIT.
