import json
import os
import secrets
import stat
import subprocess


def default_home():
    return os.path.expanduser("~/.local/share/omaq")


def default_state():
    return os.path.expanduser("~/.local/state/omaq")


def omaq_paths():
    return os.environ.get("OMAQ_HOME") or default_home(), os.environ.get("OMAQ_STATE") or default_state()


def helper_binary():
    return os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__)))), "helper", "omaq")


def ensure_private_dir(path):
    try:
        if not os.path.lexists(path):
            parent = os.path.dirname(path)
            if parent and not os.path.isdir(parent):
                os.makedirs(parent, exist_ok=True)
            try:
                os.mkdir(path, 0o700)
            except FileExistsError:
                pass
        st = os.lstat(path)
    except OSError as error:
        return "%s could not be created: %s" % (path, error.strerror or error)
    if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid():
        return "%s must be a real directory owned by you" % path
    if stat.S_IMODE(st.st_mode) & 0o077:
        return "%s permissions are %03o; run: chmod 700 -- '%s'" % (path, stat.S_IMODE(st.st_mode), path)
    return ""


def read_protocol(state):
    try:
        with open(os.path.join(state, "omaq.protocol"), encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


class HelperProcess:
    def __init__(self, presence, say):
        self.presence, self.say = presence, say
        self.proc = None
        self.owns = False
        self.nonce = ""
        self.log_path = None

    def start(self, home, state):
        binary = helper_binary()
        if not os.access(binary, os.X_OK):
            self.say("OmaQ helper not found: %s" % binary, 30)
            return
        for path in (home, state):
            problem = ensure_private_dir(path)
            if problem:
                self.say(problem, 60)
                return
        self.nonce = secrets.token_hex(16)
        env = dict(os.environ, OMAQ_HOME=home, OMAQ_STATE=state, OMAQ_PROTOCOL_NONCE=self.nonce)
        log_fd, self.log_path = self.presence.log()
        try:
            self.proc = subprocess.Popen([binary], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                         stderr=log_fd if log_fd is not None else subprocess.DEVNULL,
                                         start_new_session=True, close_fds=True)
        except OSError:
            self.proc = None
        finally:
            if log_fd is not None:
                os.close(log_fd)

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def failed(self):
        return self.proc.poll() is not None and self.proc.returncode != 2

    def abandon(self):
        detail = ""
        try:
            with open(self.log_path, "rb") as f:
                f.seek(0, os.SEEK_END)
                f.seek(max(0, f.tell() - 400))
                lines = [line for line in f.read().decode("utf-8", "replace").splitlines() if line.strip()]
            detail = lines[-1] if lines else ""
        except (OSError, AttributeError, TypeError):
            pass
        self.say("Helper could not start" + (": " + detail if detail else ""), 60)
        self.proc = None

    def reap(self):
        if self.proc is not None and self.proc.poll() is not None:
            self.proc = None

    def adopt(self, state, instance):
        running = self.running()
        launched = read_protocol(state)
        self.owns = running and bool(self.nonce) and launched.get("nonce") == self.nonce \
            and launched.get("instance") == instance and launched.get("pid") == self.proc.pid
        if self.owns:
            self.presence.mark_started(instance, self.nonce)
