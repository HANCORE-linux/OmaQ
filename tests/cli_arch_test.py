#!/usr/bin/env python3
import ast
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
PACKAGE = os.path.join(ROOT, "cli", "omaq_connect")
ENTRY = os.path.join(ROOT, "cli", "omaq.py")
SYSTEM = {"socket", "termios", "tty", "subprocess", "fcntl", "select", "signal"}
PATCHED = {"time", "os", "subprocess", "select"}
ALLOWED = {
    "text": set(),
    "protocol": set(),
    "layout": set(),
    "keys": set(),
    "palette": {"text"},
    "link": {"protocol"},
    "helper_process": set(),
    "presence": {"helper_process"},
    "terminal": {"text"},
    "system": set(),
    "session": {"protocol"},
    "files": {"protocol", "session", "text"},
    "invites": {"protocol"},
    "commands": set(),
    "view": {"commands", "protocol", "text"},
    "modals": set(),
}
PURE = {"text", "protocol", "layout", "keys", "palette", "session", "files", "invites", "commands", "view", "modals"}
failures = []


def imports(path):
    tree = ast.parse(open(path, encoding="utf-8").read(), path)
    stdlib, inner = set(), set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                root = alias.name.split(".")[0]
                if root == "omaq_connect":
                    inner.add(alias.name.split(".")[1] if "." in alias.name else "")
                else:
                    stdlib.add(root)
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                if node.module:
                    inner.add(node.module.split(".")[0])
                else:
                    inner.update(alias.name for alias in node.names)
            elif node.module and node.module.split(".")[0] == "omaq_connect":
                parts = node.module.split(".")
                inner.add(parts[1] if len(parts) > 1 else "")
            elif node.module:
                if node.module.split(".")[0] in PATCHED:
                    failures.append("%s: 'from %s import' hides the module from test patches"
                                    % (os.path.relpath(path, ROOT), node.module))
                stdlib.add(node.module.split(".")[0])
    return stdlib, inner


def main():
    modules = sorted(name[:-3] for name in os.listdir(PACKAGE) if name.endswith(".py") and name != "__init__.py")
    for name in modules:
        if name not in ALLOWED:
            failures.append("cli/omaq_connect/%s.py has no layer rule in tests/cli_arch_test.py" % name)
            continue
        stdlib, inner = imports(os.path.join(PACKAGE, name + ".py"))
        if "" in inner:
            failures.append("%s imports the package root" % name)
        for other in sorted(inner - {""} - ALLOWED[name]):
            failures.append("%s must not import %s" % (name, other))
        if name in PURE:
            for module in sorted(stdlib & SYSTEM):
                failures.append("%s is pure and must not import %s" % (name, module))
    imports(os.path.join(PACKAGE, "__init__.py"))
    imports(ENTRY)
    seen, active = set(), set()

    def visit(name, path):
        if name in active:
            failures.append("import cycle: %s" % " -> ".join(path + [name]))
            return
        if name in seen:
            return
        active.add(name)
        for other in sorted(ALLOWED.get(name, ())):
            visit(other, path + [name])
        active.discard(name)
        seen.add(name)

    for name in ALLOWED:
        visit(name, [])
    if failures:
        for failure in failures:
            print("cli-arch: %s" % failure, file=sys.stderr)
        return 1
    print("cli-arch: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
