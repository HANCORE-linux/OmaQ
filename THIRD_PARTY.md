# Third-party

| Dependency | Used from | License | When |
|---|---|---|---|
| `toxcore` (Arch extra) | `helper/tox_adapt.c` | GPL-3.0-or-later | phase 1 tox — install with `omarchy pkg add toxcore` |
| `qrencode` 4.1.1-4 / `zbar` 0.23.93-7 | `helper/qr.c` + `zbarimg` in `verify-2` | LGPL | phase 2 |

Helper is GPL-3.0-or-later once linked against toxcore. QML/plugin stays MIT.

Installed here: `toxcore` 1:0.2.22-2 from Arch extra (`omarchy pkg add toxcore`).
