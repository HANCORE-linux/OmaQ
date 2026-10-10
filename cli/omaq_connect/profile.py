import secrets
import time

NICKNAME_MAX = 18
NICKNAME_SECONDS = 10
NICKNAME_ERRORS = {
    "nickname_invalid": "Nickname must contain 1–18 valid characters.",
    "nickname_update_failed": "Nickname update failed. Try again.",
    "nickname_group_sync_failed": "Nickname saved; group updates are unconfirmed.",
    "unsupported": "Nickname update is unavailable.",
}


def nickname_ok(name):
    return 0 < len(name) <= NICKNAME_MAX and not any(ord(ch) < 0x20 or 0x7f <= ord(ch) <= 0x9f for ch in name)


class Profile:
    def __init__(self, send, say, failed):
        self.send, self.say, self.failed = send, say, failed
        self.nickname = ""
        self.request, self.until = "", 0.0

    def set_nickname(self, arg):
        name = arg.strip()
        if not name:
            self.say("Your nickname: %s" % self.nickname if self.nickname
                     else "No nickname yet; :nickname <name> sets one")
        elif not nickname_ok(name):
            self.say(NICKNAME_ERRORS["nickname_invalid"])
        else:
            self.request = "cli-nickname-" + secrets.token_hex(8)
            if self.send({"op": "nickname.set", "nickname": name, "id": self.request}):
                self.until = time.monotonic() + NICKNAME_SECONDS
                self.say("Saving nickname…", NICKNAME_SECONDS)
            else:
                self.request = ""
                self.failed()

    def on_snapshot(self, ev):
        if "nickname" in ev:
            self.nickname = str(ev.get("nickname") or "")

    def on_nickname(self, ev):
        self.nickname = str(ev.get("value") or "")
        if self.request and str(ev.get("request") or "") == self.request:
            self.request, self.until = "", 0.0
            self.say("Nickname changed to %s" % self.nickname)

    def on_correlated_error(self, code, request):
        if not (self.request and request == self.request):
            return False
        self.request, self.until = "", 0.0
        self.say(NICKNAME_ERRORS.get(code) or NICKNAME_ERRORS["nickname_update_failed"], 15)
        return True

    def tick(self, now):
        if self.request and now > self.until:
            self.request, self.until = "", 0.0
            self.say("Nickname update timed out. Try again.", 15)

    def drop(self):
        if self.request:
            self.request, self.until = "", 0.0
            self.say("Nickname result unknown; try again")

    def reset(self):
        self.nickname, self.request, self.until = "", "", 0.0
