#!/usr/bin/env python3
import importlib.util
import os
import re
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
    print("cli-invite: " + message, file=sys.stderr)
    sys.exit(1)


def plain(text):
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def main():
    base = tempfile.mkdtemp(prefix="omaq-cli-inv-")
    dirs = {k: os.path.join(base, k) for k in ("ha", "sa", "hb", "sb", "ra", "rb")}
    for d in dirs.values():
        os.makedirs(d, mode=0o700)
    procs, terms = [], []
    try:
        procs = [homes.start(dirs["ha"], dirs["sa"]), homes.start(dirs["hb"], dirs["sb"])]
        a, b = homes.Raw(dirs["sa"]), homes.Raw(dirs["sb"])
        a.send({"op": "status", "id": "o-a"})
        b.send({"op": "status", "id": "o-b"})
        if not a.wait(lambda e: e.get("event") == "snapshot" and e.get("addr"), 15):
            fail("helper has no tox")
        b.send({"op": "nickname.set", "nickname": "bob", "id": "nick-b"})
        env = lambda h, s, r: dict(os.environ, OMAQ_HOME=dirs[h], OMAQ_STATE=dirs[s], XDG_RUNTIME_DIR=dirs[r],
                                   TERM="xterm-256color")
        ta = harness.Term(env("ha", "sa", "ra"), cols=110, rows=40)
        tb = harness.Term(env("hb", "sb", "rb"), cols=110, rows=40)
        terms = [ta, tb]
        if not ta.wait_for("no friends yet", 20) or not tb.wait_for("no friends yet", 20):
            fail("CLIs did not start")
        ta.send(":invite\r")
        invite = a.wait(lambda e: e.get("event") == "invite" and str(e.get("url") or "").startswith("omaq://"), 15)
        if not invite:
            fail("no invite from :invite")
        ta.pump(1.5)
        shown = "".join(row.strip() for row in ta.screen())
        if invite["url"] not in shown or "Invite link sent to the clipboard" not in plain(ta.text()):
            fail("invite overlay or clipboard notice missing:\n" + "\n".join(ta.screen()[:8]))
        if "\x1b]52;c;" not in ta.text():
            fail("no OSC 52 clipboard write")
        ta.send(":invite revoke\r")
        if not ta.wait_for("Revoke this invite? It cannot be used again. y/n", 5):
            fail(":invite revoke did not ask first")
        ta.send("y")
        if not ta.wait_for("Invite revoked", 10):
            fail("the revoke was not confirmed")
        if not a.wait(lambda e: e.get("event") == "invite" and e.get("op") == "revoke" and not e.get("url"), 10):
            fail("the helper did not revoke the invite")
        ta.send(":invite\r")
        invite = a.wait(lambda e: e.get("event") == "invite" and e.get("op") == "create"
                        and str(e.get("url") or "").startswith("omaq://") and e.get("url") != invite["url"], 15)
        if not invite:
            fail("no fresh invite after the revoke")
        tb.send(":redeem %s\r" % invite["url"])
        redeemed = b.wait(lambda e: e.get("event") == "invite.redeemed" and e.get("kind") == "direct", 120)
        if not redeemed:
            fail("bob's :redeem did not go through (network?)")
        if not tb.wait_for("Invite checked. Waiting for the other person to accept.", 10):
            fail("bob does not see the bar's redeem result")
        end = time.monotonic() + 120
        while time.monotonic() < end and "1 request" not in plain(ta.text()):
            ta.pump(0.5)
        if "1 request" not in plain(ta.text()):
            fail("alice's friends bar shows no request")
        ta.send(":requests\r")
        ta.pump(1.0)
        first_half = str(redeemed.get("safety") or "").split(" / ")[0]
        if not first_half or first_half not in "\n".join(ta.screen()):
            fail("alice's request view does not show the same safety code as bob's")
        request = a.wait(lambda e: e.get("event") == "request" and e.get("kind") == "direct" and e.get("key"), 5)
        if not request:
            fail("alice's helper has no request event")
        ta.send(":accept\r")
        if not a.wait(lambda e: e.get("event") == "friend.info" and e.get("key") == request["key"], 60):
            fail("bob did not become alice's friend")
        if not ta.wait_screen(r"❯ .+?  (?:online|offline|typing…)", 30):
            fail("alice's CLI did not open the new friend")
        end = time.monotonic() + 120
        while time.monotonic() < end and "bob" not in ta.screen()[0]:
            ta.pump(0.5)
        if "bob" not in ta.screen()[0]:
            fail("bob's name never reached alice's friends bar:\n" + ta.screen()[0])
        if "1 request" in ta.screen()[0]:
            fail("the accepted request is still shown")
        for term in terms:
            term.send(":quit\r")
            if term.finish() != 0 or "Traceback" in term.text():
                fail("CLI exit failed")
        print("cli-invite: ok")
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
