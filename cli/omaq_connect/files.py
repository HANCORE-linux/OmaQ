import os
import secrets
import time

from .protocol import FAILURES, FILE_ERRORS, TEXT_MAX, is_direct
from .session import bound, friend_of, key_for
from .text import clean_line, home_path, human_size


class FileTransfers:
    def __init__(self, send, say, failed, friends, reload):
        self.send, self.say, self.failed = send, say, failed
        self.friends, self.reload = friends, reload
        self.offers, self.sends = {}, {}
        self.result = ("", "", 0.0)

    def bound(self, ev):
        return bound(self.friends(), ev)

    def key_for(self, conversation):
        return key_for(self.friends(), conversation)

    def send_bound(self, obj):
        conversation = str(obj.get("conversation") or "")
        if is_direct(conversation):
            key = self.key_for(conversation)
            if not key:
                return False
            obj = dict(obj, key=key)
        return self.send(obj)

    def finished(self, conversation, text, seconds):
        self.say(text, seconds)
        self.result = (conversation, clean_line(text), time.monotonic())

    def on_offer(self, ev):
        conversation, fid = str(ev.get("conversation") or ""), str(ev.get("id") or "")
        if fid and is_direct(conversation) and self.bound(ev):
            held = self.offers.get(conversation)
            if held and held.get("unconfirmed"):
                del self.offers[conversation]
                held = None
            if held and held["id"] != fid:
                self.send_bound({"op": "file.cancel", "conversation": conversation, "id": fid})
            elif not held:
                try:
                    size = max(0, int(ev.get("size") or 0))
                except (TypeError, ValueError):
                    size = 0
                name = str(ev.get("name") or "file")
                self.offers[conversation] = {"id": fid, "name": name, "size": size, "accepted": False,
                                             "key": str(ev.get("key") or "")}
                f = friend_of(self.friends(), conversation)
                self.say("File offer from %s: %s (%s)" % (f["name"] if f else "a friend", name, human_size(size)), 15)

    def on_sending(self, ev):
        conversation = str(ev.get("conversation") or "")
        send = self.sends.get(conversation)
        if send and str(ev.get("request") or "") == send["request"] and self.bound(ev):
            send["id"] = str(ev.get("id") or "")
            if send["cancel"] and send["id"]:
                self.send({"op": "file.cancel", "conversation": conversation, "id": send["id"], "key": send["key"]})

    def on_progress(self, ev):
        conversation, fid = str(ev.get("conversation") or ""), str(ev.get("id") or "")
        try:
            done, size = max(0, int(ev.get("done") or 0)), max(0, int(ev.get("size") or 0))
        except (TypeError, ValueError):
            done, size = 0, 0
        if ev.get("dir") == "out":
            target = self.sends.get(conversation)
            match = target is not None and (fid == target["id"] or str(ev.get("request") or "") == target["request"])
        else:
            target = self.offers.get(conversation)
            match = target is not None and fid == target["id"]
        if match and size and self.bound(ev):
            target["done"], target["size"] = min(done, size), size

    def on_result(self, ev):
        name = ev.get("event")
        conversation, fid = str(ev.get("conversation") or ""), str(ev.get("id") or "")
        request, code = str(ev.get("request") or ""), str(ev.get("code") or "")
        keyed = self.bound(ev) if ev.get("key") else True
        if ev.get("dir") == "out":
            send = self.sends.get(conversation)
            if send and keyed and ((request and request == send["request"]) or (fid and fid == send["id"])):
                del self.sends[conversation]
                self.reload(conversation)
                if name == "file.done":
                    self.finished(conversation, FILE_ERRORS.get(code) or "File transfer successful: %s" % send["name"], 15)
                elif name == "file.canceled":
                    self.say("File transfer canceled")
                else:
                    self.say("File transfer failed: %s" % (FILE_ERRORS.get(code) or FAILURES.get(code) or code or "error"), 15)
        else:
            offer = self.offers.get(conversation)
            if offer and fid == offer["id"] and self.bound(ev):
                del self.offers[conversation]
                self.reload(conversation)
                if name == "file.done":
                    self.finished(conversation, "File transfer successful: %s" % home_path(ev.get("path") or offer["name"]), 15)
                elif name == "file.canceled":
                    self.say("File transfer canceled")
                else:
                    self.say("File transfer failed: %s" % (FAILURES.get(code) or code or "error"), 15)

    def on_state(self, ev):
        conversation, fid = str(ev.get("conversation") or ""), str(ev.get("id") or "")
        offer = self.offers.get(conversation)
        if offer and offer.get("unconfirmed") and fid == offer["id"] and self.bound(ev):
            state = str(ev.get("state") or "")
            if state == "offer":
                offer["unconfirmed"], offer["accepted"] = False, False
            elif state == "receiving":
                offer["unconfirmed"], offer["accepted"] = False, True
                try:
                    done, size = max(0, int(ev.get("done") or 0)), max(0, int(ev.get("size") or 0))
                except (TypeError, ValueError):
                    done, size = 0, 0
                if size:
                    offer["done"], offer["size"] = min(done, size), size
            else:
                del self.offers[conversation]
                self.reload(conversation)
                self.say("File transfer status unknown; check the chat")

    def send_file(self, conversation, arg):
        given = arg.strip()
        if len(given) > 1 and given[0] == given[-1] and given[0] in "'\"":
            given = given[1:-1]
        path = os.path.abspath(os.path.expanduser(given)) if given else ""
        if not conversation:
            self.say("No chat is open")
        elif not path:
            self.say("Usage: :sendfile <path>")
        elif not os.path.isfile(path) or not os.access(path, os.R_OK):
            self.say("No readable file at %s" % clean_line(given))
        elif len(path.encode("utf-8")) > TEXT_MAX:
            self.say("Path is too long")
        elif conversation in self.sends:
            self.say(FILE_ERRORS["busy"])
        else:
            request = "cli-file-" + secrets.token_hex(8)
            name = os.path.basename(path)
            if self.send_bound({"op": "file.send", "conversation": conversation, "path": path, "kind": "file",
                                "id": request}):
                self.sends[conversation] = {"request": request, "id": "", "name": name, "cancel": False,
                                            "key": self.key_for(conversation)}
                self.say("Sending %s…" % name, 10)
            else:
                self.failed()

    def answer(self, verb, conversation):
        offer = self.offers.get(conversation)
        if offer and offer["key"] != self.key_for(conversation):
            del self.offers[conversation]
            offer = None
        if not offer:
            self.say("No file offer in this chat")
        elif verb == "acceptfile":
            if offer["accepted"]:
                self.say("Already receiving %s" % clean_line(offer["name"]))
            elif self.send({"op": "file.accept", "conversation": conversation, "id": offer["id"], "key": offer["key"]}):
                offer["accepted"] = True
                self.say("Receiving %s…" % clean_line(offer["name"]), 10)
            else:
                self.failed()
        elif self.send({"op": "file.cancel", "conversation": conversation, "id": offer["id"], "key": offer["key"]}):
            del self.offers[conversation]
            self.say("Declined file: %s" % clean_line(offer["name"]))
        else:
            self.failed()

    def cancel(self, conversation):
        send = self.sends.get(conversation) if conversation else None
        if not send:
            self.say("No file is sending in this chat")
        elif not send["id"]:
            send["cancel"] = True
            self.say("Canceling…")
        elif self.send({"op": "file.cancel", "conversation": conversation, "id": send["id"], "key": send["key"]}):
            self.say("Canceling…")
        else:
            self.failed()

    def resume(self, restarted):
        if restarted:
            if self.sends:
                self.say("File transfer failed: helper_restarted", 15)
            self.offers, self.sends = {}, {}
            return
        for conversation, send in self.sends.items():
            self.send({"op": "file.status", "conversation": conversation, "id": send["request"], "key": send["key"]})
        for conversation, offer in self.offers.items():
            offer["unconfirmed"] = True
            self.send({"op": "file.status", "conversation": conversation, "id": offer["id"], "key": offer["key"]})

    def forget(self, conversation):
        self.offers.pop(conversation, None)
        if self.sends.pop(conversation, None):
            self.say("File transfer failed: %s" % FAILURES["identity_changed"], 15)

    def reset(self):
        self.offers, self.sends = {}, {}
        self.result = ("", "", 0.0)
