import time
from dataclasses import dataclass, field
from typing import ClassVar

from .commands import COMMANDS, KEYS, RECEIPTS
from .protocol import REDEEMED_DIRECT, REDEEMED_GROUP
from .text import EMOJI, clean_line, emoji_catalog, emoji_name, text_width


@dataclass(eq=False, slots=True)
class Confirmation:
    action: str
    prompt: str
    at: float
    until: float
    conv: str = ""
    key: str = ""
    instance: str = ""

    @property
    def kept(self):
        return "Invite kept" if self.action == "revoke" else "Chat kept"

    def expired(self, now):
        return now > self.until


@dataclass(eq=False, slots=True)
class Help:
    index: int = 0
    top: int = 0
    follow: bool = True
    seen: tuple = (0, 0)

    def handle(self, app, kind, value):
        count = len(COMMANDS)
        self.follow = True
        if kind == "key" and value in ("up", "backtab"):
            self.index = (self.index - 1) % count
        elif kind == "key" and value in ("down", "tab"):
            self.index = (self.index + 1) % count
        elif kind == "key" and value in ("pgup", "pgdn"):
            step = max(1, app.size[1] - 8)
            self.top = max(0, self.top + (step if value == "pgdn" else -step))
            self.follow = False
        elif kind == "text" and value in ("k", "j"):
            self.index = (self.index + (1 if value == "j" else -1)) % count
        elif kind == "key" and value == "enter":
            first, span = self.seen
            if first <= 1 + self.index < first + span:
                command, _, needs = COMMANDS[self.index]
                app.help = None
                if needs:
                    composer = app.pane or app.scratch
                    composer.draft, composer.cursor = command, len(command)
                else:
                    app.command(command)
        elif (kind == "key" and value == "esc") or (kind == "text" and value == "q") or kind == "ctrl":
            app.help = None
        app.dirty = True
        return True

    def rows(self, screen, width, height):
        p = screen.app.palette
        rows = [p.row([(" commands", "name", "1"), ("   ↑↓ select · enter run · esc close", "dim", "")], width)]
        for k, (command, text, needs) in enumerate(COMMANDS):
            chosen = k == self.index
            label = command + needs
            rows.append(p.row([(" ▸ " if chosen else "   ", "accent", "1"), (label.ljust(18), "text" if chosen else "name", "7" if chosen else ""),
                               ("  " + text, "text" if chosen else "dim", "")], width))
        rows.append(" " * width)
        rows.append(p.row([(" receipts", "name", "1"), ("   ", "dim", ""), (RECEIPTS[0], "dim", ""), (" sending   ", "dim", ""),
                           (RECEIPTS[1], "dim", ""), (" sent   ", "dim", ""), (RECEIPTS[2], "dim", ""),
                           (" delivered   ", "dim", ""), (RECEIPTS[3], "online", "1"), (" read", "dim", "")], width))
        rows.append(" " * width)
        rows.append(p.row([(" keys", "name", "1")], width))
        for key, text in KEYS:
            rows.append(p.row([("   " + key.ljust(18), "text", ""), ("  " + text, "dim", "")], width))
        rows, self.top, span = screen.window(rows, width, height, self.top, 1 + self.index if self.follow else None)
        self.seen = (self.top, span)
        return rows


