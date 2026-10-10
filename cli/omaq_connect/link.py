import json
import os
import socket
import stat

from .protocol import LINE_MAX


class Link:
    def __init__(self, path):
        st = os.lstat(path)
        if not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid() or stat.S_IMODE(st.st_mode) & 0o077:
            raise OSError("unsafe helper socket")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
        self.sock.settimeout(2.0)
        try:
            self.sock.connect(path)
        except OSError:
            self.sock.close()
            raise
        self.sock.setblocking(False)
        self.buf = b""

    def fileno(self):
        return self.sock.fileno()

    def send(self, obj):
        line = (json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        if len(line) > LINE_MAX:
            return False
        self.sock.settimeout(2.0)
        try:
            self.sock.sendall(line)
        finally:
            self.sock.setblocking(False)
        return True

    def read(self):
        try:
            data = self.sock.recv(65536)
        except (BlockingIOError, InterruptedError):
            return []
        if not data:
            raise ConnectionError("helper closed the connection")
        self.buf += data
        events = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            try:
                event = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(event, dict):
                events.append(event)
        if len(self.buf) > (1 << 20):
            self.buf = b""
        return events

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
