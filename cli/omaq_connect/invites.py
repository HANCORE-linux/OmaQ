import secrets
import time
from dataclasses import dataclass

from .protocol import FAILURES, INVITE_ERRORS, REDEEMED_DIRECT, REDEEMED_GROUP, TEXT_MAX, hex64, safety_ok

ACTION_SECONDS = 10


@dataclass(eq=False, slots=True)
class Invite:
    url: str = ""
    expires: int = 0

    def active(self, now):
        return bool(self.url) and self.expires > now

    def cleared(self):
        return not self.url and not self.expires


@dataclass(eq=False, slots=True)
class PendingRequest:
    kind: str
    key: str = ""
    safety: str = ""
    conflict: str = ""


class Invites:
    def __init__(self, send, say, failed, show_link, show_overlay, close_overlays):
        self.send, self.say, self.failed = send, say, failed
        self.show_link, self.show_overlay, self.close_overlays = show_link, show_overlay, close_overlays
        self.invite = Invite()
        self.invite_request = ""
        self.redeem_request = ""
        self.decide_request = ""
        self.request = None
        self.action, self.action_until = "", 0.0

    def decide_failure(self, code):
        if code == "forbidden" and self.action == "accept":
            return "The request could not be accepted."
        return INVITE_ERRORS.get(code) or FAILURES.get(code) or code or "The request could not be answered."

    def create(self):
        if self.invite.active(time.time()):
            self.show_link(copy=True)
        else:
            self.invite_request = "cli-invite-" + secrets.token_hex(8)
            if self.send({"op": "invite.create", "kind": "direct", "ttlSec": 86400,
                          "request": self.invite_request}):
                self.action, self.action_until = "invite", time.monotonic() + ACTION_SECONDS
            else:
                self.invite_request = ""
                self.failed()

    def revoke(self):
        self.invite_request = "cli-revoke-" + secrets.token_hex(8)
        if self.send({"op": "invite.revoke", "request": self.invite_request}):
            self.action, self.action_until = "invite", time.monotonic() + ACTION_SECONDS
        else:
            self.invite_request = ""
            self.failed()

    def redeem(self, arg):
        link = arg.strip()
        if not link.startswith("omaq://"):
            self.say("Paste the omaq:// link after :redeem")
        elif len(link.encode("utf-8")) > TEXT_MAX:
            self.say("Link is too long")
        else:
            self.redeem_request = "cli-redeem-" + secrets.token_hex(8)
            if self.send({"op": "invite.redeem", "payload": link, "id": self.redeem_request}):
                self.action, self.action_until = "redeem", time.monotonic() + ACTION_SECONDS
                self.say("Checking the invite…", 15)
            else:
                self.redeem_request = ""
                self.failed()

    def show_request(self):
        if self.request:
            self.show_overlay({"kind": "request"})
        else:
            self.say("No pending request")

    def decide(self, verb):
        if not self.request:
            self.say("No pending request")
        else:
            self.decide_request = "cli-decide-" + secrets.token_hex(8)
            operation = {"op": "contact.decide", "id": self.decide_request, "accept": verb == "accept"}
            if self.request.kind == "direct":
                operation["key"] = self.request.key
            if self.send(operation):
                self.action, self.action_until = verb, time.monotonic() + ACTION_SECONDS
                if self.request.kind == "group":
                    self.request = None
                self.close_overlays("request")
                self.say("Accepted" if verb == "accept" else "Declined")
            else:
                self.failed()

    def on_invite(self, ev):
        if "url" in ev:
            self.invite.url = str(ev.get("url") or "")
        if "expires" in ev:
            try:
                self.invite.expires = max(0, int(ev.get("expires") or 0))
            except (TypeError, ValueError):
                self.invite.expires = 0
        if self.invite.cleared():
            if self.request and self.request.kind == "direct":
                self.request = None
            self.close_overlays("invite", "request")
        request = str(ev.get("request") or "")
        if request and request == self.invite_request:
            self.invite_request, self.action_until = "", 0.0
            if ev.get("op") == "revoke":
                self.say("Invite revoked")
            elif self.invite.url:
                self.show_link(copy=True)

    def on_decided(self, ev):
        if self.decide_request and str(ev.get("request") or "") == self.decide_request:
            self.decide_request, self.action_until = "", 0.0
            if ev.get("ok") is True:
                self.say("Accepted" if self.action == "accept" else "Declined")
            else:
                self.say(self.decide_failure(str(ev.get("code") or "")), 15)

    def on_redeemed(self, ev):
        if self.redeem_request and str(ev.get("request") or "") == self.redeem_request:
            self.redeem_request, self.action_until = "", 0.0
            kind = str(ev.get("kind") or "")
            safety = str(ev.get("safety") or "")
            if kind not in ("direct", "group") or (kind == "direct" and not (hex64(ev.get("key")) and safety_ok(safety))):
                self.say("OmaQ received an invalid response from the running helper.")
            else:
                self.show_overlay({"kind": "redeemed", "group": kind == "group", "safety": safety})
                self.say(REDEEMED_GROUP if kind == "group" else REDEEMED_DIRECT, 15)

    def on_request(self, ev):
        kind = str(ev.get("kind") or "")
        if kind == "direct":
            key, safety = str(ev.get("key") or ""), str(ev.get("safety") or "")
            if not hex64(key) or not safety_ok(safety):
                self.request = None
            elif self.request and self.request.kind == "direct" and self.request.key == key:
                self.request.safety = safety
            else:
                self.request = PendingRequest("direct", key, safety)
                self.say("Friend request; :requests shows it", 15)
        elif kind == "group":
            if not self.request or self.request.kind != "group":
                self.say("Group invitation; :requests shows it", 15)
            self.request = PendingRequest("group")

    def on_conflict(self, ev):
        key = str(ev.get("key") or "")
        if (self.request and self.request.kind == "direct" and ev.get("kind") == "direct" and hex64(key)
                and key != self.request.key):
            self.request.conflict = key
            self.say("Another device used this invite link", 15)

    def on_correlated_error(self, code, request):
        if self.redeem_request and request == self.redeem_request:
            self.redeem_request, self.action_until = "", 0.0
            self.say(INVITE_ERRORS.get(code) or FAILURES.get(code) or code or "The invite could not be added.", 15)
        elif self.invite_request and request == self.invite_request:
            self.invite_request, self.action_until = "", 0.0
            self.say(INVITE_ERRORS.get(code) or FAILURES.get(code) or code or "The invite could not be created.", 15)
        elif self.decide_request and request == self.decide_request:
            self.say(self.decide_failure(code), 15)
        else:
            return False
        return True

    def on_window_error(self, code, request, conversation):
        if not (code and code != "locked" and not conversation and not request and time.monotonic() < self.action_until):
            return False
        if self.action in ("accept", "decline"):
            self.say(self.decide_failure(code), 15)
        else:
            self.say(INVITE_ERRORS.get(code) or FAILURES.get(code) or code, 15)
        if self.action == "redeem":
            self.redeem_request = ""
        self.action_until = 0.0
        return True

    def drop(self):
        self.invite, self.request = Invite(), None
        if self.invite_request or self.redeem_request:
            self.invite_request = self.redeem_request = ""
            self.say("Invite result unknown; try again")

    def reset(self):
        self.invite, self.request = Invite(), None
        self.invite_request = self.redeem_request = ""
