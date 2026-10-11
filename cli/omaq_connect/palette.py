import os
import tomllib

from .text import clip


SEMANTIC = {"background": 0, "red": 1, "green": 2, "yellow": 3, "blue": 4, "magenta": 5, "cyan": 6, "foreground": 7}
THEME_COLORS = "~/.local/state/omarchy/current/theme/colors.toml"
HEX = set("0123456789abcdefABCDEF")
TEXT_CONTRAST = 4.5


def linear(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def contrast(a, b):
    lum = lambda c: 0.2126 * linear(c[0]) + 0.7152 * linear(c[1]) + 0.0722 * linear(c[2])
    hi, lo = sorted((lum(a), lum(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def readable(color, bg, ratio=TEXT_CONTRAST):
    target = (255, 255, 255) if contrast((255, 255, 255), bg) >= contrast((0, 0, 0), bg) else (0, 0, 0)
    for k in range(41):
        shade = mix(color, target, k / 40)
        if contrast(shade, bg) >= ratio:
            return shade
    return target


def theme_stamp():
    try:
        st = os.stat(os.path.expanduser(THEME_COLORS))
    except OSError:
        return None
    return st.st_mtime_ns, st.st_ctime_ns, st.st_ino, st.st_size


class Palette:
    def __init__(self):
        path = os.path.expanduser(THEME_COLORS)
        self.roles = {"name": "33", "status": "32", "online": "32", "unread": "31", "accent": "34",
                      "dim": "2", "offline": "2", "rule": "2", "text": "39", "away": "37", "qr": "97;40"}
        self.loaded = False
        try:
            with open(path, "rb") as f:
                data = tomllib.load(f)
        except (OSError, ValueError):
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
        try:
            screen = self.rgb(data["background"]) if "background" in data else bg
        except (ValueError, TypeError):
            screen = bg
        blend = lambda t: tuple(round(b + (f - b) * t) for f, b in zip(fg, bg))
        true = lambda c: "38;2;%d;%d;%d" % readable(c, screen)
        online = (0x7D, 0xCE, 0x6A)
        self.roles = {"name": true(palette[3]), "status": true(palette[2]), "online": true(online),
                      "unread": true(palette[1]), "accent": true(accent), "dim": true(blend(0.55)),
                      "offline": true(blend(0.35)), "rule": "38;2;%d;%d;%d" % blend(0.22), "text": true(fg),
                      "away": true((0xE0, 0xAF, 0x68)), "qr": "38;2;255;255;255;48;2;0;0;0"}
        self.loaded = True

    @staticmethod
    def rgb(value):
        value = str(value).strip().lstrip("#")
        if len(value) != 6 or not set(value) <= HEX:
            raise ValueError(value)
        return tuple(int(value[k:k + 2], 16) for k in (0, 2, 4))

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
            if used >= width or len(piece) < len(text):
                break
        return "".join(line) + " " * max(0, width - used)
