import secrets
import time

from .protocol import HISTORY_LIMIT, Message, is_direct, message_from
from .session import Conversation, bound, find_sent, mark_unsaved, merge_history
from .text import clean_line

HISTORY_RETRY = 3.0
TYPING_EXPIRE = 4.5


class Chat:
    def __init__(self, send, send_bound, say, friends, ready):
        self.send, self.send_bound, self.say = send, send_bound, say
        self.friends, self.ready = friends, ready
        self.convs, self.pending, self.clears = {}, {}, {}
        self.unread, self.read_pending, self.retry_at = {}, {}, {}

    def bound(self, ev):
        return bound(self.friends(), ev)

    def conv(self, conversation):
        c = self.convs.get(conversation)
        if c is None:
            c = Conversation()
            self.convs[conversation] = c
        return c

    def load(self, conversation):
        if not conversation or not self.ready():
            return
        c = self.conv(conversation)
        if c.loaded or c.request or time.monotonic() < self.retry_at.get(conversation, 0.0):
            return
        c.request = "cli-history-" + secrets.token_hex(6)
        if not self.send_bound({"op": "history", "conversation": conversation, "limit": HISTORY_LIMIT,
                                "id": c.request}):
            c.request = ""

    def reload(self, conversation):
        c = self.convs.get(conversation)
        if c:
            c.loaded, c.request = False, ""

    def unload(self):
        for c in self.convs.values():
            c.loaded, c.request = False, ""

    def send_message(self, conversation, text):
        request = "cli-" + secrets.token_hex(10)
        m = Message("", request, "out", text, time.time(), ack=0)
        self.conv(conversation).items.append(m)
        self.pending[request] = (conversation, m)
        if not self.send_bound({"op": "msg.send", "conversation": conversation, "text": text, "reply": "",
                                "id": request}):
            self.pending.pop(request, None)
            m.failed = "error"

    def sent_texts(self, conversation):
        texts = []
        c = self.convs.get(conversation)
        for m in reversed(c.items if c else []):
            if m.dir == "out" and m.text and not m.kind and (not texts or texts[-1] != m.text):
                texts.append(m.text)
        return texts

    def mark_read(self, visible, now):
        for conversation in visible:
            c = self.convs.get(conversation)
            if (self.unread.get(conversation) and c and c.loaded
                    and now > self.read_pending.get(conversation, 0.0)):
                if self.send_bound({"op": "conversation.read", "conversation": conversation}):
                    self.read_pending[conversation] = now + 5.0

    def expire_typing(self, now):
        expired = False
        for c in self.convs.values():
            if c.typing_until and now > c.typing_until:
                c.typing_until = 0.0
                expired = True
        return expired

    def clear(self, conversation, key, instance):
        request = "cli-clear-" + secrets.token_hex(8)
        if self.send({"op": "history.clear", "conversation": conversation, "key": key, "id": request}):
            self.clears[conversation] = (request, instance)
            self.say("Clearing this chat…", 10)
        else:
            self.say("Chat could not be cleared")

    def on_unread(self, ev):
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

    def on_history(self, ev):
        conversation = str(ev.get("conversation") or "")
        c = self.convs.get(conversation)
        if c and c.request and str(ev.get("request") or "") == c.request and self.bound(ev):
            items = ev.get("items") if isinstance(ev.get("items"), list) else []
            pending = [m for owner, m in self.pending.values() if owner == conversation]
            c.items = merge_history(c.items, items, pending)
            c.loaded, c.request = True, ""
            self.retry_at.pop(conversation, None)

    def on_history_failed(self, ev):
        conversation = str(ev.get("conversation") or "")
        c = self.convs.get(conversation)
        if c and c.request and str(ev.get("request") or "") == c.request:
            c.request = ""
            self.retry_at[conversation] = time.monotonic() + HISTORY_RETRY
            self.say("History could not be loaded; retrying")

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
            return None
        m = message_from(ev)
        if m.id and any(x.id == m.id for x in c.items):
            return None
        c.items.append(m)
        return m

    def on_message_failed(self, ev):
        request = str(ev.get("request") or "")
        entry = self.pending.pop(request, None)
        if not entry and request:
            conversation = str(ev.get("conversation") or "")
            c = self.convs.get(conversation)
            sent = find_sent(c.items if c else [], request)
            if sent is not None:
                entry = (conversation, sent)
        if entry and ev.get("delivered") is True:
            owner, m = entry
            mark_unsaved(self.conv(owner).items, m, time.time())
        elif entry:
            entry[1].failed = str(ev.get("code") or "error")

    def on_receipt(self, ev):
        if self.bound(ev):
            c = self.convs.get(str(ev.get("conversation") or ""))
            if c:
                level = 3 if ev.get("state") == "read" else (2 if ev.get("state") == "delivered" else 0)
                for m in c.items:
                    if m.id and m.id == str(ev.get("id") or "") and m.dir == "out":
                        m.ack = max(m.ack, level)

    def on_typing(self, ev):
        if self.bound(ev):
            conversation = str(ev.get("conversation") or "")
            if is_direct(conversation):
                self.conv(conversation).typing_until = time.monotonic() + TYPING_EXPIRE if ev.get("typing") else 0.0

    def on_clear_result(self, ev, current):
        name = ev.get("event")
        conversation = str(ev.get("conversation") or "")
        request, instance = self.clears.get(conversation) or ("", "")
        if (request and str(ev.get("request") or "") == request and self.bound(ev)
                and str(ev.get("instance") or "") == instance == current):
            del self.clears[conversation]
            if name == "history.clear.succeeded":
                c = self.conv(conversation)
                c.items, c.loaded, c.request = [], False, ""
                self.unread.pop(conversation, None)
                self.load(conversation)
                self.say("Chat cleared on this computer")
            else:
                self.say("Chat could not be cleared (%s)" % clean_line(ev.get("code") or "error"))

    def forget(self, fid):
        self.convs.pop(fid, None)
        self.unread.pop(fid, None)
        self.read_pending.pop(fid, None)
        self.retry_at.pop(fid, None)
        if self.clears.pop(fid, None):
            self.say("Clear result unknown; check the chat")
        for request, (conversation, m) in list(self.pending.items()):
            if conversation == fid:
                m.failed = "identity_changed"
                del self.pending[request]

    def drop(self):
        self.unload()
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        if self.clears:
            self.clears.clear()
            self.say("Clear result unknown; check the chat")

    def reset(self):
        for conversation, m in self.pending.values():
            m.failed = "delivery_unknown"
        self.pending.clear()
        self.convs, self.unread, self.read_pending, self.retry_at = {}, {}, {}, {}
        self.clears = {}
