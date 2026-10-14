import os
import secrets
import select
import signal
import subprocess
import sys
import termios
import time
import tty
from operator import attrgetter

from .text import clean_line, emojify, is_command, outgoing, shorten
from .protocol import FAILURES, HISTORY_ITEM_FAILED, ONE_LINE, PROTOCOL_MIN, TEXT_MAX, is_direct, secret_edit
from .session import bound, friend_of, key_for, rebound
from .chat import Chat
from .files import FileTransfers
from .invites import Invites
from .profile import Profile
from .identity import Identity
from .commands import TIPS, Completion, parse_command, suggestions, typed_path
from .view import Screen
from .modals import OVERLAYS, Confirmation, FilePicker, Help, InviteOverlay, Picker, SecretPrompt
from .palette import Palette, theme_stamp
from .keys import LAYOUT_KEYS, Keys
from .link import Link
from .helper_process import HelperProcess, default_state, omaq_paths, read_protocol
from .presence import Presence
from .terminal import measure_widths, pixel_size
from .system import BAR_PROBE, qr_lines
from .layout import Layout, Pane


TYPING_SEND_EVERY = 3.0
TYPING_IDLE = 3.0
STATUS_SECONDS = 6.0
COMMAND = "omaq-connect"
CONFIRM_SECONDS = 15
HUSH_SECONDS = 0.3
TIP_SECONDS = 8
ZOOM_SECONDS = 4
THEME_RETRIES = 3
EXIT_WAIT = 20.0
SETTLE_SECONDS = 5.0
UNCONFIRMED_EXIT = "could not confirm that OmaQ went offline; it may still be running"
UNCOORDINATED_EXIT = "another omaq-connect is still quitting; OmaQ may still be running"


EVENT_HANDLERS = {
    "connection": "_ev_connection",
    "friend.list.begin": "_ev_friend_list_begin",
    "friend.info": "_ev_friend_info",
    "friend.list.end": "_ev_friend_list_end",
    "identity": "_ev_identity",
    "error": "_ev_error",
    "unread": "chat.on_unread",
    "history": "_ev_history",
    "history.failed": "chat.on_history_failed",
    "message": "_ev_message",
    "message.failed": "chat.on_message_failed",
    "message.updated": "_ev_message_updated",
    "receipt": "chat.on_receipt",
    "typing": "chat.on_typing",
    "invite": "invites.on_invite",
    "file.offer": "files.on_offer",
    "file.sending": "files.on_sending",
    "file.progress": "files.on_progress",
    "file.done": "files.on_result",
    "file.failed": "files.on_result",
    "file.canceled": "files.on_result",
    "contact.decided": "invites.on_decided",
    "file.state": "files.on_state",
    "invite.redeemed": "invites.on_redeemed",
    "request": "invites.on_request",
    "request.conflict": "invites.on_conflict",
    "nickname": "profile.on_nickname",
    "history.clear.succeeded": "_ev_history_clear_result",
    "history.clear.failed": "_ev_history_clear_result",
}

COMMAND_HANDLERS = {
    "q": "_cmd_quit",
    "q!": "_cmd_quit",
    "quit": "_cmd_quit",
    "quit!": "_cmd_quit",
    "close": "_cmd_close",
    "clo": "_cmd_close",
    "invite": "_cmd_invite",
    "redeem": "_cmd_redeem",
    "nickname": "_cmd_nickname",
    "requests": "_cmd_requests",
    "accept": "_cmd_decide",
    "decline": "_cmd_decide",
    "sendfile": "_cmd_sendfile",
    "acceptfile": "_cmd_offer",
    "declinefile": "_cmd_offer",
    "cancelfile": "_cmd_cancelfile",
    "clear": "_cmd_clear",
    "reply": "_cmd_reply",
    "identity": "_cmd_identity",
    "help": "_cmd_help",
    "h": "_cmd_help",
    "connect": "_cmd_named",
    "c": "_cmd_named",
    "vsplit": "_cmd_named",
    "vs": "_cmd_named",
    "split": "_cmd_named",
    "sp": "_cmd_named",
}


