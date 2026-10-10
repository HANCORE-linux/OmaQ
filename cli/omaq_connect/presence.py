import fcntl
import hashlib
import os
import time

from .helper_process import ensure_private_dir


class Presence:
    def __init__(self, state_dir):
        base = os.environ.get("XDG_RUNTIME_DIR")
        if base:
            scope = hashlib.sha256(os.path.realpath(state_dir).encode("utf-8", "surrogateescape")).hexdigest()[:16]
            self.dir = os.path.join(base, "omaq-cli", scope)
        else:
            self.dir = "" if ensure_private_dir(state_dir) else os.path.join(state_dir, "omaq-cli")
        self.lock = None
        if not self.dir:
            return
        try:
            os.makedirs(os.path.dirname(self.dir), mode=0o700, exist_ok=True)
            os.makedirs(self.dir, mode=0o700, exist_ok=True)
            path = os.path.join(self.dir, "%d.lock" % os.getpid())
            fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.lock = (fd, path)
        except OSError:
            self.lock = None

    def others_alive(self):
        alive = False
        try:
            names = os.listdir(self.dir)
        except OSError:
            return False
        for name in names:
            if not name.endswith(".lock") or (self.lock and name == os.path.basename(self.lock[1])):
                continue
            path = os.path.join(self.dir, name)
            try:
                fd = os.open(path, os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW)
            except OSError:
                continue
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                os.unlink(path)
            except OSError:
                alive = True
            finally:
                os.close(fd)
        return alive

    def mark_started(self, instance, nonce):
        if not self.dir:
            return
        try:
            path = os.path.join(self.dir, "started")
            fd = os.open(path + ".tmp", os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write("%s %s" % (instance, nonce))
            os.replace(path + ".tmp", path)
        except OSError:
            pass

    def started(self):
        try:
            with open(os.path.join(self.dir, "started")) as f:
                parts = f.read().split()
        except OSError:
            return "", ""
        return (parts[0], parts[1]) if len(parts) == 2 else ("", "")

    def clear_started(self):
        try:
            os.unlink(os.path.join(self.dir, "started"))
        except OSError:
            pass

    def log(self):
        if not self.dir:
            return None, ""
        path = os.path.join(self.dir, "helper.log")
        try:
            return os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600), path
        except OSError:
            return None, ""

    def exit_lock(self, seconds):
        if not self.dir:
            return None
        try:
            fd = os.open(os.path.join(self.dir, "exit.mutex"), os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
                         0o600)
        except OSError:
            return None
        end = time.monotonic() + seconds
        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fd
            except OSError:
                if time.monotonic() >= end:
                    os.close(fd)
                    return None
                time.sleep(0.05)

    def release(self):
        if self.lock:
            fd, path = self.lock
            try:
                os.unlink(path)
            except OSError:
                pass
            os.close(fd)
            self.lock = None
