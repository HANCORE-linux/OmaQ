import json
import os
import stat


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
