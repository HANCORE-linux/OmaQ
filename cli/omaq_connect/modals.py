import os
import time
from dataclasses import dataclass, field
from typing import ClassVar

from .commands import COMMANDS, KEYS, RECEIPTS, sendfile_path
from .protocol import REDEEMED_DIRECT, REDEEMED_GROUP
from .text import EMOJI, clean_line, emoji_catalog, emoji_name, home_path, human_size, text_width


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
                composer.insert(items[min(self.index, len(items) - 1)][0])
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


def path_parts(typed):
    typed = typed.lstrip()
    if typed[:1] in ("'", '"'):
        typed = typed[1:]
    if typed == "~":
        typed = "~/"
    head, sep, name = typed.rpartition("/")
    return head + sep, name


def path_folder(head):
    return os.path.abspath(os.path.expanduser(head or "."))


def listable(name):
    try:
        name.encode("utf-8")
    except UnicodeEncodeError:
        return False
    return clean_line(name) == name


def shown_folder(folder):
    text = "~" if folder == os.path.expanduser("~") else home_path(folder)
    return text if text.endswith("/") else text + "/"


@dataclass(eq=False, slots=True)
class FilePicker:
    ROWS: ClassVar[int] = 8
    LIMIT: ClassVar[int] = 10000
    index: int = 0
    listed: str = ""
    entries: list = field(default_factory=list)
    error: str = ""
    full: bool = False

    def scan(self, folder):
        if folder == self.listed:
            return
        self.listed, self.entries, self.error, self.full = folder, [], "", False
        try:
            with os.scandir(folder) as found:
                for entry in found:
                    if not listable(entry.name):
                        continue
                    try:
                        if entry.is_dir():
                            self.entries.append((entry.name, True))
                        elif entry.is_file():
                            self.entries.append((entry.name, False))
                    except OSError:
                        continue
                    if len(self.entries) >= self.LIMIT:
                        self.full = True
                        break
        except (FileNotFoundError, NotADirectoryError):
            self.error = "No such folder"
        except PermissionError:
            self.error = "No access to this folder"
        except OSError:
            self.error = "This folder cannot be read"
        self.entries.sort(key=lambda entry: (not entry[1], entry[0].casefold(), entry[0]))

    def items(self, typed):
        head, name = path_parts(typed or "")
        self.scan(path_folder(head))
        want, hidden = name.casefold(), name.startswith(".")
        return [entry for entry in self.entries
                if (hidden or not entry[0].startswith(".")) and entry[0].casefold().startswith(want)]

    def take(self, app, composer, path, stay):
        verb = composer.draft[1:].split(" ", 1)[0]
        if not stay and path != path.strip():
            path = '"%s"' % path
        composer.draft = ":%s %s" % (verb, path)
        composer.cursor = len(composer.draft)
        composer.hist, composer.hist_items, app.completion = None, None, None
        self.index = 0
        if not stay:
            app.file_picker = None

    def up(self, app, composer, typed):
        head, name = path_parts(typed)
        if name:
            self.take(app, composer, head, True)
            return
        folder = path_folder(head)
        parent = os.path.dirname(folder)
        if parent == folder or not listable(shown_folder(parent)):
            return
        self.take(app, composer, shown_folder(parent), True)
        came = os.path.basename(folder)
        items = self.items(sendfile_path(composer.draft))
        self.index = next((k for k, (entry, is_dir) in enumerate(items) if is_dir and entry == came), 0)

    def handle(self, app, kind, value):
        composer = app.pane or app.scratch
        typed = sendfile_path(composer.draft)
        if typed is None:
            app.file_picker = None
            return False
        items = self.items(typed)
        self.index = min(self.index, max(0, len(items) - 1))
        if (kind == "key" and value == "esc") or (kind == "ctrl" and value == "c"):
            app.file_picker = None
        elif kind == "key" and value in ("up", "down"):
            if items:
                self.index = (self.index + (1 if value == "down" else -1)) % len(items)
        elif kind == "key" and value in ("pgup", "pgdn"):
            step = self.ROWS if value == "pgdn" else -self.ROWS
            self.index = max(0, min(len(items) - 1, self.index + step))
        elif kind == "key" and value in ("tab", "right", "enter"):
            if items:
                name, is_dir = items[self.index]
                self.take(app, composer, path_parts(typed)[0] + name + ("/" if is_dir else ""), is_dir)
            elif value == "enter":
                app.file_picker = None
                return False
        elif kind == "key" and value in ("left", "backtab"):
            self.up(app, composer, typed)
        elif kind in ("text", "paste"):
            composer.insert(value.replace("\r", "").replace("\n", ""))
            self.index = 0
        elif (kind == "key" and value in ("backspace", "delete", "home", "end")) or (kind == "ctrl" and value in ("a", "e", "u")):
            composer.edit(kind, value)
            self.index = 0
            if sendfile_path(composer.draft) is None:
                app.file_picker = None
        else:
            app.file_picker = None
            return False
        app.dirty = True
        return True

    def size(self, name):
        try:
            return human_size(os.stat(os.path.join(self.listed, name)).st_size)
        except OSError:
            return ""

    def rows(self, screen, width, height):
        p = screen.app.palette
        composer = screen.app.pane or screen.app.scratch
        items = self.items(sendfile_path(composer.draft) or "")
        index = min(self.index, max(0, len(items) - 1))
        self.index = index
        room = max(1, min(self.ROWS, height - 2))
        first = max(0, min(index - room + 1, len(items) - room))
        shown = clean_line(shown_folder(self.listed).encode("utf-8", "replace").decode("utf-8"))
        rows = [p.row([(" files › ", "accent", "1"), (shown, "text", ""),
                       ("   ↑↓ choose · tab opens · ← back · esc closes", "dim", "")], width)]
        for k in range(first, min(len(items), first + room)):
            name, is_dir = items[k]
            chosen = k == index
            label = name + ("/" if is_dir else "")
            segs = [(" ▸ " if chosen else "   ", "accent", "1"),
                    (label, "text" if chosen or not is_dir else "name", "7" if chosen else "")]
            size = "" if is_dir else self.size(name)
            gap = width - 3 - text_width(label) - text_width(size) - 1
            if size and gap >= 2:
                segs += [(" " * gap, "dim", ""), (size, "dim", "")]
            rows.append(p.row(segs, width))
        if self.error:
            note = self.error
        elif not items:
            note = "no matching folder or file" if self.entries else "empty folder"
        else:
            note = "%d of %d" % (index + 1, len(items)) + ("  ·  the first %d entries" % self.LIMIT if self.full else "")
        rows.append(p.row([(" " + note, "dim", "")], width))
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
        left = max(0, screen.app.invites.invite.expires - int(time.time()))
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
        r = screen.app.invites.request
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
