#!/usr/bin/env python3
import codecs
import fcntl
import json
import os
import secrets
import select
import signal
import socket
import stat
import subprocess
import sys
import termios
import time
import tomllib
import tty
import unicodedata

PROTOCOL_MIN = 16
LINE_MAX = 4096
TEXT_MAX = 511
HISTORY_LIMIT = 50
HISTORY_RETRY = 3.0
TYPING_SEND_EVERY = 3.0
TYPING_IDLE = 3.0
TYPING_EXPIRE = 4.5
STATUS_SECONDS = 6.0
PANE_MIN_WIDTH = 24
PANE_MIN_HEIGHT = 4
NARROW_BODY = 12
COMMAND = "omaq-connect"
BAR_PROBE = "/usr/bin/omarchy-shell"
RECEIPTS = ["·", "→", "⇉", "⇉"]
FAILURES = {
    "offline": "Contact is offline",
    "ratchet_pending": "Secure session is being established",
    "no_ratchet": "Secure session unavailable; pair this contact again",
    "history_failed": "Message could not be saved or sent",
    "busy": "Helper is busy",
    "helper_incompatible": "Helper update required",
    "locked": "Identity is locked",
    "identity_changed": "Identity changed; try again",
    "delivery_unknown": "Delivery status unknown; check with the recipient",
    "forbidden": "Message was rejected",
}
BIDI = frozenset(list(range(0x202A, 0x202F)) + list(range(0x2066, 0x206A)))
DIRECT_DIGITS = frozenset("0123456789")
VERBS = [("help", False), ("connect", True), ("split", True), ("quit", False), ("qa", False)]
HINT = " " + "   ".join(":" + verb for verb, _ in VERBS)
COMMANDS = [
    (":help", "Show this list", False),
    (":connect ", "Open a chat in a new pane on the right", True),
    (":split ", "Open a chat in a new pane below", True),
    (":quit", "Close this pane; the last pane quits OmaQ (also :q)", False),
    (":qa", "Quit OmaQ", False),
]
KEYS = [
    ("ctrl+n", "New pane with the next friend"),
    ("ctrl+w", "Close this pane"),
    ("alt+arrows", "Move the focus between panes"),
    ("alt+shift+arrows", "Move this pane"),
    ("alt+= / alt+-", "Make this column wider or narrower"),
    ("tab", "Next chat in this pane; after : it completes commands and names"),
    ("pgup / pgdn", "Scroll"),
    ("shift+enter", "New line in a message"),
    ("esc", "Clear the message"),
]
NAMED = ("connect", "c", "vsplit", "vs", "split", "sp")
SEMANTIC = {"background": 0, "red": 1, "green": 2, "yellow": 3, "blue": 4, "magenta": 5, "cyan": 6, "foreground": 7}


def default_home():
    return os.path.expanduser("~/.local/share/omaq")


def default_state():
    return os.path.expanduser("~/.local/state/omaq")


def omaq_paths():
    return os.environ.get("OMAQ_HOME") or default_home(), os.environ.get("OMAQ_STATE") or default_state()


def helper_binary():
    return os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), "helper", "omaq")


def ensure_private_dir(path):
    try:
        if not os.path.lexists(path):
            parent = os.path.dirname(path)
            if parent and not os.path.isdir(parent):
                os.makedirs(parent, exist_ok=True)
            try:
                os.mkdir(path, 0o700)
            except FileExistsError:
                pass
        st = os.lstat(path)
    except OSError as error:
        return "%s could not be created: %s" % (path, error.strerror or error)
    if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid():
        return "%s must be a real directory owned by you" % path
    if stat.S_IMODE(st.st_mode) & 0o077:
        return "%s permissions are %03o; run: chmod 700 -- '%s'" % (path, stat.S_IMODE(st.st_mode), path)
    return ""


def read_protocol(state):
    try:
        with open(os.path.join(state, "omaq.protocol"), encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def clean(text):
    out = []
    for ch in str(text):
        code = ord(ch)
        if ch == "\n":
            out.append(ch)
        elif ch == "\t":
            out.append(" ")
        elif code < 0x20:
            out.append(chr(0x2400 + code))
        elif code == 0x7F:
            out.append(chr(0x2421))
        elif 0x80 <= code <= 0x9F or code in BIDI:
            out.append(chr(0xFFFD))
        else:
            out.append(ch)
    return "".join(out)


def clean_line(text):
    return clean(text).replace("\n", " ")


def outgoing(text):
    text = str(text).replace("\r\n", "\n").replace("\r", "\n")
    return "".join(ch for ch in text if ch in "\n\t" or ord(ch) >= 0x20)


def is_direct(conversation):
    return bool(conversation) and set(conversation) <= DIRECT_DIGITS and (conversation == "0" or conversation[0] != "0")


def char_width(ch):
    if unicodedata.combining(ch) or unicodedata.category(ch) in ("Mn", "Me", "Cf"):
        return 0
    return 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1


def text_width(text):
    return sum(char_width(ch) for ch in text)


def clip(text, width):
    out, used = [], 0
    for ch in text:
        w = char_width(ch)
        if used + w > width:
            break
        out.append(ch)
        used += w
    return "".join(out), used


def shorten(text, width):
    if text_width(text) <= width:
        return text
    return clip(text, max(0, width - 1))[0] + "…"


def wrap(text, width):
    width = max(1, width)
    lines = []
    for para in text.split("\n"):
        line, used = "", 0
        for word in para.split(" "):
            ww = text_width(word)
            if line and used + 1 + ww <= width:
                line, used = line + " " + word, used + 1 + ww
                continue
            if line:
                lines.append(line)
                line, used = "", 0
            while ww > width:
                part, _ = clip(word, width)
                if not part:
                    part = word[0]
                lines.append(part)
                word = word[len(part):]
                ww = text_width(word)
            line, used = word, ww
        lines.append(line)
    return lines


def stamp(ts, today=None):
    if not ts:
        return ""
    local = time.localtime(ts)
    if time.strftime("%Y-%m-%d", local) == (today or time.strftime("%Y-%m-%d")):
        return time.strftime("%H:%M", local)
    return time.strftime("%Y-%m-%d %H:%M", local)


def link_path():
    return os.path.join(os.path.expanduser("~"), ".local", "bin", COMMAND)


def ours(target):
    if not target.endswith(os.path.join("cli", "omaq.py")):
        return False
    root = os.path.dirname(os.path.dirname(target))
    if not os.path.exists(target):
        return os.path.basename(root) == "hancore.omaq"
    try:
        with open(os.path.join(root, "manifest.json"), encoding="utf-8") as f:
            return json.load(f).get("id") == "hancore.omaq"
    except (OSError, ValueError, AttributeError):
        return False


def install_link():
    path = link_path()
    target = os.path.realpath(__file__)
    if os.path.lexists(path):
        if not os.path.islink(path) or not ours(os.readlink(path)):
            print("%s: %s exists and is not OmaQ's; left unchanged" % (COMMAND, path), file=sys.stderr)
            return 1
        if os.readlink(path) == target:
            print("%s: %s is current" % (COMMAND, path))
            return 0
    os.makedirs(os.path.dirname(path), mode=0o755, exist_ok=True)
    temporary = "%s.%s.tmp" % (path, secrets.token_hex(4))
    os.symlink(target, temporary)
    try:
        os.replace(temporary, path)
    except OSError:
        os.unlink(temporary)
        raise
    print("%s: linked %s" % (COMMAND, path))
    return 0


def remove_link():
    path = link_path()
    if not os.path.islink(path):
        return 0
    if not ours(os.readlink(path)):
        print("%s: %s is not OmaQ's; left unchanged" % (COMMAND, path), file=sys.stderr)
        return 1
    os.unlink(path)
    print("%s: removed %s" % (COMMAND, path))
    return 0


class Palette:
    def __init__(self):
        path = os.path.expanduser("~/.local/state/omarchy/current/theme/colors.toml")
        self.roles = {"name": "33", "status": "32", "online": "32", "unread": "31", "accent": "34",
                      "dim": "2", "offline": "2", "rule": "2", "text": "39", "away": "37"}
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
                      "away": true(blend(0.6))}

    @staticmethod
    def rgb(value):
        value = str(value).strip().lstrip("#")
        if len(value) != 6:
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
            if used >= width:
                break
        return "".join(line) + " " * max(0, width - used)


