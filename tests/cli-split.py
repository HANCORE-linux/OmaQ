#!/usr/bin/env python3
import importlib.util
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, "tests", file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


harness = load("cli_pty", "cli-pty.py")
homes = load("cli_two_homes", "cli-two-homes.py")


def fail(message):
    print("cli-split: " + message, file=sys.stderr)
    sys.exit(1)


def befriend(a, peer, nick):
    peer.send({"op": "nickname.set", "nickname": nick, "id": "nick-" + nick})
    a.events.clear()
    a.send({"op": "invite.create", "ttlSec": 86400, "kind": "direct"})
    invite = a.wait(lambda e: e.get("url"), 15)
    if not invite:
        fail("no invite url for " + nick)
    peer.send({"op": "invite.redeem", "payload": invite["url"]})
    request = a.wait(lambda e: e.get("event") == "request" and e.get("kind") == "direct" and e.get("key"), 120)
    if not request:
        fail("no friend request from %s (network?)" % nick)
    a.send({"op": "contact.decide", "id": "x", "key": request["key"], "accept": True})
    return request["key"]


def main():
    base = tempfile.mkdtemp(prefix="omaq-cli3-")
    dirs = {k: os.path.join(base, k) for k in ("ha", "sa", "hb", "sb", "hc", "sc", "run")}
    for d in dirs.values():
        os.makedirs(d, mode=0o700)
    procs = []
    try:
        procs = [homes.start(dirs["h" + x], dirs["s" + x]) for x in "abc"]
        a, b, c = (homes.Raw(dirs["s" + x]) for x in "abc")
        for raw, tag in ((a, "a"), (b, "b"), (c, "c")):
            raw.send({"op": "status", "id": "t-" + tag})
        if not a.wait(lambda e: e.get("event") == "snapshot" and e.get("addr"), 15):
            fail("helper has no tox")
        key_b = befriend(a, b, "bob")
        key_c = befriend(a, c, "carol")
        ids = {}
        end = time.monotonic() + 180
        while time.monotonic() < end and len(ids) < 2:
            for ev in a.events:
                if ev.get("event") == "friend.info" and ev.get("online") is True and ev.get("key") in (key_b, key_c):
                    ids[ev["key"]] = (ev["id"], ev["key"])
            a.pump(0.5)
        if len(ids) < 2:
            fail("friends did not come online")
        fb = b.wait(lambda e: e.get("event") == "friend.info" and e.get("online") is True and len(str(e.get("key") or "")) == 64, 60)
        fc = c.wait(lambda e: e.get("event") == "friend.info" and e.get("online") is True and len(str(e.get("key") or "")) == 64, 60)
        if not fb or not fc:
            fail("peers do not see A online")
        for target, peer in ((ids[key_b], b), (ids[key_c], c)):
            for i in range(60):
                a.send({"op": "msg.send", "conversation": target[0], "key": target[1], "text": "warmup", "id": "w-%s-%d" % (target[0], i)})
                if peer.wait(lambda e: e.get("event") == "message" and e.get("text") == "warmup", 1.5):
                    break
            else:
                fail("secure session never came up")
        env = dict(os.environ, OMAQ_HOME=dirs["ha"], OMAQ_STATE=dirs["sa"], XDG_RUNTIME_DIR=dirs["run"],
                   TERM="xterm-256color")
        term = harness.Term(env, args=["bob"], cols=120, rows=30)
        if not term.wait_for("chat › ", 20):
            fail("CLI did not open bob")
        term.send(":connect car")
        term.send("\t")
        if ":connect carol" not in term.text():
            fail("tab did not complete the name:\n" + repr(term.text()[-400:]))
        term.send("\r")
        term.pump(1.0)
        screen = term.screen()
        if not any("chat › bob" in row and "chat › carol" in row for row in screen):
            fail("panes are not side by side:\n" + "\n".join(screen))
        term.send("an carol\r")
        if not c.wait(lambda e: e.get("event") == "message" and e.get("text") == "an carol", 30):
            fail("carol did not receive the message from the right pane")
        term.send(b"\x1b[1;3D")
        term.send("an bob\r")
        if not b.wait(lambda e: e.get("event") == "message" and e.get("text") == "an bob", 30):
            fail("alt+left did not move the focus to bob")
        if c.wait(lambda e: e.get("event") == "message" and e.get("text") == "an bob", 2):
            fail("bob's message reached carol")
        mark = len(a.events)
        b.send({"op": "msg.send", "conversation": fb["id"], "key": fb["key"], "text": "von bob", "reply": "", "id": "b-1"})
        c.send({"op": "msg.send", "conversation": fc["id"], "key": fc["key"], "text": "von carol", "reply": "", "id": "c-1"})
        if not term.wait_for("von bob", 30) or not term.wait_for("von carol", 30):
            fail("incoming messages not shown in their panes")
        term.pump(1.5)
        screen = term.screen()
        row_bob = next((r for r in screen if "von bob" in r), "")
        row_carol = next((r for r in screen if "von carol" in r), "")
        if row_bob.find("von bob") > 55 or row_carol.find("von carol") < 60:
            fail("messages landed in the wrong pane:\n" + "\n".join(screen))
        for target in ids.values():
            end = time.monotonic() + 15
            seen = []
            while time.monotonic() < end:
                seen = [e.get("count") for e in a.events[mark:] if e.get("event") == "unread" and e.get("conversation") == target[0]]
                if seen and seen[0] >= 1 and 0 in seen[seen.index(next(c for c in seen if c >= 1)):]:
                    break
                a.pump(0.3)
            else:
                fail("visible pane %s was not marked read after its new message: %r" % (target[0], seen))
        term.send(b"\x1b[1;4C")
        term.pump(0.8)
        moved = term.screen()
        heads = [k for k, r in enumerate(moved) if "chat › bob" in r or "chat › carol" in r]
        if len(heads) != 2 or any("chat › bob" in moved[k] and "chat › carol" in moved[k] for k in heads):
            fail("alt+shift+right did not stack the panes:\n" + "\n".join(moved))
        term.send(":q\r")
        term.pump(0.8)
        if any("bob" in r and "carol" in r and "chat ›" in r for r in term.screen()):
            fail(":q did not close a pane")
        shot = os.environ.get("CLI_SCREEN")
        if shot:
            with open(shot, "w", encoding="utf-8") as f:
                f.write("\n".join(screen) + "\n\n--- after alt+shift+right ---\n" + "\n".join(moved) + "\n")
        term.send(":qa\r")
        if term.finish() != 0 or "Traceback" in term.text():
            fail("CLI exit failed")
        print("cli-split: ok")
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
