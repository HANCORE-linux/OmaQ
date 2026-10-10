#!/usr/bin/env python3
import fcntl
import json
import os
import pty
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import time

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
CLI = os.path.join(ROOT, "cli", "omaq.py")
HELPER = os.path.join(ROOT, "helper", "omaq")


def fail(message):
    print("cli-pty: " + message, file=sys.stderr)
    sys.exit(1)


class Term:
    def __init__(self, env, args=(), cols=100, rows=30):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(sys.executable, [sys.executable, "-I", CLI, *args], env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.out = bytearray()

    def pump(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    return
                if not chunk:
                    return
                self.out.extend(chunk)

    def wait_for(self, needle, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if needle in self.text():
                return True
            self.pump(0.2)
        return needle in self.text()

    def wait_screen(self, pattern, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if any(re.search(pattern, row) for row in self.screen()):
                return True
            self.pump(0.2)
        return any(re.search(pattern, row) for row in self.screen())

    def text(self):
        return self.out.decode("utf-8", "replace")

    def screen(self):
        rows = {}
        text = self.text()
        for match in re.finditer(r"\x1b\[(\d+);1H(.*?)\x1b\[K", text, re.S):
            rows[int(match.group(1))] = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", match.group(2))
        return [rows.get(i, "") for i in range(1, max(rows or [0]) + 1)]

    def send(self, data):
        os.write(self.fd, data.encode("utf-8") if isinstance(data, str) else data)
        self.pump(0.2)

    def finish(self, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.pump(0.2)
                return os.waitstatus_to_exitcode(status)
            self.pump(0.1)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        return None


def helper_pid(state):
    try:
        with open(os.path.join(state, "omaq.pid")) as f:
            pid = int(f.read().split()[0])
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError, IndexError):
        return 0


class Raw:
    def __init__(self, state):
        path = os.path.join(state, "omaq.sock")
        end = time.monotonic() + 15
        while not os.path.exists(path) and time.monotonic() < end:
            time.sleep(0.1)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.sock.settimeout(0.3)
        self.buf = b""

    def send(self, obj):
        self.sock.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode())

    def wait(self, test, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                try:
                    event = json.loads(raw)
                except ValueError:
                    continue
                if test(event):
                    return event
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                return None
            self.buf += data
        return None


def start_helper(env):
    return subprocess.Popen([HELPER], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, start_new_session=True)


def restored(term):
    try:
        lflag = termios.tcgetattr(term.fd)[3]
    except termios.error:
        lflag = None
    if lflag is not None and not (lflag & termios.ICANON and lflag & termios.ECHO):
        fail("terminal left in raw mode")
    text = term.text()
    for seq in ("\x1b[?1049l", "\x1b[?25h", "\x1b[?2004l", "\x1b[?1004l", "\x1b[0 q"):
        if seq not in text:
            fail("terminal not restored: missing %r" % seq)
    if text.rfind("\x1b[0 q") < text.rfind("\x1b[1 q"):
        fail("blinking cursor style was not reset at exit")
    if "Traceback" in text:
        fail("traceback in output")


def wait_gone(state, seconds=10):
    end = time.monotonic() + seconds
    while helper_pid(state) and time.monotonic() < end:
        time.sleep(0.2)
    return not helper_pid(state)


def main():
    if not os.access(HELPER, os.X_OK):
        fail("missing helper/omaq; run make helper")
    real_home = os.path.realpath(os.path.expanduser("~/.local/share/omaq"))
    base = tempfile.mkdtemp(prefix="omaq-cli-")
    home, state, runtime = (os.path.join(base, d) for d in ("home", "state", "run"))
    for d in (home, state, runtime):
        os.makedirs(d, mode=0o700)
    if os.path.realpath(home).startswith(real_home):
        fail("refused real home")
    env = dict(os.environ, OMAQ_HOME=home, OMAQ_STATE=state, XDG_RUNTIME_DIR=runtime, TERM="xterm-256color")
    pid = 0
    try:
        term = Term(env)
        if not term.wait_for("no friends yet", 20):
            fail("no friend list shown:\n" + term.text()[-2000:])
        pid = helper_pid(state)
        if not pid:
            fail("helper was not started by the CLI")
        term.send(":nope\r")
        if not term.wait_for("Unknown command :nope", 5):
            fail("command line did not answer")
        term.send(b"\x1b[200~line one\r\nline two\x1b[201~")
        term.send("\x15")
        term.send("\x03")
        if not term.wait_for("Type :quit and press Enter to quit", 5):
            fail("ctrl+c did not show the :quit hint")
        if os.waitpid(term.pid, os.WNOHANG)[0]:
            fail("ctrl+c quit the CLI")
        term.send(":help\r")
        if not term.wait_for(":connect <name>", 5) or "Quit OmaQ" not in term.text() or ":qa" in term.screen()[-1]:
            fail(":help did not list the commands")
        term.send("\x1b")
        term.pump(0.3)
        term.send(":) ")
        if not term.wait_screen("\U0001F642 ", 5):
            fail("':) ' did not become an emoji:\n" + "\n".join(term.screen()[-3:]))
        term.send("\x0b")
        if not term.wait_screen("emoji › ", 5):
            fail("ctrl+k did not open the emoji picker")
        term.send("fire")
        term.pump(0.3)
        term.send("\r")
        if not term.wait_screen("\U0001F642 \U0001F525", 5):
            fail("the picker did not insert the emoji:\n" + "\n".join(term.screen()[-3:]))
        term.send("\x15")
        term.pump(0.3)
        term.send(":quit\r")
        code = term.finish()
        if code != 0:
            fail("exit code %r\n%s" % (code, term.text()[-2000:]))
        text = term.text()
        if "\x1b[1 q" not in text:
            fail("the cursor was not set to blink")
        for seq in ("\x1b[?1049l", "\x1b[?25h", "\x1b[?2004l", "\x1b[?1004l", "\x1b[0 q"):
            if seq not in text:
                fail("terminal not restored: missing %r" % seq)
        if "Traceback" in text:
            fail("traceback in output")
        end = time.monotonic() + 10
        while helper_pid(state) and time.monotonic() < end:
            time.sleep(0.2)
        if helper_pid(state):
            fail("CLI-only exit left the helper running")
        left = [n for n in os.listdir(os.path.join(runtime, "omaq-cli")) if n != "helper.log"]
        if left:
            fail("presence files left behind: %r" % left)
        term = Term(env, cols=60, rows=12)
        if not term.wait_for("no friends yet", 20):
            fail("second start failed:\n" + term.text()[-1500:])
        fcntl.ioctl(term.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 980, 646))
        term.pump(1.0)
        fcntl.ioctl(term.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 35, 122, 980, 646))
        if not term.wait_screen(r"Text 112%\s*$", 5):
            fail("the bottom line does not show the zoom:\n" + "\n".join(term.screen()[-3:]))
        fcntl.ioctl(term.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 5, 20, 0, 0))
        term.pump(1.0)
        os.kill(term.pid, signal.SIGTERM)
        code = term.finish()
        if code != 0:
            fail("SIGTERM exit code %r\n%s" % (code, term.text()[-1500:]))
        restored(term)
        if not wait_gone(state):
            fail("SIGTERM exit left the CLI-started helper running")
        proc = start_helper(env)
        raw = Raw(state)
        raw.send({"op": "status", "id": "p0"})
        snap = raw.wait(lambda e: e.get("event") == "snapshot" and e.get("request") == "p0", 15)
        if not snap:
            fail("no snapshot from manual helper")
        raw.send({"op": "identity.protect", "passphrase": "correct horse 42", "id": "p1"})
        if not raw.wait(lambda e: e.get("event") == "identity" and e.get("op") == "protect", 15):
            fail("identity.protect failed")
        raw.send({"op": "helper.shutdown", "id": snap["instance"], "request": "p2"})
        proc.wait(timeout=15)
        proc = start_helper(env)
        term = Term(env)
        if not term.wait_for("passphrase ›", 20):
            fail("locked identity did not ask for the passphrase:\n" + term.text()[-1500:])
        term.send("wrong pass 1\r")
        if not term.wait_for("The identity passphrase is incorrect.", 15):
            fail("wrong passphrase not reported")
        if "wrong pass 1" in term.text():
            fail("passphrase was echoed")
        term.send("correct horse 42\r")
        if not term.wait_for("no friends yet", 30):
            fail("unlock did not reach the friend list:\n" + term.text()[-1500:])
        if "correct horse 42" in term.text():
            fail("passphrase was echoed")
        term.send(":q\r")
        if term.finish() != 0:
            fail("exit after unlock failed")
        restored(term)
        if proc.poll() is not None:
            fail("CLI stopped a helper it did not start")
        term = Term(env)
        if not term.wait_for("no friends yet", 20):
            fail("attach to the unlocked foreign helper failed")
        proc.terminate()
        proc.wait(timeout=10)
        term.pump(3.0)
        if helper_pid(state):
            fail("CLI started its own helper while the foreign helper restarted")
        if "Helper not running" not in term.text() and "Reconnecting…" not in term.text():
            fail("lost helper not reported")
        proc = start_helper(env)
        if not term.wait_for("passphrase ›", 20):
            fail("CLI did not reattach to the restarted helper")
        mark = len(term.out)
        term.send("correct horse 42\r")
        if not term.wait_for("no friends yet", 30) or "no friends yet" not in term.out[mark:].decode("utf-8", "replace"):
            fail("reattached CLI did not unlock again")
        os.kill(term.pid, signal.SIGHUP)
        if term.finish() != 0:
            fail("SIGHUP exit failed")
        restored(term)
        if proc.poll() is not None:
            fail("SIGHUP exit stopped a foreign helper")
        proc.terminate()
        proc.wait(timeout=10)
        print("cli-pty: ok")
    finally:
        if helper_pid(state):
            os.kill(helper_pid(state), signal.SIGTERM)
            time.sleep(0.5)
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    main()
