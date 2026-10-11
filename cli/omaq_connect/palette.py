import os
import tomllib

from .text import cells, clip


SEMANTIC = {"background": 0, "red": 1, "green": 2, "yellow": 3, "blue": 4, "magenta": 5, "cyan": 6, "foreground": 7}
THEME_COLORS = "~/.local/state/omarchy/current/theme/colors.toml"
GRADIENT_SECOND = (2, 6, 5, 4, 3)
GRADIENT_FROM_ACCENT, GRADIENT_FROM_NAME, GRADIENT_CONTRAST = 0.07, 0.1, 3


def linear(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def contrast(a, b):
    lum = lambda c: 0.2126 * linear(c[0]) + 0.7152 * linear(c[1]) + 0.0722 * linear(c[2])
    hi, lo = sorted((lum(a), lum(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def distance(a, b):
    def oklab(c):
        r, g, b = (linear(v) for v in c)
        l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
        m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
        s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
        return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)
    return sum((p - q) ** 2 for p, q in zip(oklab(a), oklab(b))) ** 0.5


def theme_stamp():
    try:
        st = os.stat(os.path.expanduser(THEME_COLORS))
    except OSError:
        return None
    return st.st_mtime_ns, st.st_ino, st.st_size


class Palette:
    def __init__(self):
        path = os.path.expanduser(THEME_COLORS)
        self.roles = {"name": "33", "status": "32", "online": "32", "unread": "31", "accent": "34",
                      "dim": "2", "offline": "2", "rule": "2", "text": "39", "away": "37", "qr": "97;40"}
        self.ends = None
        try:
            with open(path, "rb") as f:
                data = tomllib.load(f)
        except (OSError, tomllib.TOMLDecodeError):
            return
        found = {}
        for name, index in SEMANTIC.items():
            if name in data:
                found[index] = data[name]
        for index in range(8):
            if "color%d" % index in data:
                found[index] = data["color%d" % index]
        try:
            palette = {index: self.rgb(found[index]) for index in (0, 1, 2, 3, 7)}
            accent = self.rgb(data["accent"]) if "accent" in data else (self.rgb(found[4]) if 4 in found else palette[2])
        except (KeyError, ValueError, TypeError):
            return
        fg, bg = palette[7], palette[0]
        blend = lambda t: tuple(round(b + (f - b) * t) for f, b in zip(fg, bg))
        true = lambda c: "38;2;%d;%d;%d" % c
        self.roles = {"name": true(palette[3]), "status": true(palette[2]), "online": true((0x7D, 0xCE, 0x6A)),
                      "unread": true(palette[1]), "accent": true(accent), "dim": true(blend(0.55)),
                      "offline": true(blend(0.35)), "rule": true(blend(0.22)), "text": true(fg),
                      "away": true(blend(0.6)), "qr": "38;2;255;255;255;48;2;0;0;0"}
        candidates = []
        for index in GRADIENT_SECOND:
            try:
                candidates.append(self.rgb(found[index]))
            except (KeyError, ValueError, TypeError):
                continue
        candidates += [fg] + [tuple(round(a + (t - a) * k) for a, t in zip(accent, target))
                              for k in (0.35, 0.5) for target in (bg, (255, 255, 255))]
        self.ends = (accent, next((color for color in candidates if distance(accent, color) >= GRADIENT_FROM_ACCENT
                                   and distance(palette[3], color) >= GRADIENT_FROM_NAME
                                   and contrast(color, bg) >= GRADIENT_CONTRAST), fg))

    @staticmethod
    def rgb(value):
        value = str(value).strip().lstrip("#")
        if len(value) != 6:
            raise ValueError(value)
        return tuple(int(value[k:k + 2], 16) for k in (0, 2, 4))

    def gradient(self, text, extra=""):
        if self.ends is None:
            return [(text, "name", extra)]
        parts = cells(text)
        out = []
        for k, (chunk, _) in enumerate(parts):
            t = k / (len(parts) - 1) if len(parts) > 1 else 0
            color = tuple(round(a + (b - a) * t) for a, b in zip(*self.ends))
            out.append((chunk, "", "38;2;%d;%d;%d" % color + (";" + extra if extra else "")))
        return out

    def paint(self, text, role, extra=""):
        code = self.roles.get(role, "39")
        return "\x1b[0;%s%sm%s\x1b[0m" % (code, (";" + extra) if extra else "", text)

    def row(self, segments, width):
        line, used = [], 0
        for text, role, extra in segments:
            piece, w = clip(text, width - used)
            if piece:
                line.append(self.paint(piece, role, extra))
                used += w
            if used >= width:
                break
        return "".join(line) + " " * max(0, width - used)
