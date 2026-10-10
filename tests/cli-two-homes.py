#!/usr/bin/env python3
import importlib.util
import json
import os
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
    env = dict(os.environ, OMAQ_HOME=home, OMAQ_STATE=state)
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
        if not term.wait_for("chat › ", 20) or not term.wait_for("bob", 20):
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
        shot = os.environ.get("CLI_SCREEN")
        if shot:
            with open(shot, "w", encoding="utf-8") as f:
                f.write("\n".join(term.screen()) + "\n")
        term.send(":qa\r")
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
