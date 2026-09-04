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
| Noto Color Emoji 2.051 | 22 derived PNGs under `assets/emoji/` | OFL-1.1-no-RFN | Bundled emoji glyph images; see [`assets/emoji/ATTRIBUTION.md`](assets/emoji/ATTRIBUTION.md) |
| Simple Icons Ko-fi glyph | Derived `assets/kofi-mono.svg` | CC0-1.0 | Monochrome support link; Ko-fi name and logo remain Ko-fi trademarks |
| ICQ Desktop incoming-message sound | Derived `sounds/icq-message.mp3` | Apache-2.0 | Bundled UHOH notification sound; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| KDE Ocean Sound Theme | Four unmodified `.oga` files under `sounds/` | CC BY-SA 4.0 | Bundled notification sounds; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| Wikimedia Commons recordings | Two derived `.wav` files under `sounds/` | CC0 1.0 | Bundled notification sounds; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |
| Pixabay asset 223780 by `u_bfmec9l9lj` | Derived `sounds/phone.oga` | Pixabay Content License | Call-progress tone; see [`sounds/ATTRIBUTION.md`](sounds/ATTRIBUTION.md) |

The Ko-fi glyph preserves the path data from Simple Icons
[`icons/kofi.svg`](https://github.com/simple-icons/simple-icons/blob/8c2d958a86d745a1f1553b6dabe8218ab0227405/icons/kofi.svg)
at commit `8c2d958a86d745a1f1553b6dabe8218ab0227405`. The source SHA-256 is
`864a4d997519f937a57b11b4f052c100ca31d55936936ffaec5e653283f29e52`.
OmaQ reformats the SVG, adds accessible title attributes, and sets a fixed
monochrome fill; the distributed SHA-256 is
`225b3203b688a7ebd95a8035fddd22300db96a65c8d645965948a714af9b963f`.
The applicable CC0 text is included at
[`sounds/LICENSES/CC0-1.0.txt`](sounds/LICENSES/CC0-1.0.txt). Ko-fi's name and
logo remain Ko-fi trademarks; this source acknowledgement does not imply
endorsement.

Install runtime dependencies with the commands in the [installation lifecycle guide](docs/INSTALLATION.md). `zbar` is optional and used only for verification.

OmaQ's helper source is GPL-3.0-or-later. The distributed helper also links to [`libsignal-protocol-c` 2.3.3](https://github.com/signalapp/libsignal-protocol-c/tree/v2.3.3), whose upstream README states GPLv3 without an or-later grant, so the combined helper binary is distributed under GPL-3.0-only. The QML plugin remains MIT.