class Link:
    def __init__(self, path):
        st = os.lstat(path)
        if not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid() or stat.S_IMODE(st.st_mode) & 0o077:
            raise OSError("unsafe helper socket")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
        self.sock.settimeout(2.0)
        try:
            self.sock.connect(path)
        except OSError:
            self.sock.close()
            raise
        self.sock.setblocking(False)
        self.buf = b""

    def fileno(self):
        return self.sock.fileno()

    def send(self, obj):
        line = (json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        if len(line) > LINE_MAX:
            return False
        self.sock.settimeout(2.0)
        try:
            self.sock.sendall(line)
        finally:
            self.sock.setblocking(False)
        return True

    def read(self):
        try:
            data = self.sock.recv(65536)
        except (BlockingIOError, InterruptedError):
            return []
        if not data:
            raise ConnectionError("helper closed the connection")
        self.buf += data
        events = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            try:
                event = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(event, dict):
                events.append(event)
        if len(self.buf) > (1 << 20):
            self.buf = b""
        return events

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class Presence:
    def __init__(self, state_dir):
        base = os.environ.get("XDG_RUNTIME_DIR")
        if not base:
            base = state_dir
            if ensure_private_dir(base):
                base = ""
        self.dir = os.path.join(base, "omaq-cli") if base else ""
        self.lock = None
        if not self.dir:
            return
        try:
            os.makedirs(self.dir, mode=0o700, exist_ok=True)
            path = os.path.join(self.dir, "%d.lock" % os.getpid())
            fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.lock = (fd, path)
        except OSError:
            self.lock = None

    def others_alive(self):
        alive = False
        try:
            names = os.listdir(self.dir)
        except OSError:
            return False
        for name in names:
            if not name.endswith(".lock") or (self.lock and name == os.path.basename(self.lock[1])):
                continue
            path = os.path.join(self.dir, name)
            try:
                fd = os.open(path, os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW)
            except OSError:
                continue
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                os.unlink(path)
            except OSError:
                alive = True
            finally:
                os.close(fd)
        return alive

    def mark_started(self, instance, nonce):
        if not self.dir:
            return
        try:
            path = os.path.join(self.dir, "started")
            fd = os.open(path + ".tmp", os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write("%s %s" % (instance, nonce))
            os.replace(path + ".tmp", path)
        except OSError:
            pass

    def started(self):
        try:
            with open(os.path.join(self.dir, "started")) as f:
                parts = f.read().split()
        except OSError:
            return "", ""
        return (parts[0], parts[1]) if len(parts) == 2 else ("", "")

    def clear_started(self):
        try:
            os.unlink(os.path.join(self.dir, "started"))
        except OSError:
            pass

    def log(self):
        if not self.dir:
            return None, ""
        path = os.path.join(self.dir, "helper.log")
        try:
            return os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600), path
        except OSError:
            return None, ""

    def release(self):
        if self.lock:
            fd, path = self.lock
            try:
                os.unlink(path)
            except OSError:
                pass
            os.close(fd)
            self.lock = None


class Keys:
    SPECIAL = {"A": "up", "B": "down", "C": "right", "D": "left", "H": "home", "F": "end", "Z": "backtab",
               "I": "focus-in", "O": "focus-out", "P": "f1"}
    TILDE = {"1": "home", "3": "delete", "4": "end", "5": "pgup", "6": "pgdn", "7": "home", "8": "end",
             "11": "f1", "200": "paste-start", "201": "paste-end"}
    MODS = {"2": "shift-", "3": "alt-", "4": "alt-shift-", "5": "ctrl-"}

    def __init__(self):
        self.pending = b""
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.paste = None

    def feed(self, data, final=False):
        self.pending += data
        keys = []
        buf, i = self.pending, 0
        while i < len(buf):
            b = buf[i]
            if self.paste is not None:
                end = buf.find(b"\x1b[201~", i)
                if end < 0:
                    if final:
                        self.paste += self.decoder.decode(buf[i:])
                        i = len(buf)
                    else:
                        keep = max(i, len(buf) - 5)
                        self.paste += self.decoder.decode(buf[i:keep])
                        i = keep
                    break
                self.paste += self.decoder.decode(buf[i:end], final=True)
                keys.append(("paste", self.paste.replace("\r\n", "\n").replace("\r", "\n")))
                self.paste = None
                i = end + 6
                continue
            if b == 0x1B:
                if i + 1 >= len(buf):
                    if final:
                        keys.append(("key", "esc"))
                        i += 1
                    break
                nxt = buf[i + 1]
                if nxt in (0x5B, 0x4F):
                    j = i + 2
                    while j < len(buf) and not (0x40 <= buf[j] <= 0x7E):
                        j += 1
                    if j >= len(buf):
                        if final:
                            i = len(buf)
                        break
                    params, last = buf[i + 2:j].decode("ascii", "replace"), chr(buf[j])
                    i = j + 1
                    fields = params.split(";")
                    mod = self.MODS.get(fields[1] if len(fields) > 1 else "", "")
                    if last == "~":
                        name = self.TILDE.get(fields[0])
                        if name == "paste-start":
                            self.paste = ""
                        elif name:
                            keys.append(("key", mod + name))
                    elif last == "u":
                        if fields[0] == "13":
                            keys.append(("key", (mod or "shift-") + "enter"))
                    elif last in self.SPECIAL:
                        keys.append(("key", mod + self.SPECIAL[last]))
                    continue
                if nxt == 0x1B:
                    keys.append(("key", "esc"))
                    i += 1
                    continue
                if nxt in (0x0D, 0x0A):
                    keys.append(("key", "alt-enter"))
                else:
                    ch = self.decoder.decode(bytes([nxt]))
                    if ch:
                        keys.append(("alt", ch))
                i += 2
                continue
            if b in (0x0D, 0x0A):
                keys.append(("key", "enter"))
            elif b in (0x7F, 0x08):
                keys.append(("key", "backspace"))
            elif b == 0x09:
                keys.append(("key", "tab"))
            elif b < 0x20:
                keys.append(("ctrl", chr(b + 0x60)))
            else:
                j = i
                while j < len(buf) and buf[j] >= 0x20 and buf[j] != 0x7F and buf[j] != 0x1B:
                    j += 1
                text = self.decoder.decode(buf[i:j])
                if text:
                    keys.append(("text", text))
                i = j
                continue
            i += 1
        self.pending = buf[i:]
        return keys


class Message:
    __slots__ = ("id", "request", "dir", "text", "ts", "sender", "ack", "failed")

    def __init__(self, mid, request, direction, text, ts, sender="", ack=0, failed=""):
        self.id, self.request, self.dir, self.text = mid, request, direction, text
        self.ts, self.sender, self.ack, self.failed = ts, sender, ack, failed


def message_from(item):
    kind = str(item.get("kind") or "")
    text = str(item.get("text") or "")
    if kind in ("file", "image"):
        text = "[%s] %s" % (kind, text) if text else "[%s]" % kind
    direction = str(item.get("dir") or ("out" if item.get("from") == "me" else "in"))
    receipt = str(item.get("receipt") or "")
    ack = 3 if receipt == "read" else (2 if receipt == "delivered" else (1 if direction == "out" else 0))
    ts = item.get("ts") or 0
    try:
        ts = float(ts)
    except (TypeError, ValueError):
        ts = 0.0
    if ts > 1e12:
        ts /= 1000.0
    return Message(str(item.get("id") or ""), str(item.get("request") or ""), direction, text, ts,
                   str(item.get("sender") or ""), ack)


class Pane:
    __slots__ = ("conv", "scroll", "draft", "cursor")

    def __init__(self, conversation):
        self.conv = conversation
        self.scroll = 0
        self.draft = ""
        self.cursor = 0


class Layout:
    def __init__(self):
        self.columns = []
        self.weights = []
        self.col = 0
        self.row = 0

    def panes(self):
        return [p for column in self.columns for p in column]

    def focused(self):
        if not self.columns:
            return None
        return self.columns[self.col][self.row]

    def find(self, conversation):
        for c, column in enumerate(self.columns):
            for r, pane in enumerate(column):
                if pane.conv == conversation:
                    return c, r
        return None

    def add(self, pane, vertical=True):
        if not self.columns:
            self.columns, self.weights, self.col, self.row = [[pane]], [1.0], 0, 0
        elif vertical:
            self.columns.insert(self.col + 1, [pane])
            self.weights.insert(self.col + 1, 1.0)
            self.col, self.row = self.col + 1, 0
        else:
            self.columns[self.col].insert(self.row + 1, pane)
            self.row += 1

    def close(self):
        if len(self.panes()) <= 1:
            return None
        pane = self.columns[self.col].pop(self.row)
        if not self.columns[self.col]:
            self.columns.pop(self.col)
            self.weights.pop(self.col)
            self.col = min(self.col, len(self.columns) - 1)
        self.row = min(self.row, len(self.columns[self.col]) - 1)
        return pane

    def focus(self, direction):
        if not self.columns:
            return
        if direction in ("left", "right"):
            self.col = max(0, min(len(self.columns) - 1, self.col + (1 if direction == "right" else -1)))
            self.row = min(self.row, len(self.columns[self.col]) - 1)
        else:
            self.row = max(0, min(len(self.columns[self.col]) - 1, self.row + (1 if direction == "down" else -1)))

    def move(self, direction):
        if not self.columns:
            return
        column = self.columns[self.col]
        if direction in ("up", "down"):
            other = self.row + (1 if direction == "down" else -1)
            if 0 <= other < len(column):
                column[self.row], column[other] = column[other], column[self.row]
                self.row = other
            return
        step = 1 if direction == "right" else -1
        target = self.col + step
        if not 0 <= target < len(self.columns):
            if len(column) == 1:
                return
            pane = column.pop(self.row)
            at = len(self.columns) if step > 0 else 0
            self.columns.insert(at, [pane])
            self.weights.insert(at, 1.0)
            self.col, self.row = at, 0
            return
        pane = column.pop(self.row)
        row = min(self.row, len(self.columns[target]))
        self.columns[target].insert(row, pane)
        if not column:
            self.columns.pop(self.col)
            self.weights.pop(self.col)
            if target > self.col:
                target -= 1
        self.col, self.row = target, row

    def resize(self, delta):
        if self.weights:
            self.weights[self.col] = max(0.3, min(3.0, self.weights[self.col] + delta))

    def geometry(self, width, height):
        n = len(self.columns)
        if not n:
            return []
        room = width - (n - 1)
        total = sum(self.weights)
        widths = [max(1, int(room * w / total)) for w in self.weights]
        widths[-1] = max(1, room - sum(widths[:-1]))
        if min(widths) < PANE_MIN_WIDTH:
            return None
        boxes, x = [], 0
        for c, column in enumerate(self.columns):
            k = len(column)
            usable = height - (k - 1)
            heights = [usable // k] * k
            heights[-1] += usable - sum(heights)
            if min(heights) < PANE_MIN_HEIGHT:
                return None
            y = 0
            for r, pane in enumerate(column):
                boxes.append((c, r, pane, x, y, widths[c], heights[r]))
                y += heights[r] + 1
            x += widths[c] + 1
        return boxes


class App:
    def __init__(self, target):
        self.home, self.state = omaq_paths()
        self.target = target
        self.palette = Palette()
        self.presence = Presence(self.state)
        self.link = None
        self.proc = None
        self.owns = False
        self.launch_nonce = ""
        self.nonce = ""
        self.instance = ""
        self.protocol = 0
        self.handshake = False
        self.locked = False
        self.incompatible = False
        self.conn = "connecting"
        self.friends = []
        self.friend_build = None
        self.friend_generation = ""
        self.friends_ready = False
        self.unread = {}
        self.read_pending = {}
        self.retry_at = {}
        self.convs = {}
        self.pending = {}
        self.drafts = {}
        self.layout = Layout()
        self.secret = ""
        self.unlock_request = ""
        self.status = ""
        self.status_until = 0.0
        self.focused = True
        self.typing_sent = 0.0
        self.typing_on = ""
        self.last_edit = 0.0
        self.rehandshake_at = 0.0
        self.running = True
        self.dirty = True
        self.next_connect = 0.0
        self.keys = Keys()
        self.size = (0, 0)
        self.visible = set()
        self.help = None
        self.completion = None
        self.scratch = Pane("")
        self.exit_note = ""

    @property
    def pane(self):
        return self.layout.focused()

    @property
    def current(self):
        return self.pane.conv if self.pane else ""

    def say(self, text, seconds=STATUS_SECONDS):
        self.status = clean_line(text)
        self.status_until = time.monotonic() + seconds
        self.dirty = True

    def friend(self, fid):
        for f in self.friends:
            if f["id"] == fid:
                return f
        return None

    def key_for(self, conversation):
        f = self.friend(conversation) if is_direct(conversation) else None
        return f["key"] if f else ""

    def bound(self, ev):
        conversation = str(ev.get("conversation") or "")
        if not is_direct(conversation):
            return True
        f = self.friend(conversation)
        return bool(f) and bool(f["key"]) and str(ev.get("key") or "") == f["key"]

    def conv(self, conversation):
        c = self.convs.get(conversation)
        if c is None:
            c = {"items": [], "loaded": False, "request": "", "typing_until": 0.0}
            self.convs[conversation] = c
        return c

    def draft_key(self, conversation):
        f = self.friend(conversation) if is_direct(conversation) else None
        return ("k:" + f["key"]) if f and f["key"] else ("c:" + conversation)

    def stash_draft(self, pane):
        if pane.conv:
            if pane.draft:
                self.drafts[self.draft_key(pane.conv)] = (pane.draft, pane.cursor)
            else:
                self.drafts.pop(self.draft_key(pane.conv), None)
        pane.draft, pane.cursor = "", 0

    def restore_draft(self, pane):
        if pane.conv:
            pane.draft, pane.cursor = self.drafts.pop(self.draft_key(pane.conv), ("", 0))

    def send(self, obj):
        if not self.link:
            return False
        try:
            return self.link.send(obj)
        except OSError:
            self.drop_link()
            return False

    def send_bound(self, obj):
        conversation = str(obj.get("conversation") or "")
        if is_direct(conversation):
            key = self.key_for(conversation)
            if not key:
                return False
            obj = dict(obj, key=key)
        return self.send(obj)

    def handshake_request(self):
        self.handshake = False
        self.friends_ready = False
        self.nonce = "cli-status-" + secrets.token_hex(8)
        self.send({"op": "status", "id": self.nonce})

    def connect(self):
        path = os.path.join(self.state, "omaq.sock")
        try:
            self.link = Link(path)
        except OSError:
            self.link = None
        if not self.link and self.proc is None and (self.owns or not self.instance):
            self.proc = self.start_helper()
            deadline = time.monotonic() + 10.0
            while self.proc is not None and time.monotonic() < deadline and not self.link:
                if self.proc.poll() is not None and self.proc.returncode != 2:
                    self.report_start_failure()
                    self.proc = None
                    break
                try:
                    self.link = Link(path)
                except OSError:
                    time.sleep(0.1)
        if not self.link:
            self.conn = "helper_down"
            self.next_connect = time.monotonic() + 2.0
            self.dirty = True
            return
        self.handshake_request()
        self.dirty = True

    def start_helper(self):
        binary = helper_binary()
        if not os.access(binary, os.X_OK):
            self.say("OmaQ helper not found: %s" % binary, 30)
            return None
        for path in (self.home, self.state):
            problem = ensure_private_dir(path)
            if problem:
                self.say(problem, 60)
                return None
        self.launch_nonce = secrets.token_hex(16)
        env = dict(os.environ, OMAQ_HOME=self.home, OMAQ_STATE=self.state, OMAQ_PROTOCOL_NONCE=self.launch_nonce)
        log_fd, self.log_path = self.presence.log()
        try:
            return subprocess.Popen([binary], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=log_fd if log_fd is not None else subprocess.DEVNULL,
                                    start_new_session=True, close_fds=True)
        except OSError:
            return None
        finally:
            if log_fd is not None:
                os.close(log_fd)

    def report_start_failure(self):
        detail = ""
        try:
            with open(self.log_path, "rb") as f:
                f.seek(0, os.SEEK_END)
                f.seek(max(0, f.tell() - 400))
                lines = [line for line in f.read().decode("utf-8", "replace").splitlines() if line.strip()]
            detail = lines[-1] if lines else ""
        except (OSError, AttributeError, TypeError):
            pass
        self.say("Helper could not start" + (": " + detail if detail else ""), 60)

    def drop_link(self):
        if self.link:
            self.link.close()
        self.link = None
        self.handshake = False
        self.friend_build = None
        self.conn = "reconnecting"
        self.next_connect = time.monotonic() + 1.0
        for c in self.convs.values():
            c["loaded"], c["request"] = False, ""
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        self.dirty = True

    def reset_identity(self):
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        self.friends, self.friend_build, self.friends_ready = [], None, False
        self.convs, self.unread, self.read_pending, self.retry_at, self.drafts = {}, {}, {}, {}, {}
        self.typing_on = ""
        for pane in self.layout.panes() + [self.scratch]:
            pane.conv, pane.draft, pane.cursor, pane.scroll = "", "", 0, 0
        self.handshake_request()

    def owned_by_cli(self):
        instance, nonce = self.presence.started()
        protocol = read_protocol(self.state)
        return bool(instance) and instance == self.instance and protocol.get("instance") == instance \
            and protocol.get("nonce") == nonce

    def bar_attached(self):
        try:
            if os.path.realpath(self.state) != os.path.realpath(default_state()):
                return False
        except OSError:
            return True
        if not os.access(BAR_PROBE, os.X_OK):
            return False
        env = dict(os.environ, PATH="/usr/bin:/bin", OMARCHY_SHELL_IPC_TIMEOUT="0.3s")
        try:
            result = subprocess.run([BAR_PROBE, "hancore.omaq", "status"], env=env, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2)
        except (OSError, subprocess.TimeoutExpired):
            return True
        return result.returncode == 0

    def on_event(self, ev):
        name = ev.get("event")
        if name == "snapshot":
            if not self.handshake:
                if str(ev.get("request") or "") != self.nonce or not ev.get("instance"):
                    return
                try:
                    protocol = int(ev.get("protocol"))
                except (TypeError, ValueError):
                    protocol = 0
                if protocol < PROTOCOL_MIN:
                    self.incompatible = True
                    self.say(FAILURES["helper_incompatible"], 3600)
                    return
                self.handshake = True
                self.instance = str(ev.get("instance"))
                self.protocol = protocol
                running = self.proc is not None and self.proc.poll() is None
                launched = read_protocol(self.state)
                self.owns = running and bool(self.launch_nonce) and launched.get("nonce") == self.launch_nonce \
                    and launched.get("instance") == self.instance and launched.get("pid") == self.proc.pid
                if self.owns:
                    self.presence.mark_started(self.instance, self.launch_nonce)
                self.send({"op": "identity.ready", "id": self.instance})
            self.locked = ev.get("locked") is True
            if self.locked:
                self.conn = "locked"
            elif "online" in ev:
                self.conn = "online" if ev.get("online") else "connecting"
            self.dirty = True
            return
        if not self.handshake:
            return
        if name == "connection":
            self.conn = "online" if ev.get("state") == "online" else "reconnecting"
        elif name == "friend.list.begin":
            self.friend_generation = str(ev.get("generation") or "")
            self.friend_build = []
        elif name == "friend.info" and self.friend_build is not None and str(ev.get("generation") or "") == self.friend_generation:
            fid = str(ev.get("id") or "")
            self.friend_build.append({"id": fid, "key": str(ev.get("key") or ""),
                                      "name": clean_line(ev.get("name") or "Friend " + fid),
                                      "online": bool(ev.get("online")),
                                      "status": str(ev.get("status") or ("online" if ev.get("online") else "offline"))})
        elif name == "friend.list.end" and self.friend_build is not None and str(ev.get("generation") or "") == self.friend_generation:
            self.apply_friends(self.friend_build)
            self.friend_build = None
        elif name == "identity":
            if ev.get("op") == "unlock":
                self.locked = False
                self.unlock_request = ""
                self.status = ""
                self.secret = ""
                self.handshake_request()
            elif ev.get("op") in ("import", "recovered"):
                self.reset_identity()
        elif name == "error":
            code = str(ev.get("code") or "")
            if code == "locked":
                self.locked = True
                self.conn = "locked"
            if self.unlock_request and str(ev.get("request") or "") == self.unlock_request:
                self.unlock_request = ""
                self.say("The identity passphrase is incorrect.")
            elif code == "identity_changed":
                now = time.monotonic()
                if now > self.rehandshake_at:
                    self.rehandshake_at = now + 2.0
                    self.reset_identity()
            elif code and code != "locked":
                self.say(FAILURES.get(code, code))
        elif name == "unread":
            if self.bound(ev):
                conversation = str(ev.get("conversation") or "")
                try:
                    count = max(0, int(ev.get("count") or 0))
                except (TypeError, ValueError):
                    count = 0
                if count:
                    self.unread[conversation] = count
                else:
                    self.unread.pop(conversation, None)
                self.read_pending.pop(conversation, None)
        elif name == "history":
            conversation = str(ev.get("conversation") or "")
            c = self.convs.get(conversation)
            if c and c["request"] and str(ev.get("request") or "") == c["request"] and self.bound(ev):
                items = ev.get("items") if isinstance(ev.get("items"), list) else []
                c["items"] = [message_from(i) for i in items if isinstance(i, dict)]
                c["items"].extend(m for request, (owner, m) in self.pending.items() if owner == conversation)
                c["loaded"], c["request"] = True, ""
                self.retry_at.pop(conversation, None)
        elif name == "history.failed":
            conversation = str(ev.get("conversation") or "")
            c = self.convs.get(conversation)
            if c and c["request"] and str(ev.get("request") or "") == c["request"]:
                c["request"] = ""
                self.retry_at[conversation] = time.monotonic() + HISTORY_RETRY
                self.say("History could not be loaded; retrying")
        elif name == "message":
            if self.bound(ev):
                self.on_message(ev)
        elif name == "message.failed":
            entry = self.pending.pop(str(ev.get("request") or ""), None)
            if entry:
                entry[1].failed = str(ev.get("code") or "error")
        elif name == "receipt":
            if self.bound(ev):
                c = self.convs.get(str(ev.get("conversation") or ""))
                if c:
                    level = 3 if ev.get("state") == "read" else (2 if ev.get("state") == "delivered" else 0)
                    for m in c["items"]:
                        if m.id and m.id == str(ev.get("id") or "") and m.dir == "out":
                            m.ack = max(m.ack, level)
        elif name == "typing":
            if self.bound(ev):
                conversation = str(ev.get("conversation") or "")
                if is_direct(conversation):
                    self.conv(conversation)["typing_until"] = time.monotonic() + TYPING_EXPIRE if ev.get("typing") else 0.0
        elif name == "helper.shutdown_blocked":
            pass
        self.dirty = True

    def purge(self, fid):
        self.convs.pop(fid, None)
        self.unread.pop(fid, None)
        self.read_pending.pop(fid, None)
        self.retry_at.pop(fid, None)
        if self.typing_on == fid:
            self.typing_on = ""
        for request, (conversation, m) in list(self.pending.items()):
            if conversation == fid:
                m.failed = "identity_changed"
                del self.pending[request]

    def apply_friends(self, friends):
        old = {f["id"]: f["key"] for f in self.friends}
        new = {f["id"]: f["key"] for f in friends}
        by_key = {f["key"]: f["id"] for f in friends if f["key"]}
        moved = {}
        for fid, key in old.items():
            if new.get(fid) != key:
                for pane in self.layout.panes() + [self.scratch]:
                    if pane.conv == fid:
                        self.stash_draft(pane)
                        moved[id(pane)] = by_key.get(key, "")
                self.purge(fid)
        self.friends = friends
        self.friends_ready = True
        for pane in self.layout.panes() + [self.scratch]:
            if id(pane) in moved:
                pane.conv = moved[id(pane)]
                pane.scroll = 0
                self.restore_draft(pane)
        if not self.layout.columns and friends:
            pick = None
            if self.target:
                pick = self.match(self.target)
                if not pick:
                    self.say("No unique friend named %s" % clean_line(self.target))
                self.target = ""
            pane = Pane(pick["id"] if pick else friends[0]["id"])
            self.layout.add(pane)
            self.restore_draft(pane)
        for pane in self.layout.panes():
            if not pane.conv and friends:
                pane.conv = self.next_free("")
                self.restore_draft(pane)
            self.load(pane.conv)

    def match(self, name):
        want = name.casefold().strip()
        if not want:
            return None
        exact = [f for f in self.friends if f["name"].casefold() == want]
        prefix = [f for f in self.friends if f["name"].casefold().startswith(want)]
        return exact[0] if len(exact) == 1 else (prefix[0] if len(prefix) == 1 else None)

    def load(self, conversation):
        if not conversation or not self.handshake:
            return
        c = self.conv(conversation)
        if c["loaded"] or c["request"] or time.monotonic() < self.retry_at.get(conversation, 0.0):
            return
        c["request"] = "cli-history-" + secrets.token_hex(6)
        if not self.send_bound({"op": "history", "conversation": conversation, "limit": HISTORY_LIMIT,
                                "id": c["request"]}):
            c["request"] = ""

    def show(self, conversation):
        pane = self.pane
        if not pane:
            return
        if conversation != pane.conv:
            self.stop_typing()
            self.stash_draft(pane)
            pane.conv = conversation
            self.restore_draft(pane)
        pane.scroll = 0
        self.load(conversation)
        self.dirty = True

    def next_free(self, after, step=1):
        ids = [f["id"] for f in self.friends]
        if not ids:
            return ""
        taken = {p.conv for p in self.layout.panes()} - {after}
        at = ids.index(after) if after in ids else (-1 if step > 0 else 0)
        for k in range(1, len(ids) + 1):
            candidate = ids[(at + step * k) % len(ids)]
            if candidate not in taken:
                return candidate
        return after

    def open_pane(self, conversation, vertical=True):
        if not conversation:
            return
        found = self.layout.find(conversation)
        if found:
            self.layout.col, self.layout.row = found
            self.dirty = True
            return
        self.stop_typing()
        pane = Pane(conversation)
        self.layout.add(pane, vertical)
        self.restore_draft(pane)
        self.load(conversation)
        self.dirty = True

    def on_message(self, ev):
        conversation = str(ev.get("conversation") or "")
        c = self.conv(conversation)
        request = str(ev.get("request") or "")
        if ev.get("dir") == "out" and request in self.pending:
            owner, m = self.pending.pop(request)
            m.id = str(ev.get("id") or m.id)
            m.ack = max(m.ack, 1)
            if ev.get("ts"):
                m.ts = message_from(ev).ts
            return
        m = message_from(ev)
        if m.id and any(x.id == m.id for x in c["items"]):
            return
        c["items"].append(m)
        for pane in self.layout.panes():
            if pane.conv == conversation and m.dir == "in":
                pane.scroll = 0

    def send_text(self):
        pane = self.pane or self.scratch
        text = outgoing(pane.draft)
        if not text.strip():
            return
        if not pane.conv:
            self.say("No chat is open; :connect <name> or :help")
            return
        if len(text.encode("utf-8")) > TEXT_MAX:
            self.say("Message is too long (max %d bytes)" % TEXT_MAX)
            return
        if self.locked or not self.handshake:
            self.say(FAILURES["locked"] if self.locked else "Not connected")
            return
        request = "cli-" + secrets.token_hex(10)
        m = Message("", request, "out", text, time.time(), ack=0)
        self.conv(pane.conv)["items"].append(m)
        self.pending[request] = (pane.conv, m)
        if not self.send_bound({"op": "msg.send", "conversation": pane.conv, "text": text, "reply": "",
                                "id": request}):
            self.pending.pop(request, None)
            m.failed = "error"
        pane.draft, pane.cursor, pane.scroll = "", 0, 0
        self.stop_typing()

    def stop_typing(self):
        if self.typing_on:
            self.send_bound({"op": "typing.set", "conversation": self.typing_on, "typing": False})
        self.typing_on = ""
        self.typing_sent = 0.0

    def tick(self, now):
        if self.status and now > self.status_until:
            self.status = ""
            self.dirty = True
        if self.proc is not None and self.proc.poll() is not None:
            self.proc = None
        if not self.link and now >= self.next_connect:
            self.connect()
        if self.typing_on and now - self.last_edit > TYPING_IDLE:
            self.stop_typing()
        for c in self.convs.values():
            if c["typing_until"] and now > c["typing_until"]:
                c["typing_until"] = 0.0
                self.dirty = True
        if self.handshake and not self.locked:
            for pane in self.layout.panes():
                if pane.conv and not self.convs.get(pane.conv, {}).get("loaded"):
                    self.load(pane.conv)

    def mark_read(self, now):
        if not (self.focused and self.handshake and not self.locked):
            return
        for conversation in self.visible:
            if (self.unread.get(conversation) and self.convs.get(conversation, {}).get("loaded")
                    and now > self.read_pending.get(conversation, 0.0)):
                if self.send_bound({"op": "conversation.read", "conversation": conversation}):
                    self.read_pending[conversation] = now + 5.0

    def command(self, line):
        parts = line[1:].strip().split(None, 1)
        verb = parts[0].lower() if parts else ""
        arg = parts[1].strip() if len(parts) > 1 else ""
        known = ("qa", "qa!", "qall", "qall!", "quitall", "quitall!", "wqa", "xa", "q", "q!", "quit", "quit!",
                 "close", "clo", "help", "h") + NAMED
        if verb and verb not in known:
            candidates = [name for name, _ in VERBS if name.startswith(verb)]
            if len(candidates) == 1:
                verb = candidates[0]
        if verb in ("qa", "qa!", "qall", "qall!", "quitall", "quitall!", "wqa", "xa"):
            self.running = False
        elif verb in ("q", "q!", "quit", "quit!", "close", "clo"):
            if not self.close_pane(quiet=True):
                self.running = False
        elif verb in ("help", "h"):
            self.help = 0
        elif verb in NAMED:
            pick = self.match(arg)
            if not pick:
                self.say("No unique friend named %s" % clean_line(arg) if arg else "Usage: :%s <name>" % verb)
            elif not self.pane:
                pane = Pane(pick["id"])
                self.layout.add(pane)
                self.restore_draft(pane)
                self.load(pick["id"])
            else:
                self.open_pane(pick["id"], vertical=verb not in ("split", "sp"))
        else:
            self.say("Unknown command :%s; :help lists the commands" % clean_line(verb))

    def suggestions(self, draft):
        body = draft[1:]
        if " " not in body:
            want = body.casefold()
            return "verb", body, [name for name, _ in VERBS if name.startswith(want)]
        verb, rest = body.split(" ", 1)
        if verb.lower() not in NAMED:
            return "none", rest, []
        want = rest.casefold()
        return "name", rest, [f["name"] for f in self.friends if f["name"].casefold().startswith(want)]

    def complete(self, pane, step):
        if self.completion is None:
            kind, base, items = self.suggestions(pane.draft)
            if not items:
                if kind == "name":
                    self.say("No friend starts with %s" % clean_line(base))
                self.dirty = True
                return
            verb = pane.draft[1:].split(" ", 1)[0]
            self.completion = {"kind": kind, "verb": verb, "items": items, "index": -1 if step > 0 else 0}
        state = self.completion
        state["index"] = (state["index"] + step) % len(state["items"])
        choice = state["items"][state["index"]]
        pane.draft = (":" + choice) if state["kind"] == "verb" else ":%s %s" % (state["verb"], choice)
        pane.cursor = len(pane.draft)
        self.dirty = True

    def command_hint(self, cols):
        p = self.palette
        pane = self.pane or self.scratch
        if not pane.draft.startswith(":"):
            return p.paint(clip(HINT, cols)[0], "dim")
        if self.completion is not None:
            kind, items, chosen = self.completion["kind"], self.completion["items"], self.completion["index"]
        else:
            kind, _, items = self.suggestions(pane.draft)
            chosen = 0
        if not items:
            text = " no matching friend" if kind == "name" else (" no matching command" if kind == "verb" else HINT)
            return p.paint(clip(text, cols)[0], "dim")
        segs = [(" ", "dim", "")]
        for k, item in enumerate(items):
            label = (":" + item) if kind == "verb" else item
            segs.append((label, "name" if k == chosen else "dim", "1;7" if k == chosen else ""))
            segs.append(("  ", "dim", ""))
        return p.row(segs, cols)

    def help_key(self, kind, value):
        count = len(COMMANDS)
        if kind == "key" and value in ("up", "backtab"):
            self.help = (self.help - 1) % count
        elif kind == "key" and value in ("down", "tab"):
            self.help = (self.help + 1) % count
        elif kind == "text" and value in ("k", "j"):
            self.help = (self.help + (1 if value == "j" else -1)) % count
        elif kind == "key" and value == "enter":
            command, _, needs = COMMANDS[self.help]
            self.help = None
            if needs:
                composer = self.pane or self.scratch
                composer.draft, composer.cursor = command, len(command)
            else:
                self.command(command)
        elif (kind == "key" and value == "esc") or (kind == "text" and value == "q") or kind == "ctrl":
            self.help = None
        self.dirty = True

    def help_rows(self, width, height):
        p = self.palette
        rows = [p.row([(" commands", "name", "1"), ("   ↑↓ select · enter run · esc close", "dim", "")], width)]
        for k, (command, text, needs) in enumerate(COMMANDS):
            chosen = k == self.help
            label = command + ("<name>" if needs else "")
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
        rows = rows[:height]
        rows.extend([" " * width] * (height - len(rows)))
        return rows

    def close_pane(self, quiet=False):
        pane = self.pane
        if pane and self.layout.close():
            self.stash_draft(pane)
            self.stop_typing()
            self.dirty = True
            return True
        if not quiet:
            self.say("This is the last pane; :q or :qa quits")
        self.dirty = True
        return False

    def edit(self, kind, value):
        now = time.monotonic()
        if kind == "key" and value in ("focus-in", "focus-out"):
            self.focused = value == "focus-in"
            self.dirty = True
            return
        if self.locked:
            if kind in ("text", "paste"):
                self.secret += value.replace("\n", "")
            elif kind == "key" and value == "backspace":
                self.secret = self.secret[:-1]
            elif kind == "key" and value == "enter" and self.secret:
                self.unlock_request = "cli-unlock-" + secrets.token_hex(6)
                self.send({"op": "identity.unlock", "passphrase": self.secret, "id": self.unlock_request})
                self.secret = ""
                self.say("Unlocking identity…", 15)
            elif kind == "ctrl" and value in ("c", "d"):
                self.running = False
            elif kind == "key" and value == "esc":
                self.secret = ""
            self.dirty = True
            return
        if self.help is not None:
            self.help_key(kind, value)
            return
        if kind == "ctrl" and value == "c":
            composer = self.pane or self.scratch
            composer.draft, composer.cursor = "", 0
            self.say("Type :qa and press Enter to quit")
            return
        if kind == "key" and value == "f1":
            self.help = 0
            self.dirty = True
            return
        layout_keys = {"alt-left": ("focus", "left"), "alt-right": ("focus", "right"), "alt-up": ("focus", "up"),
                       "alt-down": ("focus", "down"), "alt-shift-left": ("move", "left"),
                       "alt-shift-right": ("move", "right"), "alt-shift-up": ("move", "up"),
                       "alt-shift-down": ("move", "down")}
        alt_keys = {"h": ("focus", "left"), "l": ("focus", "right"), "k": ("focus", "up"), "j": ("focus", "down"),
                    "H": ("move", "left"), "L": ("move", "right"), "K": ("move", "up"), "J": ("move", "down")}
        action = layout_keys.get(value) if kind == "key" else (alt_keys.get(value) if kind == "alt" else None)
        if action:
            if action[0] == "focus":
                before = self.pane
                self.layout.focus(action[1])
                if self.pane is not before:
                    self.stop_typing()
            else:
                self.layout.move(action[1])
            self.dirty = True
            return
        if kind == "alt" and value in ("=", "+", "-"):
            self.layout.resize(0.2 if value != "-" else -0.2)
            self.dirty = True
            return
        if kind == "ctrl" and value == "n":
            conversation = self.next_free("")
            if conversation and not self.layout.find(conversation):
                self.open_pane(conversation)
            else:
                self.say("Every friend is already open")
            return
        if kind == "ctrl" and value == "w":
            self.close_pane()
            return
        pane = self.pane or self.scratch
        if not (kind == "key" and value in ("tab", "backtab")):
            self.completion = None
        if kind in ("text", "paste") or (kind == "key" and value in ("shift-enter", "alt-shift-enter", "alt-enter")):
            insert = "\n" if kind == "key" else value
            pane.draft = pane.draft[:pane.cursor] + insert + pane.draft[pane.cursor:]
            pane.cursor += len(insert)
            self.last_edit = now
            if pane.conv and not pane.draft.startswith(":") and now - self.typing_sent > TYPING_SEND_EVERY:
                if self.send_bound({"op": "typing.set", "conversation": pane.conv, "typing": True}):
                    self.typing_on, self.typing_sent = pane.conv, now
        elif kind == "key":
            if value == "enter":
                if pane.draft.startswith(":"):
                    line, pane.draft, pane.cursor = pane.draft, "", 0
                    self.command(line)
                else:
                    self.send_text()
            elif value == "backspace" and pane.cursor > 0:
                pane.draft = pane.draft[:pane.cursor - 1] + pane.draft[pane.cursor:]
                pane.cursor -= 1
            elif value == "delete" and pane.cursor < len(pane.draft):
                pane.draft = pane.draft[:pane.cursor] + pane.draft[pane.cursor + 1:]
            elif value == "left":
                pane.cursor = max(0, pane.cursor - 1)
            elif value == "right":
                pane.cursor = min(len(pane.draft), pane.cursor + 1)
            elif value == "home":
                pane.cursor = 0
            elif value == "end":
                pane.cursor = len(pane.draft)
            elif value == "pgup":
                pane.scroll += max(1, self.size[1] // 2)
            elif value == "pgdn":
                pane.scroll = max(0, pane.scroll - max(1, self.size[1] // 2))
            elif value in ("tab", "backtab") and pane.draft.startswith(":"):
                self.complete(pane, 1 if value == "tab" else -1)
                return
            elif value in ("tab", "backtab") and self.friends:
                self.show(self.next_free(pane.conv, 1 if value == "tab" else -1))
            elif value == "esc":
                pane.draft, pane.cursor = "", 0
        elif kind == "ctrl":
            if value == "a":
                pane.cursor = 0
            elif value == "e":
                pane.cursor = len(pane.draft)
            elif value == "u":
                pane.draft, pane.cursor = pane.draft[pane.cursor:], 0
        self.dirty = True

    def conn_label(self):
        return {"online": "Online", "locked": "Locked", "reconnecting": "Reconnecting…",
                "helper_down": "Helper not running"}.get(self.conn, "Connecting…")

    def peer_status(self, conversation):
        if self.conn != "online":
            return ("reconnecting…" if self.conn == "reconnecting" else "connecting…"), "dim"
        c = self.convs.get(conversation)
        if c and c["typing_until"]:
            return "typing…", "status"
        f = self.friend(conversation)
        return ("online" if f and f["online"] else "offline"), "status"

    def friends_bar(self, cols):
        shown = {pane.conv for pane in self.layout.panes()}
        bar = [(" omaq ", "accent", "1"), (self.conn_label(), "online" if self.conn == "online" else "dim", ""), ("   ", "dim", "")]
        for f in self.friends:
            role = "online" if f["online"] and f["status"] != "afk" else ("away" if f["online"] else "offline")
            extra = ";".join(x for x in ("1" if f["id"] in shown else "", "4" if self.unread.get(f["id"]) else "") if x)
            bar.append((f["name"], "name", extra))
            bar.append((" ●", role, ""))
            bar.append(("   ", "dim", ""))
        if self.friends_ready and not self.friends:
            bar.append(("no friends yet", "dim", ""))
        return self.palette.row(bar, cols)

    def tab_label(self, pane):
        f = self.friend(pane.conv)
        return f["name"] if f else (pane.conv or "·")

    def header(self, pane, width, focused, tabs=None):
        if tabs:
            label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
            room = width - text_width(label) - 3
            names = [("[%s]" % self.tab_label(t)) if t is pane else self.tab_label(t) for t in tabs]
            start = tabs.index(pane) if pane in tabs else 0
            if sum(text_width(n) + 2 for n in names) <= room:
                start = 0
            segs = [(" ‹ " if start else " ", "dim", "")]
            used = text_width(segs[0][0])
            for k in range(start, len(tabs)):
                piece = names[k] + "  "
                if used + text_width(piece) > room and k != start:
                    segs.append(("› ", "dim", ""))
                    break
                segs.append((piece, "name", "1" if tabs[k] is pane else ""))
                used += text_width(piece)
            segs.append((label, role, ""))
            return self.palette.row(segs, width)
        f = self.friend(pane.conv)
        label, role = self.peer_status(pane.conv) if pane.conv else ("", "dim")
        segs = [("▸ " if focused else "  ", "accent", "1"), ("chat › ", "dim", ""),
                (f["name"] if f else (pane.conv or "no chat"), "name", "1" if focused else ""), ("  " + label, role, "")]
        return self.palette.row(segs, width)

    def pane_rows(self, pane, width, height, focused, tabs=None):
        rows = [self.header(pane, width, focused, tabs)]
        body = height - 1
        if body > 0 and pane.conv:
            self.visible.add(pane.conv)
        lines = self.message_lines(pane.conv, width)
        pane.scroll = min(pane.scroll, max(0, len(lines) - body))
        end = len(lines) - pane.scroll
        visible = lines[max(0, end - body):end] if body > 0 else []
        rows.extend([" " * width] * (body - len(visible)))
        rows.extend(self.palette.row(segs, width) for segs in visible)
        return rows

    def render(self):
        cols, rows = self.size
        p = self.palette
        out = [self.friends_bar(cols), p.paint("─" * cols, "rule")]
        area = max(1, rows - 5)
        self.visible = set()
        if self.locked:
            out.append(p.row([(" Identity is locked", "unread", "")], cols))
            out.extend([" " * cols] * (area - 1))
        elif self.help is not None:
            out.extend(self.help_rows(cols, area))
        elif not self.layout.columns:
            out.append(p.row([(" " + self.conn_label(), "dim", "")], cols))
            out.extend([" " * cols] * (area - 1))
        else:
            boxes = self.layout.geometry(cols, area)
            focused = self.pane
            if boxes is None:
                out.extend(self.pane_rows(focused, cols, area, True, tabs=self.layout.panes()))
            else:
                widths = {}
                canvas = [[] for _ in range(area)]
                for c, r, pane, x, y, w, h in boxes:
                    widths[x] = w
                    for k, line in enumerate(self.pane_rows(pane, w, h, pane is focused)):
                        canvas[y + k].append((x, line))
                    if r < len(self.layout.columns[c]) - 1:
                        canvas[y + h].append((x, p.paint("─" * w, "rule")))
                for y in range(area):
                    line, at = [], 0
                    for x, text in sorted(canvas[y], key=lambda item: item[0]):
                        if x > at:
                            line.append(p.paint("│", "rule") + " " * (x - at - 1))
                        line.append(text)
                        at = x + widths[x]
                    out.append("".join(line))
        hint = self.status or ""
        rule = cols - text_width(hint) - 3 if hint else cols
        out.append(p.paint("─" * max(0, rule), "rule") + (p.paint(" " + clip(hint, cols - 2)[0] + " ", "unread") if hint else ""))
        if self.locked:
            prompt = " passphrase › "
            shown = "•" * len(self.secret)
            out.append(p.paint(prompt, "accent") + p.paint(clip(shown, max(0, cols - len(prompt) - 1))[0], "text"))
            cursor_col = min(cols, len(prompt) + len(shown) + 1)
        else:
            pane = self.pane or self.scratch
            f = self.friend(pane.conv)
            prompt = clip(" %s › " % (f["name"] if f else ""), max(4, cols // 3))[0]
            room = max(1, cols - text_width(prompt) - 1)
            shown = clean(pane.draft).replace("\n", "↵")
            start = 0
            while text_width(shown[start:pane.cursor]) > room - 1:
                start += 1
            out.append(p.paint(prompt, "accent") + p.paint(clip(shown[start:], room)[0], "text"))
            cursor_col = text_width(prompt) + text_width(shown[start:pane.cursor]) + 1
        if self.locked:
            out.append(p.paint(clip(" enter unlock · esc clear · ctrl+c quit", cols)[0], "dim"))
        else:
            out.append(self.command_hint(cols))
        frame = ["\x1b[?2026h\x1b[?25l"]
        for i, line in enumerate(out[:rows]):
            frame.append("\x1b[%d;1H%s\x1b[0m\x1b[K" % (i + 1, line))
        frame.append("\x1b[%d;%dH\x1b[?25h\x1b[?2026l" % (max(1, min(rows, len(out)) - 1), max(1, cursor_col)))
        return "".join(frame)

    def message_lines(self, conversation, cols):
        c = self.convs.get(conversation)
        if not c:
            return []
        if not c["loaded"] and not c["items"]:
            return [[("   loading…", "dim", "")]]
        f = self.friend(conversation)
        peer = f["name"] if f else "peer"
        today = time.strftime("%Y-%m-%d")
        lines = []
        for m in c["items"]:
            when = stamp(m.ts, today) or "-"
            who = "you" if m.dir == "out" else ("·" if m.dir == "sys" else (clean_line(m.sender) or peer))
            who = shorten(who, max(4, cols // 3))
            who_role = "text" if m.dir == "out" else ("dim" if m.dir == "sys" else "name")
            pw = text_width(" %s  %s › " % (when, who))
            narrow = cols - pw - 1 < NARROW_BODY
            pad = 3 if narrow else pw
            body = wrap(clean(m.text), max(1, cols - pad - 1))
            extra = ""
            if m.dir == "out" and m.failed:
                label, role = "✕ " + FAILURES.get(m.failed, "Message was not sent"), "unread"
            elif m.dir == "out":
                label = RECEIPTS[min(m.ack, 3)]
                role, extra = ("online", "1") if m.ack >= 3 else ("dim", "")
            else:
                label, role = "", "dim"
            if narrow:
                lines.append([(" %s  " % when, "dim", ""), (who, who_role, ""), (" ›", "dim", "")])
            for k, part in enumerate(body):
                segs = []
                if k == 0 and not narrow:
                    segs.append((" %s  " % when, "dim", ""))
                    segs.append((who, who_role, ""))
                    segs.append((" › ", "dim", ""))
                else:
                    segs.append((" " * pad, "dim", ""))
                segs.append((part, "dim" if m.dir == "sys" else "text", ""))
                if label and k == len(body) - 1:
                    if pad + text_width(part) + 2 + text_width(label) <= cols:
                        segs.append(("  ", "dim", ""))
                        segs.append((label, role, extra))
                    else:
                        lines.append(segs)
                        segs = [(" " * pad, "dim", ""), (label, role, extra)]
                lines.append(segs)
        if not c["items"]:
            lines.append([("   no messages yet", "dim", "")])
        return lines

    def exit_helper(self):
        if not self.link or not self.handshake or not self.instance:
            return
        if self.presence.others_alive() or not self.owned_by_cli():
            return
        if self.bar_attached():
            self.exit_note = "%s: OmaQ keeps running for the bar" % COMMAND
            return
        request = "cli-exit-" + secrets.token_hex(8)
        if not self.send({"op": "helper.shutdown_if_no_groups", "id": self.instance, "request": request}):
            return
        deadline = time.monotonic() + 6.0
        while self.link and time.monotonic() < deadline:
            ready, _, _ = select.select([self.link], [], [], 0.2)
            if not ready:
                continue
            try:
                events = self.link.read()
            except (ConnectionError, OSError):
                break
            for ev in events:
                if ev.get("event") == "helper.shutdown" and str(ev.get("request") or "") == request:
                    self.presence.clear_started()
                    return
                if ev.get("event") == "helper.shutdown_blocked" and str(ev.get("request") or "") == request:
                    if ev.get("reason") == "active_groups":
                        self.exit_note = ("%s: OmaQ stays online because you are in groups; closing it could "
                                          "lose a private group membership" % COMMAND)
                    else:
                        self.exit_note = ("%s: OmaQ stays online because its group state is uncertain; closing it "
                                          "could lose a private group membership" % COMMAND)
                    return

    def cleanup(self, fd, saved, out):
        blocked = {signal.SIGTERM, signal.SIGHUP, signal.SIGINT}
        signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        try:
            try:
                self.stop_typing()
                self.exit_helper()
            except BaseException:
                pass
            try:
                if self.link:
                    self.link.close()
                self.presence.release()
            except BaseException:
                pass
        finally:
            try:
                termios.tcsetattr(fd, termios.TCSADRAIN, saved)
                out.write("\x1b[?2026l\x1b[0m\x1b[?2004l\x1b[?1004l\x1b[?25h\x1b[?1049l")
                out.flush()
                if self.exit_note:
                    print(self.exit_note, file=sys.stderr)
            except (OSError, ValueError):
                pass
            signal.pthread_sigmask(signal.SIG_UNBLOCK, blocked)

    def run(self):
        fd = sys.stdin.fileno()
        saved = termios.tcgetattr(fd)
        out = sys.stdout
        for sig in (signal.SIGTERM, signal.SIGHUP):
            signal.signal(sig, stop)
        out.write("\x1b[?1049h\x1b[?1004h\x1b[?2004h")
        out.flush()
        try:
            tty.setraw(fd)
            size = os.get_terminal_size(fd)
            self.size = (size.columns, size.lines)
            out.write(self.render())
            out.flush()
            self.connect()
            while self.running:
                now = time.monotonic()
                self.tick(now)
                size = os.get_terminal_size(fd)
                if (size.columns, size.lines) != self.size:
                    self.size = (size.columns, size.lines)
                    self.dirty = True
                if self.dirty:
                    out.write(self.render())
                    out.flush()
                    self.dirty = False
                self.mark_read(now)
                watch = [fd] + ([self.link] if self.link else [])
                try:
                    ready, _, _ = select.select(watch, [], [], 0.25)
                except InterruptedError:
                    continue
                if fd in ready:
                    data = os.read(fd, 4096)
                    if not data:
                        break
                    for kind, value in self.keys.feed(data):
                        self.edit(kind, value)
                    if self.keys.pending:
                        more, _, _ = select.select([fd], [], [], 0.03)
                        if not more:
                            for kind, value in self.keys.feed(b"", final=True):
                                self.edit(kind, value)
                if self.link and self.link in ready:
                    try:
                        events = self.link.read()
                    except (ConnectionError, OSError):
                        self.drop_link()
                        events = []
                    for ev in events:
                        self.on_event(ev)
        finally:
            self.cleanup(fd, saved, out)


def stop(signum, frame):
    raise SystemExit(0)


def main(argv):
    if any(a in ("-h", "--help") for a in argv):
        print("usage: %s [friend]\n       %s --link | --unlink\n\n"
              "Open the OmaQ chat in this terminal, optionally with that friend.\n"
              "--link creates ~/.local/bin/%s; --unlink removes it if it is OmaQ's." % (COMMAND, COMMAND, COMMAND))
        return 0
    if argv == ["--link"]:
        return install_link()
    if argv == ["--unlink"]:
        return remove_link()
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print("%s needs an interactive terminal" % COMMAND, file=sys.stderr)
        return 2
    try:
        App(" ".join(argv).strip()).run()
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