class App:
    def __init__(self, target):
        self.home, self.state = omaq_paths()
        self.target = target
        self.theme_stamp, self.theme_check, self.theme_misses = theme_stamp(), 0.0, 0
        self.palette = Palette()
        self.presence = Presence(self.state)
        self.helper = HelperProcess(self.presence, self.say)
        self.link = None
        self.open_link = Link
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
        self.drafts = {}
        self.layout = Layout()
        self.secret = ""
        self.unlock_request = ""
        self.more_input = self.hush = False
        self.pastes_marked = self.gate = False
        self.status = ""
        self.status_until = 0.0
        self.status_set, self.status_seconds = 0.0, STATUS_SECONDS
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
        self.commands = []
        self.out = sys.stdout
        self.invites = Invites(self.send, self.say, self.send_failed, self.show_invite, self.open_overlay,
                               self.close_overlays)
        self.overlay = None
        self.picker = None
        self.file_picker = None
        self.addr = ""
        self.cell_base, self.zoom, self.zoom_until = 0, None, 0.0
        self.screen = Screen(self)
        self.chat = Chat(self.send, self.send_bound, self.say, lambda: self.friends, lambda: self.handshake)
        self.profile = Profile(self.send, self.say, self.send_failed)
        self.secret_prompt = None
        self.identity = Identity(self.identity_send, self.say, self.ask_secret, self.ask_identity, self.send_failed)
        self.files = FileTransfers(self.send, self.say, self.send_failed, lambda: self.friends, self.chat.reload)
        self.tip, self.tip_at = secrets.randbelow(len(TIPS)), time.monotonic() + TIP_SECONDS
        self.confirm = None
        self.history_clear = 0

    @property
    def pane(self):
        return self.layout.focused()

    @property
    def current(self):
        return self.pane.conv if self.pane else ""

    def say(self, text, seconds=STATUS_SECONDS):
        self.status = clean_line(text)
        self.status_until = time.monotonic() + seconds
        self.status_set, self.status_seconds = time.monotonic(), seconds
        self.dirty = True

    def friend(self, fid):
        return friend_of(self.friends, fid)

    def key_for(self, conversation):
        return key_for(self.friends, conversation)

    def bound(self, ev):
        return bound(self.friends, ev)

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
            self.link = self.open_link(path)
        except OSError:
            self.link = None
        if not self.link and self.helper.proc is None and (self.helper.owns or not self.instance):
            self.helper.start(self.home, self.state)
            deadline = time.monotonic() + 10.0
            while self.helper.proc is not None and time.monotonic() < deadline and not self.link:
                if self.helper.failed():
                    self.helper.abandon()
                    break
                try:
                    self.link = self.open_link(path)
                except OSError:
                    time.sleep(0.1)
        if not self.link:
            self.conn = "helper_down"
            self.next_connect = time.monotonic() + 2.0
            self.dirty = True
            return
        self.handshake_request()
        self.dirty = True

    def drop_link(self):
        if self.link:
            self.link.close()
        self.link = None
        if self.confirm is not None:
            self.drop_confirm()
        self.handshake = False
        self.friend_build = None
        self.conn = "reconnecting"
        self.next_connect = time.monotonic() + 1.0
        self.chat.drop()
        self.overlay = None
        self.invites.drop()
        self.profile.drop()
        self.hush_secret()
        self.secret, self.unlock_request, self.secret_prompt = "", "", None
        self.identity.drop()
        self.dirty = True

    def reset_identity(self):
        self.chat.reset()
        self.friends, self.friend_build, self.friends_ready = [], None, False
        self.drafts = {}
        if self.confirm is not None:
            self.drop_confirm()
        self.overlay = None
        self.invites.reset()
        self.files.reset()
        self.profile.reset()
        self.hush_secret()
        self.secret, self.unlock_request, self.secret_prompt = "", "", None
        self.identity.reset()
        self.typing_on = ""
        self.commands = []
        for pane in self.layout.panes() + [self.scratch]:
            pane.conv, pane.draft, pane.cursor, pane.scroll = "", "", 0, 0
            pane.forget_recall()
            pane.forget_reply()
        self.handshake_request()

    def drop_confirm(self):
        notice = self.notice_behind(self.confirm)
        kept = self.confirm.kept
        self.confirm = None
        self.say(kept)
        self.show_notice(notice)

    def notice_behind(self, confirm):
        if self.status and self.status_set > confirm.at:
            return self.status, self.status_seconds
        return None

    def show_notice(self, notice):
        if not notice:
            return
        text, seconds = notice
        if self.status and self.status != text:
            self.say("%s \u00b7 %s" % (self.status, text), max(STATUS_SECONDS, seconds))
        else:
            self.say(text, seconds)

    def resume_transfers(self, restarted):
        self.files.resume(restarted)

    def settle(self):
        if self.handshake or not self.helper.running():
            return
        end = time.monotonic() + SETTLE_SECONDS
        path = os.path.join(self.state, "omaq.sock")
        while not self.link and time.monotonic() < end:
            try:
                self.link = self.open_link(path)
            except OSError:
                time.sleep(0.1)
        if not self.link:
            return
        self.handshake_request()
        while self.link and not self.handshake and time.monotonic() < end:
            ready, _, _ = select.select([self.link], [], [], 0.2)
            if not ready:
                continue
            try:
                events = self.link.read()
            except (ConnectionError, OSError):
                return
            for ev in events:
                if ev.get("event") == "snapshot" and not self.handshake:
                    self.on_event(ev)

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
            self._ev_snapshot(ev)
            return
        if not self.handshake:
            return
        handler = EVENT_HANDLERS.get(name) if isinstance(name, str) else None
        if handler:
            attrgetter(handler)(self)(ev)
        self.dirty = True

    def purge(self, fid):
        if self.typing_on == fid:
            self.typing_on = ""
        if self.confirm is not None and self.confirm.conv == fid:
            self.drop_confirm()
        self.chat.forget(fid)
        self.files.forget(fid)
        for pane in self.layout.panes() + [self.scratch]:
            if pane.conv == fid:
                pane.forget_reply()

    def apply_friends(self, friends):
        moved = {}
        for fid, target in rebound(self.friends, friends):
            for pane in self.layout.panes() + [self.scratch]:
                if pane.conv == fid:
                    self.stash_draft(pane)
                    moved[id(pane)] = target
            self.purge(fid)
        self.friends = friends
        self.friends_ready = True
        for pane in self.layout.panes() + [self.scratch]:
            if id(pane) in moved:
                pane.conv = moved[id(pane)]
                pane.scroll = 0
                pane.forget_recall()
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
            self.chat.load(pane.conv)

    def match(self, name):
        want = name.casefold().strip()
        if not want:
            return None
        exact = [f for f in self.friends if f["name"].casefold() == want]
        prefix = [f for f in self.friends if f["name"].casefold().startswith(want)]
        return exact[0] if len(exact) == 1 else (prefix[0] if len(prefix) == 1 else None)

    def show(self, conversation):
        pane = self.pane
        if not pane:
            return
        if conversation != pane.conv:
            self.stop_typing()
            self.stash_draft(pane)
            pane.conv = conversation
            pane.forget_reply()
            self.restore_draft(pane)
        pane.scroll = 0
        self.chat.load(conversation)
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
        self.chat.load(conversation)
        self.dirty = True

    def send_text(self):
        pane = self.pane or self.scratch
        text = outgoing(emojify(pane.draft))
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
        self.chat.send_message(pane.conv, text, pane.reply)
        pane.draft, pane.cursor, pane.scroll, pane.reply = "", 0, 0, ""
        self.stop_typing()

    def take_input(self, events):
        later, flags = False, []
        for kind, value in reversed(events):
            flags.append(later)
            later = later or not (kind == "key" and value in ("enter", "focus-in", "focus-out"))
        for (kind, value), more in zip(events, reversed(flags)):
            focus = kind == "key" and value in ("focus-in", "focus-out")
            if self.hush and not focus:
                continue
            if self.gated() and not focus:
                if kind == "key" and value == "esc":
                    self.gate, self.dirty = False, True
                continue
            self.more_input = more
            self.edit(kind, value)
        self.more_input = False

    def discard_burst(self, fd):
        self.hush = False
        keys = self.keys
        if self.secret_prompt is not None or self.locked:
            bound = keys.skip
            quiet = keys.paste is None and bool(keys.pending) and not select.select([fd], [], [], 0.03)[0]
            keys.drop_current(quiet and keys.pending == b"\x1b")
            if quiet and not bound:
                keys.hold()
            return True
        while select.select([fd], [], [], HUSH_SECONDS)[0]:
            data = os.read(fd, 4096)
            if not data:
                return False
            for kind, value in keys.feed(data):
                if kind == "key" and value in ("focus-in", "focus-out"):
                    self.edit(kind, value)
        keys.drop_current(True)
        self.gate = self.gate or not (self.pastes_marked or keys.marked)
        return True

    def take_read(self, fd):
        data = os.read(fd, 4096)
        if not data:
            return False
        self.take_input(self.keys.feed(data))
        if self.keys.pending and self.keys.paste is None and not self.hush and not self.keys.skip:
            more, _, _ = select.select([fd], [], [], 0.03)
            if not more and (self.secret_prompt is not None or self.locked):
                self.keys.hold()
            if not more and not self.keys.held:
                self.take_input(self.keys.feed(b"", final=True))
        return True

    def gated(self):
        return self.gate and self.secret_prompt is None and not self.locked

    def hush_secret(self):
        if self.secret_prompt is not None:
            self.hush = True

    def identity_send(self, obj):
        return self.handshake and not self.locked and self.send(obj)

    def ask_secret(self, label, purpose):
        if self.confirm is not None:
            self.drop_confirm()
        self.secret_prompt = SecretPrompt(label, purpose)
        self.picker = self.file_picker = self.help = self.overlay = None
        if self.pane is not None:
            self.pane.select = None
        self.dirty = True

    def close_secret(self):
        if self.secret_prompt is not None:
            self.keys.drop_current()
        self.secret_prompt = None
        self.identity.forget_secret()

    def ask_identity(self, action, prompt, path, secret=""):
        now = time.monotonic()
        self.confirm = Confirmation(action, prompt, now, now + CONFIRM_SECONDS, instance=self.instance, path=path,
                                    secret=secret)
        self.dirty = True

    def begin_select(self):
        pane = self.pane
        if not pane or not pane.conv:
            self.say("No chat is open")
            return
        targets = self.chat.targets(pane.conv)
        if not targets:
            self.say("No message to reply to")
            return
        pane.select = targets[-1].id
        self.dirty = True

    def select_key(self, kind, value):
        pane = self.pane
        ids = [m.id for m in self.chat.targets(pane.conv)]
        at = ids.index(pane.select) if pane.select in ids else len(ids) - 1
        steps = {"up": -1, "down": 1, "pgup": -5, "pgdn": 5, "home": -len(ids), "end": len(ids)}
        step = steps.get(value) if kind == "key" else (-1 if kind == "ctrl" and value == "r" else None)
        self.dirty = True
        if not ids:
            pane.select = None
            self.say("No message to reply to")
            return kind == "key" and value == "esc"
        if step is not None:
            pane.select = ids[max(0, min(len(ids) - 1, at + step))]
        elif (kind == "key" and value == "enter") or (kind == "text" and value == "r"):
            pane.reply, pane.select = ids[at], None
        elif kind == "key" and value == "esc":
            pane.select = None
        else:
            pane.select = None
            return False
        return True

    def stop_typing(self):
        if self.typing_on:
            self.send_bound({"op": "typing.set", "conversation": self.typing_on, "typing": False})
        self.typing_on = ""
        self.typing_sent = 0.0

    def tick(self, now):
        if self.status and now > self.status_until and self.confirm is None:
            self.status = ""
            self.dirty = True
        if self.confirm is not None and self.confirm.expired(now):
            self.drop_confirm()
        self.profile.tick(now)
        self.identity.tick(now)
        if now >= self.theme_check:
            self.theme_check = now + 1.0
            stamp = theme_stamp()
            if stamp == self.theme_stamp:
                self.theme_misses = 0
            else:
                palette = Palette()
                if palette.loaded or self.theme_misses + 1 >= THEME_RETRIES:
                    self.theme_stamp, self.palette, self.theme_misses = stamp, palette, 0
                    self.dirty = True
                else:
                    self.theme_misses += 1
        if self.zoom_until and now >= self.zoom_until:
            self.zoom_until = 0.0
            self.dirty = True
        if now >= self.tip_at:
            self.tip_at = now + TIP_SECONDS
            if self.focused and not is_command((self.pane or self.scratch).draft):
                self.tip = (self.tip + 1) % len(TIPS)
                self.dirty = True
        if self.overlay is not None and self.overlay.tick():
            self.dirty = True
        self.helper.reap()
        if not self.link and now >= self.next_connect:
            self.connect()
        if self.typing_on and now - self.last_edit > TYPING_IDLE:
            self.stop_typing()
        if self.chat.expire_typing(now):
            self.dirty = True
        if self.handshake and not self.locked:
            for pane in self.layout.panes():
                self.chat.load(pane.conv)

    def mark_read(self, now):
        if not (self.focused and self.handshake and not self.locked):
            return
        self.chat.mark_read(self.visible, now)

    def command(self, line):
        verb, arg = parse_command(line)
        handler = COMMAND_HANDLERS.get(verb)
        if handler:
            getattr(self, handler)(verb, arg)
        else:
            self.say('Unknown command :%s; :help lists the commands' % clean_line(verb))

    def track_zoom(self, size):
        if not size or not size[0] or not size[3] or size[3] < size[0]:
            return
        cell = size[3] // size[0]
        self.cell_base = self.cell_base or cell
        zoom = round(cell * 100 / self.cell_base)
        if zoom != self.zoom:
            if self.zoom is not None:
                self.zoom_until = time.monotonic() + ZOOM_SECONDS
            self.zoom = zoom
            self.dirty = True

    def decide_failure(self, code):
        return self.invites.decide_failure(code)

    def send_failed(self):
        self.say("Not connected" if not self.link else "Could not send the request")

    def open_overlay(self, kind, **fields):
        self.overlay = OVERLAYS[kind](**fields)

    def close_overlays(self, *kinds):
        if self.overlay is not None and self.overlay.kind in kinds:
            self.overlay = None

    def show_invite(self, copy=False):
        url = self.invites.invite.url
        if not url:
            return
        self.overlay = InviteOverlay(url, qr_lines(url))
        if copy:
            import base64
            try:
                self.out.write("\x1b]52;c;%s\x07" % base64.b64encode(url.encode("utf-8")).decode("ascii"))
                self.out.flush()
                self.say("Invite link sent to the clipboard", 10)
            except (OSError, ValueError):
                pass
        self.dirty = True

    def overlay_rows(self, width, height):
        return self.screen.overlay_rows(width, height)

    def window(self, rows, width, height, top, keep=None):
        return self.screen.window(rows, width, height, top, keep)

    def picker_items(self):
        return self.picker.items()

    def complete(self, pane, step):
        if self.completion is None:
            kind, base, items = suggestions(pane.draft, [f["name"] for f in self.friends])
            if not items:
                if kind == "name":
                    self.say("No friend starts with %s" % clean_line(base))
                self.dirty = True
                return
            verb = pane.draft[1:].split(" ", 1)[0]
            self.completion = Completion(kind, verb, items, -1 if step > 0 else 0)
        pane.draft = self.completion.step(step)
        pane.cursor = len(pane.draft)
        self.dirty = True

    def command_hint(self, cols):
        return self.screen.command_hint(cols)

    def close_pane(self, quiet=False):
        pane = self.pane
        if pane and self.layout.close():
            self.stash_draft(pane)
            self.stop_typing()
            self.dirty = True
            return True
        if not quiet:
            self.say("This is the last pane; :quit quits OmaQ")
        self.dirty = True
        return False

    def edit(self, kind, value):
        now = time.monotonic()
        if kind == "key" and value in ("focus-in", "focus-out"):
            self.focused = value == "focus-in"
            self.dirty = True
            return
        if self.locked:
            closing = (kind == "key" and value in ("enter", "esc")) or (kind == "ctrl" and value in ("c", "d"))
            if closing and self.more_input:
                self.secret, self.hush = "", True
                self.say(ONE_LINE)
            elif kind == "key" and value == "enter" and self.secret:
                request = "cli-unlock-" + secrets.token_hex(6)
                if self.send({"op": "identity.unlock", "passphrase": self.secret, "id": request}):
                    self.unlock_request = request
                    self.say("Unlocking identity…", 15)
                else:
                    self.say("Not connected")
                self.secret, self.hush = "", True
            elif kind == "ctrl" and value in ("c", "d"):
                self.running = False
            elif kind == "key" and value == "esc":
                self.secret = ""
            else:
                self.secret, problem = secret_edit(self.secret, kind, value)
                if problem:
                    self.say(problem)
            self.dirty = True
            return
        if self.secret_prompt is not None:
            self.secret_prompt.handle(self, kind, value)
            return
        if self.help is not None:
            self.help.handle(self, kind, value)
            return
        if self.confirm is not None and self.confirm.expired(now):
            self.drop_confirm()
        if self.confirm is not None and kind in ("text", "key", "ctrl", "paste", "alt"):
            confirm, self.confirm = self.confirm, None
            notice = self.notice_behind(confirm)
            yes = kind == "text" and value.lower() == "y"
            if confirm.action == "clear":
                bound = (self.handshake and confirm.instance == self.instance and bool(confirm.key)
                         and confirm.key == self.key_for(confirm.conv))
                if yes and bound:
                    self.chat.clear(confirm.conv, confirm.key, self.instance)
                else:
                    self.say("Chat kept")
            elif confirm.action == "revoke":
                if yes:
                    self.invites.revoke()
                else:
                    self.say("Invite kept")
            else:
                bound = self.handshake and confirm.instance == self.instance
                if yes and bound:
                    self.identity.confirmed(confirm.action, confirm.path, confirm.secret)
                else:
                    self.say(confirm.kept)
            self.show_notice(notice)
            self.dirty = True
            return
        if self.overlay is not None:
            self.picker = self.file_picker = None
        if self.picker is not None:
            self.picker.handle(self, kind, value)
            return
        if self.file_picker is not None and self.file_picker.handle(self, kind, value):
            return
        if self.overlay is not None and self.overlay.handle(self, kind, value):
            return
        if self.pane is not None and self.pane.select is not None and self.select_key(kind, value):
            return
        if kind == "ctrl" and value == "r":
            self.begin_select()
            return
        if kind == "ctrl" and value == "c":
            composer = self.pane or self.scratch
            composer.draft, composer.cursor = "", 0
            self.say("Type :quit and press Enter to quit")
            return
        if kind == "key" and value == "f1":
            self.help = Help()
            self.dirty = True
            return
        action = LAYOUT_KEYS.get((kind, value))
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
        if kind == "ctrl" and value == "k" and self.overlay is None:
            self.picker = Picker()
            self.dirty = True
            return
        pane = self.pane or self.scratch
        if not (kind == "key" and value in ("tab", "backtab")):
            self.completion = None
        if not (kind == "key" and value in ("up", "down")):
            pane.hist, pane.hist_items = None, None
        if kind in ("text", "paste") or (kind == "key" and value in ("shift-enter", "alt-shift-enter", "alt-enter")):
            pane.insert("\n" if kind == "key" else value)
            self.last_edit = now
            if pane.conv and not is_command(pane.draft) and now - self.typing_sent > TYPING_SEND_EVERY:
                if self.send_bound({"op": "typing.set", "conversation": pane.conv, "typing": True}):
                    self.typing_on, self.typing_sent = pane.conv, now
        elif kind == "key" and value == "enter":
            self.overlay = None
            if is_command(pane.draft):
                line, pane.draft, pane.cursor = pane.draft, "", 0
                if line.strip() != ":" and (not self.commands or self.commands[-1] != line):
                    self.commands = (self.commands + [line])[-100:]
                self.command(line)
            else:
                self.send_text()
        elif kind == "key" and value in ("up", "down"):
            source = lambda: self.commands[::-1] if pane.draft.startswith(":") else self.chat.sent_texts(pane.conv)
            if pane.recall(1 if value == "up" else -1, source):
                self.dirty = True
            return
        elif kind == "key" and value == "pgup":
            pane.scroll += max(1, self.size[1] // 2)
        elif kind == "key" and value == "pgdn":
            pane.scroll = max(0, pane.scroll - max(1, self.size[1] // 2))
        elif kind == "key" and value == "tab" and self.overlay is None and typed_path(pane.draft) is not None:
            self.file_picker = FilePicker()
        elif kind == "key" and value in ("tab", "backtab") and is_command(pane.draft):
            self.complete(pane, 1 if value == "tab" else -1)
            return
        elif kind == "key" and value in ("tab", "backtab") and self.friends:
            self.show(self.next_free(pane.conv, 1 if value == "tab" else -1))
        elif kind == "key" and value == "esc" and pane.reply:
            pane.reply = ""
        elif kind == "key" and value == "esc" and not pane.draft:
            self.files.dismiss(pane.conv)
        else:
            pane.edit(kind, value)
        self.dirty = True

    def friends_bar(self, cols):
        return self.screen.friends_bar(cols)

    def header(self, pane, width, focused, tabs=None):
        return self.screen.header(pane, width, focused, tabs)

    def render(self):
        return self.screen.render()

    def message_lines(self, conversation, cols):
        return self.screen.message_lines(conversation, cols)

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
        self.exit_note = "%s: %s" % (COMMAND, UNCONFIRMED_EXIT)

    def cleanup(self, fd, saved, out):
        blocked = {signal.SIGTERM, signal.SIGHUP, signal.SIGINT}
        signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        try:
            exiting = None
            try:
                self.stop_typing()
                self.settle()
                exiting = self.presence.exit_lock(EXIT_WAIT)
                self.presence.release()
                if exiting is None and self.presence.dir:
                    self.exit_note = "%s: %s" % (COMMAND, UNCOORDINATED_EXIT)
                else:
                    self.exit_helper()
                    if not self.handshake and self.helper.running():
                        self.exit_note = "%s: %s" % (COMMAND, UNCONFIRMED_EXIT)
            except BaseException:
                pass
            try:
                if self.link:
                    self.link.close()
                self.presence.release()
                if exiting is not None:
                    os.close(exiting)
            except BaseException:
                pass
        finally:
            try:
                termios.tcsetattr(fd, termios.TCSADRAIN, saved)
                out.write("\x1b[?2026l\x1b[0m\x1b[?2004l\x1b[?1004l\x1b[0 q\x1b[?25h\x1b[?1049l")
                out.flush()
                if self.exit_note:
                    print(self.exit_note, file=sys.stderr)
            except (OSError, ValueError):
                pass
            signal.pthread_sigmask(signal.SIG_UNBLOCK, blocked)

    def run(self):
        fd = sys.stdin.fileno()
        saved = termios.tcgetattr(fd)
        out = self.out
        for sig in (signal.SIGTERM, signal.SIGHUP):
            signal.signal(sig, stop)
        out.write("\x1b[?1049h\x1b[?1004h\x1b[?2004h\x1b[1 q")
        out.flush()
        try:
            tty.setraw(fd)
            typed, self.pastes_marked = measure_widths(fd, out)
            size = os.get_terminal_size(fd)
            self.size = (size.columns, size.lines)
            self.track_zoom(pixel_size(fd))
            self.take_input(self.keys.feed(typed))
            if not self.running:
                return
            out.write(self.render())
            out.flush()
            self.connect()
            while self.running:
                now = time.monotonic()
                self.tick(now)
                size = os.get_terminal_size(fd)
                if (size.columns, size.lines) != self.size:
                    self.size = (size.columns, size.lines)
                    self.track_zoom(pixel_size(fd))
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
                if fd in ready and not self.take_read(fd):
                    break
                if self.link and self.link in ready:
                    try:
                        events = self.link.read()
                    except (ConnectionError, OSError):
                        self.drop_link()
                        events = []
                    for ev in events:
                        self.on_event(ev)
                if self.hush and not self.discard_burst(fd):
                    break
        finally:
            self.cleanup(fd, saved, out)

    def _ev_snapshot(self, ev):
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
            addr = str(ev.get("addr") or "")
            if addr and self.addr and addr[:64] != self.addr[:64]:
                self.addr = addr
                self.reset_identity()
                return
            self.addr = addr or self.addr
            instance = str(ev.get("instance"))
            restarted = bool(self.instance) and instance != self.instance
            self.handshake = True
            self.instance = instance
            self.protocol = protocol
            try:
                self.history_clear = int(ev.get("historyClear") or 0)
            except (TypeError, ValueError):
                self.history_clear = 0
            try:
                self.identity.export_version = int(ev.get("identityExport") or 0)
            except (TypeError, ValueError):
                self.identity.export_version = 0
            self.helper.adopt(self.state, self.instance)
            self.send({"op": "identity.ready", "id": self.instance})
            self.resume_transfers(restarted)
        self.profile.on_snapshot(ev)
        self.identity.on_snapshot(ev)
        locked = ev.get("locked") is True
        if self.locked and not locked:
            self.secret, self.hush = "", True
        self.locked = locked
        if self.locked:
            self.conn = "locked"
            self.close_secret()
            if self.confirm is not None:
                self.drop_confirm()
        elif "online" in ev:
            self.conn = "online" if ev.get("online") else "connecting"
        self.dirty = True

    def _ev_connection(self, ev):
        self.conn = "online" if ev.get("state") == "online" else "reconnecting"

    def _ev_friend_list_begin(self, ev):
        self.friend_generation = str(ev.get("generation") or "")
        self.friend_build = []

    def _ev_friend_info(self, ev):
        if not (self.friend_build is not None and str(ev.get("generation") or "") == self.friend_generation):
            return
        fid = str(ev.get("id") or "")
        self.friend_build.append({"id": fid, "key": str(ev.get("key") or ""),
                                  "name": clean_line(ev.get("name") or "Friend " + fid),
                                  "online": bool(ev.get("online")),
                                  "status": str(ev.get("status") or ("online" if ev.get("online") else "offline"))})

    def _ev_friend_list_end(self, ev):
        if not (self.friend_build is not None and str(ev.get("generation") or "") == self.friend_generation):
            return
        self.apply_friends(self.friend_build)
        self.friend_build = None

    def _ev_identity(self, ev):
        self.identity.on_identity(ev)
        if ev.get("op") == "unlock":
            self.hush = self.hush or self.locked
            self.locked = False
            self.unlock_request = ""
            self.status = ""
            self.secret = ""
            self.handshake_request()
        elif ev.get("op") in ("import", "recovered"):
            self.reset_identity()

    def _ev_error(self, ev):
        code, request = str(ev.get("code") or ""), str(ev.get("request") or "")
        conversation = str(ev.get("conversation") or "")
        if code == "locked":
            self.locked = True
            self.conn = "locked"
            self.close_secret()
            if self.confirm is not None:
                self.drop_confirm()
        if self.unlock_request and request == self.unlock_request:
            self.unlock_request = ""
            self.say("The identity passphrase is incorrect.")
        elif self.identity.on_correlated_error(code, request):
            pass
        elif self.invites.on_correlated_error(code, request):
            pass
        elif self.profile.on_correlated_error(code, request):
            pass
        elif not request and self.identity.on_global_error(code):
            pass
        elif code == "history_failed" and conversation:
            last_conv, last_text, last_at = self.files.result
            if last_conv == conversation and time.monotonic() - last_at < 3.0:
                self.say("%s \u00b7 %s" % (last_text, HISTORY_ITEM_FAILED), 15)
            else:
                self.say(HISTORY_ITEM_FAILED, 15)
        elif code == "identity_changed":
            now = time.monotonic()
            if now > self.rehandshake_at:
                self.rehandshake_at = now + 2.0
                self.chat.unload()
                self.handshake_request()
        else:
            self.invites.on_window_error(code, request, conversation)

    def _ev_message(self, ev):
        if not self.bound(ev):
            return
        m = self.chat.on_message(ev)
        if m is None:
            return
        conversation = str(ev.get("conversation") or "")
        for pane in self.layout.panes():
            if pane.conv == conversation and m.dir == "in" and pane.select is None:
                pane.scroll = 0
        f = self.friend(conversation)
        if m.dir == "in" and f and conversation not in self.visible:
            self.say("%s: %s" % (f["name"], shorten(m.text.split("\n", 1)[0], 60)), 8)

    def _ev_history(self, ev):
        self.chat.on_history(ev)
        self.drop_deleted_choices(str(ev.get("conversation") or ""))

    def _ev_message_updated(self, ev):
        m = self.chat.on_message_updated(ev)
        if m is not None and m.deleted:
            self.drop_deleted_choices(str(ev.get("conversation") or ""))

    def drop_deleted_choices(self, conversation):
        c = self.chat.convs.get(conversation)
        deleted = {m.id for m in (c.items if c else []) if m.deleted and m.id}
        for pane in self.layout.panes():
            if pane.conv != conversation:
                continue
            if pane.select in deleted:
                targets = self.chat.targets(conversation)
                pane.select = targets[-1].id if targets else None
            if pane.reply in deleted:
                pane.reply = ""
                self.say("Reply canceled: the message was deleted")

    def _ev_history_clear_result(self, ev):
        if self.chat.on_clear_result(ev, self.instance):
            conversation = str(ev.get("conversation") or "")
            for pane in self.layout.panes():
                if pane.conv == conversation:
                    pane.forget_reply()

    def _cmd_quit(self, verb, arg):
        self.running = False

    def _cmd_close(self, verb, arg):
        self.close_pane()

    def _cmd_invite(self, verb, arg):
        if arg.lower() == "revoke":
            if not self.invites.invite.url:
                self.say("No active invite")
            else:
                self.confirm = Confirmation("revoke", "Revoke this invite? It cannot be used again. y/n",
                                            time.monotonic(), time.monotonic() + CONFIRM_SECONDS)
        elif arg:
            self.say("Usage: :invite or :invite revoke")
        else:
            self.invites.create()

    def _cmd_redeem(self, verb, arg):
        self.invites.redeem(arg)

    def _cmd_nickname(self, verb, arg):
        self.profile.set_nickname(arg)

    def _cmd_requests(self, verb, arg):
        self.invites.show_request()

    def _cmd_decide(self, verb, arg):
        self.invites.decide(verb)

    def _cmd_sendfile(self, verb, arg):
        self.files.send_file(self.current, arg)

    def _cmd_offer(self, verb, arg):
        self.files.answer(verb, self.current)

    def _cmd_cancelfile(self, verb, arg):
        self.files.cancel(self.current)

    def _cmd_clear(self, verb, arg):
        pane = self.pane
        if not pane or not pane.conv:
            self.say("No chat is open")
        elif self.history_clear != 2:
            self.say("Helper update required")
        elif pane.conv in self.chat.clears:
            self.say("Clearing is already in progress")
        else:
            self.confirm = Confirmation("clear", "Clear this chat? y/n", time.monotonic(),
                                        time.monotonic() + CONFIRM_SECONDS, pane.conv, self.key_for(pane.conv),
                                        self.instance)

    def _cmd_reply(self, verb, arg):
        self.begin_select()

    def _cmd_identity(self, verb, arg):
        self.identity.command(arg)

    def _cmd_help(self, verb, arg):
        self.help = Help()

    def _cmd_named(self, verb, arg):
        pick = self.match(arg)
        if not pick:
            self.say("No unique friend named %s" % clean_line(arg) if arg else "Usage: :%s <name>" % verb)
        elif not self.pane:
            pane = Pane(pick["id"])
            self.layout.add(pane)
            self.restore_draft(pane)
            self.chat.load(pick["id"])
        else:
            self.open_pane(pick["id"], vertical=verb not in ("split", "sp"))


def stop(signum, frame):
    raise SystemExit(0)