@dataclass(eq=False, slots=True)
class Picker:
    query: str = ""
    index: int = 0

    def items(self):
        query = self.query.strip().lower()
        items = [(emoji, emoji_name(emoji)) for emoji in EMOJI]
        if not query:
            return items
        hits = [item for item in items if query in item[1]]
        return hits + [item for item in emoji_catalog() if query in item[1] and item[0] not in EMOJI]

    def handle(self, app, kind, value):
        items = self.items()
        per_row = max(1, (app.size[0] - 1) // 4)
        if (kind == "key" and value == "esc") or (kind == "ctrl" and value in ("k", "c")):
            app.picker = None
        elif kind == "key" and value == "enter":
            if items:
                composer = app.pane or app.scratch
                emoji = items[min(self.index, len(items) - 1)][0]
                composer.draft = composer.draft[:composer.cursor] + emoji + composer.draft[composer.cursor:]
                composer.cursor += len(emoji)
                composer.hist, composer.hist_items, app.completion = None, None, None
            app.picker = None
        elif kind == "key" and value in ("left", "right", "up", "down"):
            step = {"left": -1, "right": 1, "up": -per_row, "down": per_row}[value]
            self.index = max(0, min(len(items) - 1, self.index + step))
        elif kind == "key" and value == "backspace":
            self.query, self.index = self.query[:-1], 0
        elif kind in ("text", "paste"):
            typed = "".join(ch for ch in value if ch.isprintable())
            self.query, self.index = (self.query + typed)[:40], 0
        app.dirty = True
        return True

    def rows(self, screen, width, height):
        p = screen.app.palette
        items = self.items()
        per_row = max(1, (width - 1) // 4)
        index = min(self.index, max(0, len(items) - 1))
        self.index = index
        lines = max(1, -(-len(items) // per_row))
        grid = max(1, min(4, lines, height - 2))
        first = max(0, min(index // per_row - grid + 1, lines - grid))
        rows = [p.row([(" emoji › ", "accent", "1"), (self.query, "text", ""),
                       ("   arrows choose · enter inserts · esc closes", "dim", "")], width)]
        for r in range(first, first + grid):
            segs = [(" ", "dim", "")]
            for k in range(r * per_row, min(len(items), (r + 1) * per_row)):
                segs.append((" %s " % items[k][0], "accent" if k == index else "text", "1;7" if k == index else ""))
            rows.append(p.row(segs, width))
        rows.append(p.row([(" " + (items[index][1] if items else "no emoji found"), "dim", "")], width))
        return rows[:max(1, height)]


@dataclass(eq=False, slots=True)
class Overlay:
    kind: ClassVar[str] = ""
    top: int = field(default=0, kw_only=True)

    def handle(self, app, kind, value):
        if kind == "key" and value in ("pgup", "pgdn"):
            step = max(1, app.size[1] - 8)
            self.top = max(0, self.top + (step if value == "pgdn" else -step))
        elif kind == "key" and value == "esc":
            app.overlay = None
        else:
            return False
        app.dirty = True
        return True

    def tick(self):
        return False

    def page(self, screen, rows, width, height):
        rows, self.top, _ = screen.window(rows, width, height, self.top)
        return rows


@dataclass(eq=False, slots=True)
class InviteOverlay(Overlay):
    kind: ClassVar[str] = "invite"
    url: str
    qr: list
    minute: int | None = None

    def tick(self):
        minute = int(time.time() // 60)
        if self.minute == minute:
            return False
        self.minute = minute
        return True

    def rows(self, screen, width, height):
        p = screen.app.palette
        rows = []
        left = max(0, screen.app.invite.expires - int(time.time()))
        valid = "valid %d h %02d min" % (left // 3600, left % 3600 // 60) if left else "expired; :invite creates a new link"
        rows.append(p.row([(" invite", "name", "1"), ("   %s · esc close" % valid, "dim", "")], width))
        url = clean_line(self.url)
        step = max(8, width - 2)
        chunks = [url[k:k + step] for k in range(0, len(url), step)] or [""]
        for chunk in chunks:
            rows.append(p.row([(" " + chunk, "text", "")], width))
        qr = self.qr
        notes = (screen.text_rows("Send the link or let your friend scan the code. They run :redeem <link>,", "dim", width)
                 + screen.text_rows("then :requests shows their request here and :accept adds them.", "dim", width))
        if qr and max(text_width(line) for line in qr) + 2 <= width and len(qr) + len(notes) + 2 + len(chunks) <= height:
            for line in qr:
                rows.append(p.row([(" ", "dim", ""), (line, "qr", "")], width))
        elif not qr:
            rows.extend(screen.text_rows("no QR code: qrencode is not installed", "dim", width))
        else:
            rows.extend(screen.text_rows("the window is too small for the QR code", "dim", width))
        rows.extend(notes)
        return self.page(screen, rows, width, height)


@dataclass(eq=False, slots=True)
class RequestOverlay(Overlay):
    kind: ClassVar[str] = "request"

    def rows(self, screen, width, height):
        r = screen.app.request
        if not r:
            screen.app.overlay = None
            return [" " * width] * height
        p = screen.app.palette
        rows = []
        if r.kind == "group":
            rows.append(p.row([(" group invitation", "name", "1"), ("   :accept or :decline · esc close", "dim", "")], width))
            rows.extend(screen.text_rows("A friend invited you to a private group. Group chats show in the bar for now.", "dim", width))
        else:
            rows.append(p.row([(" friend request", "name", "1"), ("   :accept or :decline · esc close", "dim", "")], width))
            rows.extend(screen.text_rows("Compare this code with your friend before accepting", "dim", width))
            rows.extend(screen.code_rows(r.safety, width))
            if r.conflict:
                rows.extend(screen.text_rows("Another device used this invite link", "unread", width))
        return self.page(screen, rows, width, height)


@dataclass(eq=False, slots=True)
class RedeemedOverlay(Overlay):
    kind: ClassVar[str] = "redeemed"
    group: bool
    safety: str

    def rows(self, screen, width, height):
        p = screen.app.palette
        rows = [p.row([(" " + (REDEEMED_GROUP if self.group else REDEEMED_DIRECT), "name", "1"),
                       ("   esc close", "dim", "")], width)]
        if self.safety:
            rows.extend(screen.text_rows("Compare this code with your friend before they accept", "dim", width))
            rows.extend(screen.code_rows(self.safety, width))
        return self.page(screen, rows, width, height)


OVERLAYS = {overlay.kind: overlay for overlay in (InviteOverlay, RequestOverlay, RedeemedOverlay)}
