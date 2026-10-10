#!/usr/bin/env python3
import importlib.util
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
HELPER = os.path.join(ROOT, "helper", "omaq")
spec = importlib.util.spec_from_file_location("cli_pty", os.path.join(ROOT, "tests", "cli-pty.py"))
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


cli_spec = importlib.util.spec_from_file_location("omaq_cli_glyphs", os.path.join(ROOT, "cli", "omaq.py"))
cli = importlib.util.module_from_spec(cli_spec)
cli_spec.loader.exec_module(cli)
ROLES = cli.Palette().roles
DELIVERED = "\x1b[0;%sm%s" % (ROLES["dim"], cli.RECEIPTS[2])
READ = "\x1b[0;%s;1m%s" % (ROLES["online"], cli.RECEIPTS[3])


def fail(message):
    print("cli-two-homes: " + message, file=sys.stderr)
    sys.exit(1)


class Raw:
    def __init__(self, state):
        path = os.path.join(state, "omaq.sock")
        end = time.monotonic() + 15
        while not os.path.exists(path) and time.monotonic() < end:
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.sock.setblocking(False)
        self.buf = b""
        self.events = []

    def send(self, obj):
        self.sock.sendall((json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n").encode())

    def pump(self, seconds=0.2):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            try:
                data = self.sock.recv(65536)
            except BlockingIOError:
                time.sleep(0.05)
                continue
            if not data:
                return
            self.buf += data
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                try:
                    self.events.append(json.loads(raw))
                except ValueError:
                    pass

    def wait(self, test, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for ev in self.events:
                if test(ev):
                    return ev
            self.pump(0.3)
        return None


def start(home, state):
    env = dict(os.environ, OMAQ_HOME=home, OMAQ_STATE=state, OMAQ_DOWNLOAD_DIR=os.path.join(state, "downloads"))
    return subprocess.Popen([HELPER], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)


def main():
    if not os.access(HELPER, os.X_OK):
        fail("missing helper/omaq; run make helper")
    base = tempfile.mkdtemp(prefix="omaq-cli2-")
    dirs = {k: os.path.join(base, k) for k in ("ha", "sa", "hb", "sb", "run")}
    for d in dirs.values():
        os.makedirs(d, mode=0o700)
    procs = []
    try:
        procs = [start(dirs["ha"], dirs["sa"]), start(dirs["hb"], dirs["sb"])]
        a, b = Raw(dirs["sa"]), Raw(dirs["sb"])
        a.send({"op": "status", "id": "t-a"})
        b.send({"op": "status", "id": "t-b"})
        if not a.wait(lambda e: e.get("event") == "snapshot" and e.get("addr"), 15):
            fail("helper has no tox")
        b.send({"op": "nickname.set", "nickname": "bob", "id": "nick-b"})
        a.send({"op": "invite.create", "ttlSec": 86400, "kind": "direct"})
        invite = a.wait(lambda e: e.get("url"), 15)
        if not invite:
            fail("no invite url")
        b.send({"op": "invite.redeem", "payload": invite["url"]})
        request = a.wait(lambda e: e.get("event") == "request" and e.get("kind") == "direct" and e.get("key"), 120)
        if not request:
            fail("no friend request (network?)")
        a.send({"op": "contact.decide", "id": "x", "key": request["key"], "accept": True})
        if not a.wait(lambda e: e.get("event") == "contact.decided" and e.get("request") == "x" and e.get("ok") is True, 30):
            fail("the helper did not report the decision with its request")
        online = lambda r: r.wait(lambda e: e.get("event") == "friend.info" and e.get("id") == "0"
                                  and e.get("online") is True and len(str(e.get("key") or "")) == 64, 180)
        fa, fb = online(a), online(b)
        if not fa or not fb:
            fail("friends did not come online")
        for i in range(60):
            a.send({"op": "msg.send", "conversation": "0", "key": fa["key"], "text": "warmup", "id": "warm-%d" % i})
            if b.wait(lambda e: e.get("event") == "message" and e.get("text") == "warmup", 1.5):
                break
        else:
            fail("secure session never came up")
        env = dict(os.environ, OMAQ_HOME=dirs["ha"], OMAQ_STATE=dirs["sa"], XDG_RUNTIME_DIR=dirs["run"],
                   TERM="xterm-256color")
        term = harness.Term(env, args=["bob"], cols=110, rows=30)
        if not term.wait_screen(r"❯ bob  ", 20):
            fail("CLI did not open the chat with bob:\n" + term.text()[-1500:])
        term.send("hallo von der cli\r")
        got = b.wait(lambda e: e.get("event") == "message" and e.get("text") == "hallo von der cli", 30)
        if not got:
            fail("bob did not receive the CLI message:\n" + term.text()[-1500:])
        if not term.wait_for(DELIVERED, 30) and READ not in term.text():
            fail("no delivered receipt glyph in the CLI")
        mark_read = len(term.out)
        b.send({"op": "conversation.read", "conversation": "0", "key": fb["key"]})
        if not term.wait_for(READ, 30) or READ not in term.out[mark_read:].decode("utf-8", "replace"):
            fail("no read receipt glyph in the CLI")
        b.send({"op": "typing.set", "conversation": "0", "key": fb["key"], "typing": True})
        if not term.wait_for("typing…", 15):
            fail("typing indicator not shown")
        mark = len(a.events)
        nasty = "evil\x7fdel\u0085c1\u202ebidi!"
        b.send({"op": "msg.send", "conversation": "0", "key": fb["key"], "text": nasty, "reply": "", "id": "nasty-1"})
        if not term.wait_for("bidi!", 30):
            fail("peer message not shown")
        tail = term.text()[-6000:]
        if any(raw in tail for raw in ("\x7f", "\u0085", "\u202e")):
            fail("raw control characters reached the terminal")
        if "evil␡del�c1�bidi!" not in tail:
            fail("placeholders missing:\n" + repr(tail[-600:]))
        def after(test, seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                for k in range(mark, len(a.events)):
                    if test(a.events[k]):
                        return k
                a.pump(0.3)
            return -1
        rose = after(lambda e: e.get("event") == "unread" and e.get("conversation") == "0" and e.get("count", 0) >= 1, 15)
        cleared = after(lambda e: e.get("event") == "unread" and e.get("conversation") == "0" and e.get("count") == 0, 15)
        if rose < 0 or cleared < rose:
            fail("CLI did not mark the new message read (rose %d, cleared %d)" % (rose, cleared))
        term.send(b"\x1b[A")
        term.pump(0.6)
        if "hallo von der cli" not in term.screen()[-2]:
            fail("up did not recall the last sent message:\n" + "\n".join(term.screen()[-3:]))
        term.send(b"\x1b")
        term.pump(0.4)
        mark_clear = len(a.events)
        term.send(":clear\r")
        if not term.wait_for("Clear this chat? y/n", 5):
            fail(":clear did not ask first")
        term.send("y")
        if after(lambda e: e.get("event") == "history.clear.succeeded" and e.get("conversation") == "0", 15) < mark_clear:
            fail("the helper did not confirm the clear")
        if not term.wait_for("no messages yet", 15):
            fail("the cleared chat still shows messages")
        term.send(":) hallo <3\r")
        if not b.wait(lambda e: e.get("event") == "message" and e.get("text") == "\U0001F642 hallo \u2764\ufe0f", 30):
            fail("the emoticons did not arrive as emoji")
        payload = os.urandom(3000)
        outgoing = os.path.join(base, "cli file.bin")
        with open(outgoing, "wb") as f:
            f.write(payload)
        term.send(":sendfile %s\r" % outgoing)
        offer = b.wait(lambda e: e.get("event") == "file.offer" and e.get("dir") == "in", 60)
        if not offer:
            fail(":sendfile did not offer the file to bob")
        b.send({"op": "file.accept", "conversation": "0", "key": fb["key"], "id": offer["id"]})
        done = b.wait(lambda e: e.get("event") == "file.done" and e.get("dir") == "in" and e.get("id") == offer["id"], 120)
        if not done or not done["path"].startswith(os.path.join(dirs["sb"], "downloads") + "/"):
            fail("bob's copy did not land in the test download folder: %r" % done)
        with open(done["path"], "rb") as f:
            if f.read() != payload:
                fail("bob received different bytes")
        if not term.wait_for("File transfer successful: cli file.bin", 60):
            fail("the CLI did not report the sent file")
        incoming = os.path.join(base, "from-bob.txt")
        content = ("hallo datei " * 300).encode()
        with open(incoming, "wb") as f:
            f.write(content)
        mark = len(term.out)
        b.send({"op": "file.send", "conversation": "0", "key": fb["key"], "path": incoming, "kind": "file", "id": "bob-f1"})
        if not term.wait_for("File offer from bob: from-bob.txt", 60):
            fail("the CLI did not show bob's file offer")
        term.send(":acceptfile\r")
        got = a.wait(lambda e: e.get("event") == "file.done" and e.get("dir") == "in", 120)
        if not got or not got["path"].startswith(os.path.join(dirs["sa"], "downloads") + "/"):
            fail("alice's copy did not land in the test download folder: %r" % got)
        with open(got["path"], "rb") as f:
            if f.read() != content:
                fail("alice received different bytes")
        end = time.monotonic() + 30
        while time.monotonic() < end and got["path"] not in term.out[mark:].decode("utf-8", "replace"):
            term.pump(0.5)
        if got["path"] not in term.out[mark:].decode("utf-8", "replace"):
            fail("the CLI did not report where the file was saved")
        blob = os.urandom(1500000)
        big = os.path.join(base, "big.bin")
        with open(big, "wb") as f:
            f.write(blob)
        mark = len(term.out)
        since_a, since_b = len(a.events), len(b.events)

        def fresh(raw, since, test, seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                for ev in raw.events[since:]:
                    if test(ev):
                        return ev
                term.pump(0.05)
                raw.pump(0.2)
            return None
        term.send(":sendfile %s\r" % big)
        offer = fresh(b, since_b, lambda e: e.get("event") == "file.offer" and e.get("name") == "big.bin", 60)
        if not offer:
            fail(":sendfile did not offer the big file")
        b.send({"op": "file.accept", "conversation": "0", "key": fb["key"], "id": offer["id"]})
        done = fresh(b, since_b, lambda e: e.get("event") == "file.done" and e.get("dir") == "in"
                     and e.get("id") == offer["id"], 300)
        if not done:
            fail("the big file did not arrive")
        with open(done["path"], "rb") as f:
            got = f.read()
        if got != blob:
            fail("bob received different bytes for the big file: %s, %d of %d bytes" % (done["path"], len(got), len(blob)))
        steps = [e["done"] for e in b.events[since_b:] if e.get("event") == "file.progress" and e.get("id") == offer["id"]
                 and e.get("dir") == "in" and e.get("size") == len(blob)]
        if not steps or steps != sorted(steps) or not 0 < steps[0] <= steps[-1] < len(blob) or len(steps) > 101:
            fail("bad incoming progress events: %d %r" % (len(steps), steps[:5]))
        a.pump(1.0)
        sent = [e for e in a.events[since_a:] if e.get("event") == "file.progress" and e.get("dir") == "out"]
        if not sent:
            fail("alice's helper sent no outgoing progress events")
        rendered = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", term.out[mark:].decode("utf-8", "replace"))
        if not re.search(r"Sending big\.bin\s+[\u2588\u2591]+ \d+%", rendered):
            starts = [e for e in a.events[since_a:] if e.get("event") == "file.sending"]
            fail("the CLI never drew a progress bar: plain line %s, sending %r, progress %d %r" % (
                "Sending big.bin" in rendered, starts[:1], len(sent), sent[:2]))
        inbox = os.path.join(dirs["sa"], "downloads", "omaq")
        if not os.path.isdir(inbox):
            fail("alice's download folder is missing: %s" % inbox)
        blocked = os.path.join(base, "blocked.txt")
        with open(blocked, "wb") as f:
            f.write(b"blocked")
        since_a = len(a.events)
        os.chmod(inbox, 0o500)
        try:
            b.send({"op": "file.send", "conversation": "0", "key": fb["key"], "path": blocked, "kind": "file",
                    "id": "bob-f2"})
            offered = fresh(a, since_a, lambda e: e.get("event") == "file.offer" and e.get("name") == "blocked.txt", 60)
            if not offered or not term.wait_for("File offer from bob: blocked.txt", 30):
                fail("the CLI did not show the offer for the blocked folder")
            term.send(":acceptfile\r")
            failed = fresh(a, since_a, lambda e: e.get("event") == "file.failed" and e.get("id") == offered["id"], 30)
            if not failed or (failed.get("code"), failed.get("dir"), failed.get("key")) != ("accept_failed", "in",
                                                                                          fa["key"]):
                fail("the helper sent no correlated accept failure: %r" % failed)
            if not term.wait_for("File transfer failed: accept_failed", 15):
                fail("the CLI did not report the failed accept")
        finally:
            os.chmod(inbox, 0o700)
        after_file = os.path.join(base, "after.txt")
        with open(after_file, "wb") as f:
            f.write(b"after")
        b.send({"op": "file.send", "conversation": "0", "key": fb["key"], "path": after_file, "kind": "file",
                "id": "bob-f3"})
        if not term.wait_for("File offer from bob: after.txt", 60):
            fail("the CLI dropped the next offer after a failed accept")
        since_a = len(a.events)
        term.send(":acceptfile\r")
        arrived = fresh(a, since_a, lambda e: e.get("event") == "file.done" and e.get("dir") == "in"
                        and str(e.get("path") or "").endswith("after.txt"), 120)
        if not arrived:
            fail("the transfer after a failed accept did not complete")
        with open(arrived["path"], "rb") as f:
            if f.read() != b"after":
                fail("the transfer after a failed accept delivered different bytes")

        def from_bob(name, payload, request):
            path = os.path.join(base, name)
            with open(path, "wb") as f:
                f.write(payload)
            since = len(a.events)
            b.send({"op": "file.send", "conversation": "0", "key": fb["key"], "path": path, "kind": "file",
                    "id": request})
            offered = fresh(a, since, lambda e: e.get("event") == "file.offer" and e.get("name") == name, 60)
            if not offered or not term.wait_for("File offer from bob: %s" % name, 30):
                fail("the CLI did not show bob's %s" % name)
            return offered, since

        first, since_a = from_bob("x1.txt", b"one", "bob-x1")
        a.send({"op": "file.status", "conversation": "0", "key": fa["key"], "id": first["id"]})
        state = fresh(a, since_a, lambda e: e.get("event") == "file.state" and e.get("id") == first["id"], 15)
        if not state or (state.get("state"), state.get("name"), state.get("dir")) != ("offer", "x1.txt", "in"):
            fail("file.status did not report the pending incoming offer: %r" % state)
        term.send(":declinefile\r")
        if not term.wait_for("Declined file: x1.txt", 10):
            fail("the CLI did not decline x1.txt")
        since_a = len(a.events)
        a.send({"op": "file.status", "conversation": "0", "key": fa["key"], "id": first["id"]})
        gone = fresh(a, since_a, lambda e: e.get("event") == "file.state" and e.get("id") == first["id"], 15)
        if not gone or gone.get("state") != "unknown":
            fail("file.status did not report the declined transfer as unknown: %r" % gone)
        payload = os.urandom(200000)
        second, since_a = from_bob("x2.txt", payload, "bob-x2")
        term.send(":acceptfile\r")
        arrived = fresh(a, since_a, lambda e: e.get("event") == "file.done" and e.get("dir") == "in"
                        and str(e.get("path") or "").endswith("x2.txt"), 120)
        if not arrived:
            fail("a file sent right after a decline did not arrive (cancel echo)")
        with open(arrived["path"], "rb") as f:
            if f.read() != payload:
                fail("the file after a decline delivered different bytes")
        y1 = os.path.join(base, "y1.txt")
        with open(y1, "wb") as f:
            f.write(b"y1")
        since_b, mark = len(b.events), len(term.out)
        term.send(":sendfile %s\r" % y1)
        offered = fresh(b, since_b, lambda e: e.get("event") == "file.offer" and e.get("name") == "y1.txt", 60)
        if not offered:
            fail(":sendfile did not offer y1.txt")
        b.send({"op": "file.cancel", "conversation": "0", "key": fb["key"], "id": offered["id"]})
        end = time.monotonic() + 30
        while time.monotonic() < end and "File transfer canceled" not in term.out[mark:].decode("utf-8", "replace"):
            term.pump(0.3)
        if "File transfer canceled" not in term.out[mark:].decode("utf-8", "replace"):
            fail("the CLI did not report bob's decline")
        y2 = os.path.join(base, "y2.txt")
        payload = os.urandom(200000)
        with open(y2, "wb") as f:
            f.write(payload)
        since_b = len(b.events)
        term.send(":sendfile %s\r" % y2)
        offered = fresh(b, since_b, lambda e: e.get("event") == "file.offer" and e.get("name") == "y2.txt", 60)
        if not offered:
            fail(":sendfile did not offer y2.txt after the decline")
        b.send({"op": "file.accept", "conversation": "0", "key": fb["key"], "id": offered["id"]})
        arrived = fresh(b, since_b, lambda e: e.get("event") == "file.done" and e.get("dir") == "in"
                        and e.get("id") == offered["id"], 120)
        if not arrived:
            fail("a file sent right after the peer's decline did not arrive (cancel echo)")
        with open(arrived["path"], "rb") as f:
            if f.read() != payload:
                fail("bob received different bytes after his decline")
        shot = os.environ.get("CLI_SCREEN")
        if shot:
            with open(shot, "w", encoding="utf-8") as f:
                f.write("\n".join(term.screen()) + "\n")
        term.send(":quit\r")
        code = term.finish()
        if code != 0 or "Traceback" in term.text():
            fail("CLI exit %r" % code)
        if procs[0].poll() is not None:
            fail("CLI stopped a helper it did not start")
        a.send({"op": "status", "id": "t-a2"})
        if not a.wait(lambda e: e.get("event") == "snapshot" and e.get("request") == "t-a2", 10):
            fail("helper A no longer answers after the CLI left")
        print("cli-two-homes: ok")
    finally:
        for p in procs:
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        for p in procs:
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    main()
