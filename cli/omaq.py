#!/usr/bin/env python3
import importlib.util
import json
import os
import secrets
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.realpath(__file__))
if "omaq_connect" not in sys.modules:
    _spec = importlib.util.spec_from_file_location("omaq_connect", os.path.join(HERE, "omaq_connect", "__init__.py"),
                                                   submodule_search_locations=[os.path.join(HERE, "omaq_connect")])
    _package = importlib.util.module_from_spec(_spec)
    sys.modules["omaq_connect"] = _package
    try:
        _spec.loader.exec_module(_package)
    except BaseException:
        del sys.modules["omaq_connect"]
        raise
from omaq_connect import *


def link_path():
    return os.path.join(os.path.expanduser("~"), ".local", "bin", COMMAND)


def ours(target):
    if not target.endswith(os.path.join("cli", "omaq.py")):
        return False
    root = os.path.dirname(os.path.dirname(target))
    if not os.path.exists(target):
        return os.path.basename(root) == "hancore.omaq"
    try:
        with open(os.path.join(root, "manifest.json"), encoding="utf-8") as f:
            return json.load(f).get("id") == "hancore.omaq"
    except (OSError, ValueError, AttributeError):
        return False


def install_link():
    path = link_path()
    target = os.path.realpath(__file__)
    if os.path.lexists(path):
        if not os.path.islink(path) or not ours(os.readlink(path)):
            print("%s: %s exists and is not OmaQ's; left unchanged" % (COMMAND, path), file=sys.stderr)
            return 1
        if os.readlink(path) == target:
            print("%s: %s is current" % (COMMAND, path))
            return 0
    os.makedirs(os.path.dirname(path), mode=0o755, exist_ok=True)
    temporary = "%s.%s.tmp" % (path, secrets.token_hex(4))
    os.symlink(target, temporary)
    try:
        os.replace(temporary, path)
    except OSError:
        os.unlink(temporary)
        raise
    print("%s: linked %s" % (COMMAND, path))
    return 0


def remove_link():
    path = link_path()
    if not os.path.islink(path):
        return 0
    if not ours(os.readlink(path)):
        print("%s: %s is not OmaQ's; left unchanged" % (COMMAND, path), file=sys.stderr)
        return 1
    os.unlink(path)
    print("%s: removed %s" % (COMMAND, path))
    return 0


def main(argv):
    if any(a in ("-h", "--help") for a in argv):
        print("usage: %s [friend]\n       %s --link | --unlink\n\n"
              "Open the OmaQ chat in this terminal, optionally with that friend.\n"
              "--link creates ~/.local/bin/%s; --unlink removes it if it is OmaQ's." % (COMMAND, COMMAND, COMMAND))
        return 0
    if argv == ["--link"]:
        return install_link()
    if argv == ["--unlink"]:
        return remove_link()
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        print("%s needs an interactive terminal" % COMMAND, file=sys.stderr)
        return 2
    try:
        App(" ".join(argv).strip()).run()
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
