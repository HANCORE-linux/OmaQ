import os
import secrets
import select
import signal
import subprocess
import sys
import termios
import time
import tty

from .text import *
from .protocol import *
from .session import *
from .files import *
from .invites import *
from .commands import *
from .view import *
from .modals import *
from .palette import *
from .keys import *
from .link import *
from .helper_process import *
from .presence import *
from .terminal import *
from .system import *
from .layout import *


HISTORY_RETRY = 3.0
TYPING_SEND_EVERY = 3.0
TYPING_IDLE = 3.0
TYPING_EXPIRE = 4.5
STATUS_SECONDS = 6.0
COMMAND = "omaq-connect"
CONFIRM_SECONDS = 15
TIP_SECONDS = 8
ZOOM_SECONDS = 4
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
    "unread": "_ev_unread",
    "history": "_ev_history",
    "history.failed": "_ev_history_failed",
    "message": "_ev_message",
    "message.failed": "_ev_message_failed",
    "receipt": "_ev_receipt",
    "typing": "_ev_typing",
    "invite": "_ev_invite",
    "file.offer": "_ev_file_offer",
    "file.sending": "_ev_file_sending",
    "file.progress": "_ev_file_progress",
    "file.done": "_ev_file_result",
    "file.failed": "_ev_file_result",
    "file.canceled": "_ev_file_result",
    "contact.decided": "_ev_contact_decided",
    "file.state": "_ev_file_state",
    "invite.redeemed": "_ev_invite_redeemed",
    "request": "_ev_request",
    "request.conflict": "_ev_request_conflict",
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
    "requests": "_cmd_requests",
    "accept": "_cmd_decide",
    "decline": "_cmd_decide",
    "sendfile": "_cmd_sendfile",
    "acceptfile": "_cmd_offer",
    "declinefile": "_cmd_offer",
    "cancelfile": "_cmd_cancelfile",
    "clear": "_cmd_clear",
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
        self.palette = Palette()
        self.presence = Presence(self.state)
        self.link = None
        self.open_link = Link
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
        self.help_top, self.help_follow, self.help_seen = 0, True, (0, 0)
        self.completion = None
        self.scratch = Pane("")
        self.exit_note = ""
        self.commands = []
        self.out = sys.stdout
        self.invites = Invites(self.send, self.say, self.send_failed, self.show_invite, self.open_overlay,
                               self.close_overlays)
        self.overlay = None
        self.picker = None
        self.addr = ""
        self.cell_base, self.zoom, self.zoom_until = 0, None, 0.0
        self.screen = Screen(self)
        self.files = FileTransfers(self.send, self.say, self.send_failed, lambda: self.friends, self.finish_file)
        self.tip, self.tip_at = secrets.randbelow(len(TIPS)), time.monotonic() + TIP_SECONDS
        self.confirm = None
        self.clears = {}
        self.history_clear = 0

    @property
    def invite(self):
        return self.invites.invite

    @invite.setter
    def invite(self, value):
        self.invites.invite = value

    @property
    def invite_request(self):
        return self.invites.invite_request

    @invite_request.setter
    def invite_request(self, value):
        self.invites.invite_request = value

    @property
    def redeem_request(self):
        return self.invites.redeem_request

    @redeem_request.setter
    def redeem_request(self, value):
        self.invites.redeem_request = value

    @property
    def decide_request(self):
        return self.invites.decide_request

    @decide_request.setter
    def decide_request(self, value):
        self.invites.decide_request = value

    @property
    def request(self):
        return self.invites.request

    @request.setter
    def request(self, value):
        self.invites.request = value

    @property
    def action(self):
        return self.invites.action

    @action.setter
    def action(self, value):
        self.invites.action = value

    @property
    def action_until(self):
        return self.invites.action_until

    @action_until.setter
    def action_until(self, value):
        self.invites.action_until = value

    @property
    def offers(self):
        return self.files.offers

    @offers.setter
    def offers(self, value):
        self.files.offers = value

    @property
    def sends(self):
        return self.files.sends

    @sends.setter
    def sends(self, value):
        self.files.sends = value

    @property
    def file_result(self):
        return self.files.result

    @file_result.setter
    def file_result(self, value):
        self.files.result = value

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
            self.link = self.open_link(path)
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
        if self.confirm is not None:
            self.drop_confirm()
        self.handshake = False
        self.friend_build = None
        self.conn = "reconnecting"
        self.next_connect = time.monotonic() + 1.0
        for c in self.convs.values():
            c["loaded"], c["request"] = False, ""
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        if self.clears:
            self.clears.clear()
            self.say("Clear result unknown; check the chat")
        self.overlay = None
        self.invites.drop()
        self.dirty = True

    def reset_identity(self):
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        self.friends, self.friend_build, self.friends_ready = [], None, False
        self.convs, self.unread, self.read_pending, self.retry_at, self.drafts = {}, {}, {}, {}, {}
        if self.confirm is not None:
            self.drop_confirm()
        self.clears = {}
        self.overlay = None
        self.invites.reset()
        self.files.reset()
        self.typing_on = ""
        self.commands = []
        for pane in self.layout.panes() + [self.scratch]:
            pane.conv, pane.draft, pane.cursor, pane.scroll = "", "", 0, 0
            self.forget_recall(pane)
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

    def forget_recall(self, pane):
        pane.hist, pane.hist_items, pane.stash = None, None, ("", 0)

    def resume_transfers(self, restarted):
        self.files.resume(restarted)

    def settle(self):
        if self.handshake or self.proc is None or self.proc.poll() is not None:
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
            getattr(self, handler)(ev)
        self.dirty = True

    def purge(self, fid):
        self.convs.pop(fid, None)
        self.unread.pop(fid, None)
        self.read_pending.pop(fid, None)
        self.retry_at.pop(fid, None)
        if self.typing_on == fid:
            self.typing_on = ""
        if self.confirm is not None and self.confirm.conv == fid:
            self.drop_confirm()
        if self.clears.pop(fid, None):
            self.say("Clear result unknown; check the chat")
        self.files.forget(fid)
        for request, (conversation, m) in list(self.pending.items()):
            if conversation == fid:
                m.failed = "identity_changed"
                del self.pending[request]

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
                self.forget_recall(pane)
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
        f = self.friend(conversation)
        if m.dir == "in" and f and conversation not in self.visible:
            self.say("%s: %s" % (f["name"], shorten(m.text.split("\n", 1)[0], 60)), 8)

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
        if self.status and now > self.status_until and self.confirm is None:
            self.status = ""
            self.dirty = True
        if self.confirm is not None and self.confirm.expired(now):
            self.drop_confirm()
        if self.zoom_until and now >= self.zoom_until:
            self.zoom_until = 0.0
            self.dirty = True
        if now >= self.tip_at:
            self.tip_at = now + TIP_SECONDS
            if self.focused and not is_command((self.pane or self.scratch).draft):
                self.tip = (self.tip + 1) % len(TIPS)
                self.dirty = True
        if self.overlay and self.overlay.get("kind") == "invite":
            minute = int(time.time() // 60)
            if self.overlay.get("minute") != minute:
                self.overlay["minute"] = minute
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
        known = ("q", "q!", "quit", "quit!", "close", "clo", "help", "h", "clear", "invite", "redeem", "requests",
                 "accept", "decline", "sendfile", "acceptfile", "declinefile", "cancelfile") + NAMED
        if verb and verb not in known:
            candidates = [name for name, _ in VERBS
                          if name.startswith(verb) and name not in ("accept", "decline", "acceptfile", "declinefile")]
            if len(candidates) == 1:
                verb = candidates[0]
        handler = COMMAND_HANDLERS.get(verb)
        if handler:
            getattr(self, handler)(verb, arg)
        else:
            self.say('Unknown command :%s; :help lists the commands' % clean_line(verb))

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

    def finish_file(self, conversation):
        c = self.convs.get(conversation)
        if c:
            c["loaded"], c["request"] = False, ""

    def send_failed(self):
        self.say("Not connected" if not self.link else "Could not send the request")

    def clear_chat(self, conversation, key):
        request = "cli-clear-" + secrets.token_hex(8)
        if self.send({"op": "history.clear", "conversation": conversation, "key": key, "id": request}):
            self.clears[conversation] = (request, self.instance)
            self.say("Clearing this chat…", 10)
        else:
            self.say("Chat could not be cleared")

    def sent_texts(self, conversation):
        texts = []
        for m in reversed(self.convs.get(conversation, {}).get("items", [])):
            if m.dir == "out" and m.text and (not texts or texts[-1] != m.text):
                texts.append(m.text)
        return texts

    def history_step(self, pane, step):
        if pane.hist is None:
            if step < 0:
                return
            items = self.commands[::-1] if pane.draft.startswith(":") else self.sent_texts(pane.conv)
            if not items:
                return
            pane.hist_items, pane.stash, pane.hist = items, (pane.draft, pane.cursor), 0
        else:
            pane.hist += step
            if pane.hist < 0:
                pane.draft, pane.cursor = pane.stash
                pane.hist, pane.hist_items = None, None
                self.dirty = True
                return
            pane.hist = min(pane.hist, len(pane.hist_items) - 1)
        pane.draft = pane.hist_items[pane.hist]
        pane.cursor = len(pane.draft)
        self.dirty = True

    def open_overlay(self, overlay):
        self.overlay = overlay

    def close_overlays(self, *kinds):
        if self.overlay and self.overlay.get("kind") in kinds:
            self.overlay = None

    def show_invite(self, copy=False):
        url = self.invite.url
        if not url:
            return
        self.overlay = {"kind": "invite", "url": url, "qr": qr_lines(url)}
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
        query = self.picker["query"].strip().lower()
        items = [(emoji, emoji_name(emoji)) for emoji in EMOJI]
        if not query:
            return items
        hits = [item for item in items if query in item[1]]
        return hits + [item for item in emoji_catalog() if query in item[1] and item[0] not in EMOJI]

    def picker_key(self, kind, value):
        picker = self.picker
        items = self.picker_items()
        per_row = max(1, (self.size[0] - 1) // 4)
        if (kind == "key" and value == "esc") or (kind == "ctrl" and value in ("k", "c")):
            self.picker = None
        elif kind == "key" and value == "enter":
            if items:
                composer = self.pane or self.scratch
                emoji = items[min(picker["index"], len(items) - 1)][0]
                composer.draft = composer.draft[:composer.cursor] + emoji + composer.draft[composer.cursor:]
                composer.cursor += len(emoji)
                composer.hist, composer.hist_items, self.completion = None, None, None
            self.picker = None
        elif kind == "key" and value in ("left", "right", "up", "down"):
            step = {"left": -1, "right": 1, "up": -per_row, "down": per_row}[value]
            picker["index"] = max(0, min(len(items) - 1, picker["index"] + step))
        elif kind == "key" and value == "backspace":
            picker["query"], picker["index"] = picker["query"][:-1], 0
        elif kind in ("text", "paste"):
            typed = "".join(ch for ch in value if ch.isprintable())
            picker["query"], picker["index"] = (picker["query"] + typed)[:40], 0
        self.dirty = True

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
        return self.screen.command_hint(cols)

    def help_key(self, kind, value):
        count = len(COMMANDS)
        self.help_follow = True
        if kind == "key" and value in ("up", "backtab"):
            self.help = (self.help - 1) % count
        elif kind == "key" and value in ("down", "tab"):
            self.help = (self.help + 1) % count
        elif kind == "key" and value in ("pgup", "pgdn"):
            step = max(1, self.size[1] - 8)
            self.help_top = max(0, self.help_top + (step if value == "pgdn" else -step))
            self.help_follow = False
            self.dirty = True
            return
        elif kind == "text" and value in ("k", "j"):
            self.help = (self.help + (1 if value == "j" else -1)) % count
        elif kind == "key" and value == "enter":
            first, span = self.help_seen
            if not first <= 1 + self.help < first + span:
                self.help_follow = True
                self.dirty = True
                return
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
                    self.clear_chat(confirm.conv, confirm.key)
                else:
                    self.say("Chat kept")
            elif confirm.action == "revoke":
                if yes:
                    self.invites.revoke()
                else:
                    self.say("Invite kept")
            self.show_notice(notice)
            self.dirty = True
            return
        if self.picker is not None:
            self.picker_key(kind, value)
            return
        if self.overlay is not None and kind == "key" and value in ("pgup", "pgdn"):
            step = max(1, self.size[1] - 8)
            self.overlay["top"] = max(0, self.overlay.get("top", 0) + (step if value == "pgdn" else -step))
            self.dirty = True
            return
        if self.overlay is not None and kind == "key" and value == "esc":
            self.overlay = None
            self.dirty = True
            return
        if kind == "ctrl" and value == "c":
            composer = self.pane or self.scratch
            composer.draft, composer.cursor = "", 0
            self.say("Type :quit and press Enter to quit")
            return
        if kind == "key" and value == "f1":
            self.help, self.help_top, self.help_follow = 0, 0, True
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
        if kind == "ctrl" and value == "k":
            self.picker = {"query": "", "index": 0}
            self.dirty = True
            return
        pane = self.pane or self.scratch
        if not (kind == "key" and value in ("tab", "backtab")):
            self.completion = None
        if not (kind == "key" and value in ("up", "down")):
            pane.hist, pane.hist_items = None, None
        if kind in ("text", "paste") or (kind == "key" and value in ("shift-enter", "alt-shift-enter", "alt-enter")):
            insert = "\n" if kind == "key" else value
            pane.draft = pane.draft[:pane.cursor] + insert + pane.draft[pane.cursor:]
            pane.cursor += len(insert)
            if not is_command(pane.draft) and any(ch.isspace() for ch in insert):
                before = emojify(pane.draft[:pane.cursor], final=False)
                pane.draft, pane.cursor = before + pane.draft[pane.cursor:], len(before)
            self.last_edit = now
            if pane.conv and not is_command(pane.draft) and now - self.typing_sent > TYPING_SEND_EVERY:
                if self.send_bound({"op": "typing.set", "conversation": pane.conv, "typing": True}):
                    self.typing_on, self.typing_sent = pane.conv, now
        elif kind == "key":
            if value == "enter":
                self.overlay = None
                if is_command(pane.draft):
                    line, pane.draft, pane.cursor = pane.draft, "", 0
                    if line.strip() != ":" and (not self.commands or self.commands[-1] != line):
                        self.commands = (self.commands + [line])[-100:]
                    self.command(line)
                else:
                    self.send_text()
            elif value == "backspace" and pane.cursor > 0:
                j = prev_stop(pane.draft, pane.cursor)
                pane.draft, pane.cursor = pane.draft[:j] + pane.draft[pane.cursor:], j
            elif value == "delete" and pane.cursor < len(pane.draft):
                pane.draft = pane.draft[:pane.cursor] + pane.draft[next_stop(pane.draft, pane.cursor):]
            elif value == "left":
                pane.cursor = prev_stop(pane.draft, pane.cursor)
            elif value == "right":
                pane.cursor = next_stop(pane.draft, pane.cursor)
            elif value == "home":
                pane.cursor = 0
            elif value == "end":
                pane.cursor = len(pane.draft)
            elif value in ("up", "down"):
                self.history_step(pane, 1 if value == "up" else -1)
                return
            elif value == "pgup":
                pane.scroll += max(1, self.size[1] // 2)
            elif value == "pgdn":
                pane.scroll = max(0, pane.scroll - max(1, self.size[1] // 2))
            elif value in ("tab", "backtab") and is_command(pane.draft):
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
                    if not self.handshake and self.proc is not None and self.proc.poll() is None:
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
            typed = measure_widths(fd, out)
            size = os.get_terminal_size(fd)
            self.size = (size.columns, size.lines)
            self.track_zoom(pixel_size(fd))
            for kind, value in self.keys.feed(typed):
                self.edit(kind, value)
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
            running = self.proc is not None and self.proc.poll() is None
            launched = read_protocol(self.state)
            self.owns = running and bool(self.launch_nonce) and launched.get("nonce") == self.launch_nonce \
                and launched.get("instance") == self.instance and launched.get("pid") == self.proc.pid
            if self.owns:
                self.presence.mark_started(self.instance, self.launch_nonce)
            self.send({"op": "identity.ready", "id": self.instance})
            self.resume_transfers(restarted)
        self.locked = ev.get("locked") is True
        if self.locked:
            self.conn = "locked"
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
        if ev.get("op") == "unlock":
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
            if self.confirm is not None:
                self.drop_confirm()
        if self.unlock_request and request == self.unlock_request:
            self.unlock_request = ""
            self.say("The identity passphrase is incorrect.")
        elif self.invites.on_correlated_error(code, request):
            pass
        elif code == "history_failed" and conversation:
            last_conv, last_text, last_at = self.file_result
            if last_conv == conversation and time.monotonic() - last_at < 3.0:
                self.say("%s \u00b7 %s" % (last_text, HISTORY_ITEM_FAILED), 15)
            else:
                self.say(HISTORY_ITEM_FAILED, 15)
        elif code == "identity_changed":
            now = time.monotonic()
            if now > self.rehandshake_at:
                self.rehandshake_at = now + 2.0
                for c in self.convs.values():
                    c["loaded"], c["request"] = False, ""
                self.handshake_request()
        else:
            self.invites.on_window_error(code, request, conversation)

    def _ev_unread(self, ev):
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

    def _ev_history(self, ev):
        conversation = str(ev.get("conversation") or "")
        c = self.convs.get(conversation)
        if c and c["request"] and str(ev.get("request") or "") == c["request"] and self.bound(ev):
            items = ev.get("items") if isinstance(ev.get("items"), list) else []
            pending = [m for owner, m in self.pending.values() if owner == conversation]
            c["items"] = merge_history(c["items"], items, pending)
            c["loaded"], c["request"] = True, ""
            self.retry_at.pop(conversation, None)

    def _ev_history_failed(self, ev):
        conversation = str(ev.get("conversation") or "")
        c = self.convs.get(conversation)
        if c and c["request"] and str(ev.get("request") or "") == c["request"]:
            c["request"] = ""
            self.retry_at[conversation] = time.monotonic() + HISTORY_RETRY
            self.say("History could not be loaded; retrying")

    def _ev_message(self, ev):
        if self.bound(ev):
            self.on_message(ev)

    def _ev_message_failed(self, ev):
        request = str(ev.get("request") or "")
        entry = self.pending.pop(request, None)
        if not entry and request:
            conversation = str(ev.get("conversation") or "")
            sent = find_sent(self.convs.get(conversation, {}).get("items", []), request)
            if sent is not None:
                entry = (conversation, sent)
        if entry and ev.get("delivered") is True:
            owner, m = entry
            mark_unsaved(self.conv(owner)["items"], m, time.time())
        elif entry:
            entry[1].failed = str(ev.get("code") or "error")

    def _ev_receipt(self, ev):
        if self.bound(ev):
            c = self.convs.get(str(ev.get("conversation") or ""))
            if c:
                level = 3 if ev.get("state") == "read" else (2 if ev.get("state") == "delivered" else 0)
                for m in c["items"]:
                    if m.id and m.id == str(ev.get("id") or "") and m.dir == "out":
                        m.ack = max(m.ack, level)

    def _ev_typing(self, ev):
        if self.bound(ev):
            conversation = str(ev.get("conversation") or "")
            if is_direct(conversation):
                self.conv(conversation)["typing_until"] = time.monotonic() + TYPING_EXPIRE if ev.get("typing") else 0.0

    def _ev_invite(self, ev):
        self.invites.on_invite(ev)

    def _ev_file_offer(self, ev):
        self.files.on_offer(ev)

    def _ev_file_sending(self, ev):
        self.files.on_sending(ev)

    def _ev_file_progress(self, ev):
        self.files.on_progress(ev)

    def _ev_file_result(self, ev):
        self.files.on_result(ev)

    def _ev_contact_decided(self, ev):
        self.invites.on_decided(ev)

    def _ev_file_state(self, ev):
        self.files.on_state(ev)

    def _ev_invite_redeemed(self, ev):
        self.invites.on_redeemed(ev)

    def _ev_request(self, ev):
        self.invites.on_request(ev)

    def _ev_request_conflict(self, ev):
        self.invites.on_conflict(ev)

    def _ev_history_clear_result(self, ev):
        name = ev.get("event")
        conversation = str(ev.get("conversation") or "")
        request, instance = self.clears.get(conversation) or ("", "")
        if (request and str(ev.get("request") or "") == request and self.bound(ev)
                and str(ev.get("instance") or "") == instance == self.instance):
            del self.clears[conversation]
            if name == "history.clear.succeeded":
                c = self.conv(conversation)
                c["items"], c["loaded"], c["request"] = [], False, ""
                self.unread.pop(conversation, None)
                self.load(conversation)
                self.say("Chat cleared on this computer")
            else:
                self.say("Chat could not be cleared (%s)" % clean_line(ev.get("code") or "error"))

    def _cmd_quit(self, verb, arg):
        self.running = False

    def _cmd_close(self, verb, arg):
        self.close_pane()

    def _cmd_invite(self, verb, arg):
        if arg.lower() == "revoke":
            if not self.invite.url:
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
        elif pane.conv in self.clears:
            self.say("Clearing is already in progress")
        else:
            self.confirm = Confirmation("clear", "Clear this chat? y/n", time.monotonic(),
                                        time.monotonic() + CONFIRM_SECONDS, pane.conv, self.key_for(pane.conv),
                                        self.instance)

    def _cmd_help(self, verb, arg):
        self.help, self.help_top, self.help_follow = 0, 0, True

    def _cmd_named(self, verb, arg):
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


def stop(signum, frame):
    raise SystemExit(0)
