#!/usr/bin/env python3
import importlib.util
import json
import operator
import os
import re
import sys
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
spec = importlib.util.spec_from_file_location("omaq_cli", os.path.join(ROOT, "cli", "omaq.py"))
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


class Sanitize(unittest.TestCase):
    def test_controls_become_visible(self):
        shown = cli.clean("a\rb\x1b[31mc\x7fd\x85e\u202ef\u2066g\x00h")
        for raw in ("\r", "\x1b", "\x7f", "\x85", "\u202e", "\u2066", "\x00"):
            self.assertNotIn(raw, shown)
        self.assertIn("␍", shown)
        self.assertIn("␛", shown)
        self.assertIn("␡", shown)
        self.assertEqual(shown.count("�"), 3)

    def test_newline_kept_only_in_messages(self):
        self.assertEqual(cli.clean("a\nb"), "a\nb")
        self.assertEqual(cli.clean_line("a\nb"), "a b")

    def test_outgoing_keeps_text_and_drops_controls(self):
        self.assertEqual(cli.outgoing("a\r\nb\rc\x01d\te"), "a\nb\ncd\te")
        self.assertEqual(cli.outgoing("ä ö 👍"), "ä ö 👍")


class Layout(unittest.TestCase):
    def test_width(self):
        self.assertEqual(cli.text_width("abc"), 3)
        self.assertEqual(cli.text_width("日本"), 4)
        self.assertEqual(cli.text_width("é"), 1)

    def test_wrap_respects_width(self):
        for width in (1, 5, 12):
            for line in cli.wrap("hallo welt das ist ein sehr langes wort: donaudampfschifffahrt", width):
                self.assertLessEqual(cli.text_width(line), width)

    def test_wrap_keeps_paragraphs(self):
        self.assertEqual(cli.wrap("a\n\nb", 10), ["a", "", "b"])

    def test_clip(self):
        self.assertEqual(cli.clip("日本語", 5), ("日本", 4))


class Protocol(unittest.TestCase):
    def test_direct_ids(self):
        self.assertTrue(cli.is_direct("0"))
        self.assertTrue(cli.is_direct("12"))
        self.assertFalse(cli.is_direct("012"))
        self.assertFalse(cli.is_direct("g:" + "a" * 64))
        self.assertFalse(cli.is_direct(""))

    def test_history_items(self):
        m = cli.message_from({"id": "x", "dir": "out", "text": "hi", "receipt": "read", "ts": 1700000000})
        self.assertEqual((m.ack, m.dir), (3, "out"))
        m = cli.message_from({"id": "y", "from": "me", "text": "hi"})
        self.assertEqual((m.ack, m.dir), (1, "out"))
        m = cli.message_from({"id": "z", "dir": "in", "kind": "file", "text": "a.pdf", "ts": 1700000000000})
        self.assertEqual((m.text, m.ts), ("[file] a.pdf", 1700000000.0))

    def test_binding(self):
        app = cli.App.__new__(cli.App)
        app.friends = [{"id": "0", "key": "k" * 64, "name": "alice", "online": True, "status": "online"}]
        self.assertTrue(app.bound({"conversation": "0", "key": "k" * 64}))
        self.assertFalse(app.bound({"conversation": "0", "key": "x" * 64}))
        self.assertFalse(app.bound({"conversation": "0"}))
        self.assertFalse(app.bound({"conversation": "1", "key": "k" * 64}))
        self.assertTrue(app.bound({"conversation": "g:" + "a" * 64}))


class Panes(unittest.TestCase):
    def layout(self, *names):
        layout = cli.Layout()
        for name in names:
            layout.add(cli.Pane(name))
        return layout

    def test_add_focus_close(self):
        layout = self.layout("a", "b", "c")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["a"], ["b"], ["c"]])
        self.assertEqual(layout.focused().conv, "c")
        layout.focus("left")
        self.assertEqual(layout.focused().conv, "b")
        layout.add(cli.Pane("d"), vertical=False)
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["a"], ["b", "d"], ["c"]])
        layout.focus("up")
        self.assertEqual(layout.focused().conv, "b")
        self.assertEqual(layout.close().conv, "b")
        self.assertEqual(layout.focused().conv, "d")
        layout.close()
        layout.close()
        self.assertIsNone(layout.close())
        self.assertEqual(len(layout.panes()), 1)

    def test_move_between_columns(self):
        layout = self.layout("a", "b")
        layout.move("left")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["b", "a"]])
        self.assertEqual(layout.focused().conv, "b")
        layout.move("up")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["b", "a"]])
        layout.move("down")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["a", "b"]])
        layout.move("right")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["a"], ["b"]])
        self.assertEqual(layout.focused().conv, "b")
        layout.move("right")
        self.assertEqual([[p.conv for p in col] for col in layout.columns], [["a"], ["b"]])

    def test_geometry_fits_and_falls_back(self):
        layout = self.layout("a", "b", "c")
        boxes = layout.geometry(100, 20)
        self.assertEqual([b[5] for b in boxes], [32, 32, 34])
        self.assertEqual(sum(b[5] for b in boxes) + 2, 100)
        self.assertIsNone(layout.geometry(60, 20))
        layout.resize(3.0)
        self.assertLessEqual(max(layout.weights), 3.0)

    def test_day_labels_and_clock(self):
        now = cli.time.mktime((2026, 10, 10, 15, 0, 0, 0, 0, -1))
        at = lambda *d: cli.time.mktime(d + (0, 0, -1))
        self.assertEqual(cli.day_label(at(2026, 10, 10, 9, 12, 0), now), "today")
        self.assertEqual(cli.day_label(at(2026, 10, 9, 23, 59, 0), now), "yesterday")
        self.assertEqual(cli.day_label(at(2026, 10, 8, 14, 5, 0), now), "Thu 8 Oct")
        self.assertEqual(cli.day_label(at(2025, 12, 31, 8, 0, 0), now), "Wed 31 Dec 2025")
        self.assertEqual(cli.day_label(at(2026, 1, 1, 8, 0, 0), cli.time.mktime((2026, 1, 2, 9, 0, 0, 0, 0, -1))), "yesterday")
        self.assertEqual(cli.clock(at(2026, 10, 8, 14, 5, 0)), "14:05")
        self.assertEqual(cli.clock(0), "--:--")


class Link(unittest.TestCase):
    def setUp(self):
        self.home = cli.os.path.realpath(__import__("tempfile").mkdtemp(prefix="omaq-link-"))
        self.old = cli.os.environ.get("HOME")
        cli.os.environ["HOME"] = self.home

    def tearDown(self):
        cli.os.environ["HOME"] = self.old
        __import__("shutil").rmtree(self.home, ignore_errors=True)

    def test_link_lifecycle(self):
        path = cli.link_path()
        self.assertTrue(path.startswith(self.home))
        self.assertEqual(cli.install_link(), 0)
        self.assertEqual(cli.os.readlink(path), cli.os.path.realpath(cli.__file__))
        self.assertEqual(cli.install_link(), 0)
        self.assertEqual(cli.remove_link(), 0)
        self.assertFalse(cli.os.path.lexists(path))
        self.assertEqual(cli.remove_link(), 0)

    def test_foreign_paths_are_kept(self):
        path = cli.link_path()
        cli.os.makedirs(cli.os.path.dirname(path))
        with open(path, "w") as f:
            f.write("mine")
        self.assertEqual(cli.install_link(), 1)
        with open(path) as f:
            self.assertEqual(f.read(), "mine")
        cli.os.unlink(path)
        cli.os.symlink("/usr/bin/true", path)
        self.assertEqual(cli.install_link(), 1)
        self.assertEqual(cli.remove_link(), 1)
        self.assertEqual(cli.os.readlink(path), "/usr/bin/true")

    def test_stale_omaq_link_is_replaced(self):
        path = cli.link_path()
        cli.os.makedirs(cli.os.path.dirname(path))
        cli.os.symlink("/gone/plugins/hancore.omaq/cli/omaq.py", path)
        self.assertEqual(cli.install_link(), 0)
        self.assertEqual(cli.os.readlink(path), cli.os.path.realpath(cli.__file__))


class InstallerHook(unittest.TestCase):
    def test_source_installer_links_the_installed_tree(self):
        import shutil
        import tempfile
        from pathlib import Path
        spec = importlib.util.spec_from_file_location("omaq_source_install_hook", os.path.join(ROOT, "scripts", "install-omaq.py"))
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        base = os.path.realpath(tempfile.mkdtemp(prefix="omaq-hook-"))
        old = os.environ.get("HOME")
        try:
            os.environ["HOME"] = os.path.join(base, "home")
            root = Path(base) / "plugins" / "hancore.omaq"
            (root / "cli").mkdir(parents=True)
            shutil.copy2(os.path.join(ROOT, "cli", "omaq.py"), root / "cli" / "omaq.py")
            shutil.copytree(os.path.join(ROOT, "cli", "omaq_connect"), root / "cli" / "omaq_connect",
                            ignore=shutil.ignore_patterns("__pycache__"))
            shutil.copy2(os.path.join(ROOT, "manifest.json"), root / "manifest.json")
            module.link_cli(root)
            link = os.path.join(base, "home", ".local", "bin", "omaq-connect")
            self.assertEqual(os.readlink(link), str(root / "cli" / "omaq.py"))
            module.link_cli(Path(base) / "missing")
            self.assertEqual(os.readlink(link), str(root / "cli" / "omaq.py"))
        finally:
            os.environ["HOME"] = old
            shutil.rmtree(base, ignore_errors=True)

    def test_entry_point_writes_no_bytecode(self):
        import shutil
        import subprocess
        import tempfile
        base = os.path.realpath(tempfile.mkdtemp(prefix="omaq-cli-pyc-"))
        try:
            shutil.copytree(os.path.join(ROOT, "cli"), os.path.join(base, "cli"),
                            ignore=shutil.ignore_patterns("__pycache__"))
            result = subprocess.run([sys.executable, "-I", os.path.join(base, "cli", "omaq.py"), "--help"],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual((result.returncode, result.stdout.startswith("usage: omaq-connect")), (0, True))
            self.assertEqual([d for d, _, _ in os.walk(base) if os.path.basename(d) == "__pycache__"], [])
        finally:
            shutil.rmtree(base, ignore_errors=True)


class FakeLink:
    def __init__(self):
        self.sent = []

    def send(self, obj):
        self.sent.append(obj)
        return True

    def close(self):
        pass

    def ops(self, name):
        return [o for o in self.sent if o.get("op") == name]


class Sandbox(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.base = os.path.realpath(tempfile.mkdtemp(prefix="omaq-cli-unit-"))
        self.saved = {k: os.environ.get(k) for k in ("HOME", "XDG_RUNTIME_DIR", "OMAQ_HOME", "OMAQ_STATE")}
        os.environ["HOME"] = os.path.join(self.base, "home")
        os.environ["XDG_RUNTIME_DIR"] = os.path.join(self.base, "run")
        os.environ["OMAQ_HOME"] = os.path.join(self.base, "omaq-home")
        os.environ["OMAQ_STATE"] = os.path.join(self.base, "omaq-state")
        os.makedirs(os.environ["XDG_RUNTIME_DIR"], mode=0o700)

    def tearDown(self):
        import shutil
        for key, value in self.saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        shutil.rmtree(self.base, ignore_errors=True)

    def app(self, friends):
        app = cli.App("")
        app.link, app.handshake, app.instance, app.conn = FakeLink(), True, "i" * 32, "online"
        app.apply_friends([dict(f, online=True, status="online") for f in friends])
        return app

    def loaded(self, app, conversation="0", *texts):
        c = app.chat.conv(conversation)
        c.loaded, c.request = True, ""
        for k, text in enumerate(texts):
            c.items.append(cli.Message("m%d" % k, "", "in", text, 1700000000.0))
        return c


A, B, C = "a" * 64, "b" * 64, "c" * 64


class PackageA(Sandbox):
    def test_h01_rebound_number_is_purged_and_pane_follows_the_key(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0", "for alice")
        app.pane.draft, app.pane.cursor = "secret for alice", 16
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"},
                           {"id": "5", "key": A, "name": "alice", "online": True, "status": "online"},
                           {"id": "1", "key": B, "name": "bob", "online": True, "status": "online"}])
        self.assertEqual(app.pane.conv, "5")
        self.assertEqual(app.pane.draft, "secret for alice")
        self.assertNotIn("for alice", [m.text for m in app.chat.conv("0").items])
        app.edit("key", "enter")
        sent = app.link.ops("msg.send")[-1]
        self.assertEqual((sent["conversation"], sent["key"]), ("5", A))

    def test_h01_draft_stays_with_its_conversation(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        app.edit("text", "secret for alice")
        app.edit("key", "tab")
        self.assertEqual((app.pane.conv, app.pane.draft), ("1", ""))
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send"), [])
        app.edit("key", "tab")
        self.assertEqual((app.pane.conv, app.pane.draft), ("0", "secret for alice"))

    def test_h02_ownership_needs_the_launch_nonce(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        os.makedirs(os.environ["OMAQ_STATE"], mode=0o700, exist_ok=True)
        with open(os.path.join(os.environ["OMAQ_STATE"], "omaq.protocol"), "w") as f:
            json.dump({"pid": 1, "instance": app.instance, "nonce": "n" * 32}, f)
        app.presence.mark_started(app.instance, "x" * 32)
        self.assertFalse(app.owned_by_cli())
        app.presence.mark_started(app.instance, "n" * 32)
        self.assertTrue(app.owned_by_cli())

    def test_h02_bar_probe_only_for_the_bar_state(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        calls = []
        original_run, original_access = cli.subprocess.run, cli.os.access
        try:
            cli.subprocess.run = lambda *a, **k: calls.append(a) or type("R", (), {"returncode": 0})()
            cli.os.access = lambda path, mode: True if path == cli.BAR_PROBE else original_access(path, mode)
            self.assertFalse(app.bar_attached())
            self.assertEqual(calls, [])
            app.state = cli.default_state()
            self.assertTrue(app.bar_attached())
            cli.subprocess.run = lambda *a, **k: type("R", (), {"returncode": 1})()
            self.assertFalse(app.bar_attached())
            def slow(*a, **k):
                raise cli.subprocess.TimeoutExpired("probe", 2)
            cli.subprocess.run = slow
            self.assertTrue(app.bar_attached())
        finally:
            cli.subprocess.run, cli.os.access = original_run, original_access

    def test_m01_history_failure_retries_and_reconnect_reloads(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        request = app.chat.conv("0").request
        app.on_event({"event": "history.failed", "conversation": "0", "request": request, "code": "history_failed"})
        self.assertEqual(app.chat.conv("0").request, "")
        app.tick(time.monotonic())
        self.assertEqual(len(app.link.ops("history")), 1)
        app.chat.retry_at["0"] = 0.0
        app.tick(time.monotonic())
        self.assertEqual(len(app.link.ops("history")), 2)
        self.loaded(app, "0", "old")
        app.edit("text", "in flight")
        app.edit("key", "enter")
        app.drop_link()
        self.assertEqual(app.chat.conv("0").items[-1].failed, "delivery_unknown")
        app.link, app.handshake = FakeLink(), True
        app.on_event({"event": "friend.list.begin", "generation": "2"})
        app.on_event({"event": "friend.info", "generation": "2", "id": "0", "key": A, "name": "alice", "online": True})
        app.on_event({"event": "friend.list.end", "generation": "2"})
        self.assertEqual(len(app.link.ops("history")), 1)

    def test_m02_identity_ready_after_handshake_and_reset_on_import(self):
        app = cli.App("")
        app.link = FakeLink()
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": "j" * 32, "protocol": 16, "online": True})
        self.assertEqual(app.link.ops("identity.ready"), [{"op": "identity.ready", "id": "j" * 32}])
        app.friends = [{"id": "0", "key": A, "name": "alice", "online": True, "status": "online"}]
        app.on_event({"event": "identity", "op": "import"})
        self.assertEqual((app.friends, app.handshake), ([], False))
        self.assertEqual(len(app.link.ops("status")), 2)

    def test_m04_private_directories(self):
        target = os.path.join(self.base, "fresh", "state", "omaq")
        old = os.umask(0o022)
        try:
            self.assertEqual(cli.ensure_private_dir(target), "")
        finally:
            os.umask(old)
        self.assertEqual(cli.stat.S_IMODE(os.stat(target).st_mode), 0o700)
        loose = os.path.join(self.base, "loose")
        os.mkdir(loose, 0o755)
        os.chmod(loose, 0o755)
        self.assertIn("chmod 700", cli.ensure_private_dir(loose))
        os.environ.pop("XDG_RUNTIME_DIR")
        state = os.path.join(self.base, "new-state")
        presence = cli.Presence(state)
        self.assertEqual(cli.stat.S_IMODE(os.stat(state).st_mode), 0o700)
        presence.release()

    def test_m05_header_only_pane_is_not_read(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0", "unseen")
        app.chat.unread["0"] = 1
        app.size = (24, 6)
        app.render()
        app.mark_read(time.monotonic())
        self.assertEqual(app.link.ops("conversation.read"), [])
        app.size = (60, 12)
        app.render()
        app.mark_read(time.monotonic())
        self.assertEqual(len(app.link.ops("conversation.read")), 1)

    def test_m06_narrow_pane_keeps_the_body(self):
        app = self.app([{"id": "0", "key": A, "name": "alexandra-the-great"}])
        c = self.loaded(app, "0")
        c.items.append(cli.Message("m", "", "in", "the important body", time.time() - 5 * 86400))
        rows = [re.sub(r"\x1b\[[0-9;]*m", "", app.palette.row(segs, 24)) for segs in app.message_lines("0", 24)]
        self.assertTrue(any("important" in r for r in rows), rows)
        self.assertTrue(all(cli.text_width(r) <= 24 for r in rows))

    def test_m06_tab_strip_keeps_the_focused_tab(self):
        friends = [{"id": str(k), "key": chr(97 + k) * 64, "name": "friend-number-%d" % k} for k in range(6)]
        app = self.app(friends)
        for k in range(1, 6):
            app.open_pane(str(k))
        header = re.sub(r"\x1b\[[0-9;]*m", "", app.header(app.pane, 40, True, tabs=app.layout.panes()))
        self.assertIn("[friend-number-5]", header)

    def test_m10_terminal_is_restored_when_cleanup_is_interrupted(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        restored = []
        def boom():
            raise SystemExit(0)
        app.exit_helper = boom
        original = cli.termios.tcsetattr
        cli.termios.tcsetattr = lambda *a: restored.append(a)
        out = __import__("io").StringIO()
        try:
            app.cleanup(0, [], out)
        finally:
            cli.termios.tcsetattr = original
        self.assertEqual(len(restored), 1)
        self.assertIsNone(app.presence.lock)
        self.assertIn("\x1b[?1049l", out.getvalue())
        self.assertIn("\x1b[?2026l", out.getvalue())

    def test_l02_named_only_theme_gets_the_exact_roles(self):
        path = os.path.join(os.environ["HOME"], ".local/state/omarchy/current/theme")
        os.makedirs(path)
        with open(os.path.join(path, "colors.toml"), "w") as f:
            f.write('background = "#101010"\nforeground = "#f0f0f0"\nred = "#ff0000"\ngreen = "#00ff00"\n'
                    'yellow = "#ffff00"\naccent = "#8080ff"\n')
        roles = cli.Palette().roles
        self.assertEqual(roles["unread"], "38;2;255;0;0")
        self.assertEqual(roles["name"], "38;2;255;255;0")
        self.assertEqual(roles["accent"], "38;2;128;128;255")

    def theme(self, **colors):
        path = os.path.join(os.environ["HOME"], ".local/state/omarchy/current/theme")
        os.makedirs(path, exist_ok=True)
        with open(os.path.join(path, "colors.toml"), "w") as f:
            f.write("".join('%s = "%s"\n' % item for item in colors.items()))

    def test_theme_colors_are_raised_to_wcag_contrast(self):
        self.assertFalse(cli.Palette().loaded)
        rgb = lambda code: tuple(int(x) for x in code.split(";")[2:5])
        light = dict(background="#ffffff", color0="#e6e9ef", foreground="#4c4f69", red="#d20f39", green="#40a02b",
                     yellow="#df8e1d", accent="#1e66f5")
        self.theme(**light)
        p = cli.Palette()
        self.assertTrue(p.loaded)
        for role, code in p.roles.items():
            if role not in ("rule", "qr"):
                self.assertGreaterEqual(cli.contrast(rgb(code), (255, 255, 255)), 4.5, role)
        name = rgb(p.roles["name"])
        self.assertTrue(all(c <= o for c, o in zip(name, (0xDF, 0x8E, 0x1D))), name)
        dark = dict(background="#0c0b0c", foreground="#cfd3cd", red="#c38b7b", green="#87a9b0", yellow="#6b5e73",
                    accent="#b59790")
        self.theme(**dark)
        p = cli.Palette()
        name = rgb(p.roles["name"])
        self.assertGreaterEqual(cli.contrast(name, (0x0C, 0x0B, 0x0C)), 4.5)
        self.assertTrue(all(c >= o for c, o in zip(name, (0x6B, 0x5E, 0x73))), name)
        self.assertEqual(p.roles["rule"], "38;2;%d;%d;%d" % cli.mix((0x0C, 0x0B, 0x0C), (0xCF, 0xD3, 0xCD), 0.22))
        self.assertEqual(p.roles["text"], "38;2;207;211;205")

    def test_bad_theme_files_never_crash_and_a_failed_reload_is_retried(self):
        for value in ("-10000", "1_2345", "12345g", "+12345"):
            with self.assertRaises(ValueError):
                cli.Palette.rgb(value)
        base = dict(background="#101010", foreground="#f0f0f0", red="#ff5555", green="#9ece6a", yellow="#e0af68", cyan="#40c0c0")
        self.theme(accent="#-10000", **base)
        self.assertEqual(cli.Palette().roles["accent"], "34")
        path = os.path.join(os.environ["HOME"], ".local/state/omarchy/current/theme/colors.toml")
        with open(path, "wb") as f:
            f.write(b'accent = "#7aa2f7"\nname = "caf\xe9"\n')
        self.assertEqual(cli.Palette().roles["accent"], "34")
        self.theme(accent="#7aa2f7", **base)
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        good = app.palette
        now = time.monotonic()
        self.theme(accent="#ff0000", **base)
        os.chmod(path, 0)
        try:
            if os.geteuid() != 0:
                for k in range(cli.THEME_RETRIES - 1):
                    app.tick(now + 2 + 2 * k)
                    self.assertIs(app.palette, good)
                app.tick(now + 20)
                self.assertEqual(app.palette.roles["accent"], "34")
        finally:
            os.chmod(path, 0o644)
        app.tick(now + 40)
        self.assertEqual(app.palette.roles["accent"], "38;2;255;0;0")
        os.remove(path)
        for k in range(cli.THEME_RETRIES):
            app.tick(now + 60 + 2 * k)
        self.assertEqual(app.palette.roles["accent"], "34")

    def test_a_row_stops_at_the_first_segment_it_cuts(self):
        app = self.app([{"id": "0", "key": A, "name": "abcdefghijklmnopqrstu\u754cZ"}])
        header = re.sub(r"\x1b\[[0-9;]*m", "", app.header(app.pane, 24, True))
        self.assertEqual(header, "❯ abcdefghijklmnopqrstu ")
        self.assertEqual(re.sub(r"\x1b\[[0-9;]*m", "", app.palette.row([("ab\u754c", "text", ""), ("Z", "text", "")], 3)), "ab ")

    def test_presence_dot_and_name_color_in_the_chat_header(self):
        self.theme(accent="#7aa2f7", background="#1a1b26", foreground="#c0caf5", red="#f7768e", green="#9ece6a",
                   yellow="#e0af68", cyan="#7dcfff")
        friends = [{"id": "0", "key": A, "name": "alexandra"}, {"id": "1", "key": B, "name": "bob"}]
        app = self.app(friends)
        roles = app.palette.roles
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text)
        painted = lambda text, role, extra="": "\x1b[0;%s%sm%s\x1b[0m" % (roles[role], ";" + extra if extra else "", text)
        header = app.header(app.pane, 60, True)
        self.assertEqual(plain(header).rstrip(), "❯ alexandra ●  online")
        for piece in (painted("alexandra", "text", "1"), painted(" ●", "online"), painted("  online", "status")):
            self.assertIn(piece, header)
        app.apply_friends([dict(friends[0], online=True, status="afk"), dict(friends[1], online=True, status="online")])
        self.assertIn(painted(" ●", "away"), app.header(app.pane, 60, True))
        app.apply_friends([dict(friends[0], online=False, status="offline"), dict(friends[1], online=True, status="online")])
        header = app.header(app.pane, 60, True)
        self.assertEqual(plain(header).rstrip(), "❯ alexandra ●  offline")
        for piece in (painted("alexandra", "name", "1"), painted(" ●", "offline"), painted("  offline", "offline")):
            self.assertIn(piece, header)
        app.conn = "reconnecting"
        self.assertEqual(plain(app.header(app.pane, 60, True)).rstrip(), "❯ alexandra  reconnecting…")
        app.conn = "online"
        app.open_pane("1")
        strip = app.header(app.pane, 40, True, tabs=app.layout.panes())
        self.assertEqual(plain(strip).split()[:4], ["alexandra", "●", "[bob]", "●"])
        self.assertIn(painted("bob", "text", "1"), strip)
        self.assertIn(painted("alexandra", "name"), strip)

    def test_command_hint_and_colon_suggestions(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text).strip()
        app.tip = 0
        self.assertEqual(plain(app.command_hint(120)), ":help   :quit   \u00b7   ctrl+k opens the emoji picker")
        app.edit("text", ":")
        self.assertEqual(plain(app.command_hint(300)).split(), [":help", ":connect", ":split", ":invite", ":redeem",
                                                                ":requests", ":accept", ":decline", ":nickname",
                                                                ":sendfile", ":acceptfile", ":declinefile", ":cancelfile",
                                                                ":reply", ":clear", ":close", ":quit"])
        self.assertIn("1;7m:help", app.command_hint(80))
        app.edit("text", "c")
        self.assertEqual(plain(app.command_hint(80)).split(), [":connect", ":cancelfile", ":clear", ":close"])
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":connect")
        app.edit("text", " ")
        self.assertEqual(plain(app.command_hint(80)).split(), ["alice", "bob"])
        app.edit("key", "tab")
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":connect bob")
        self.assertIn("1;7mbob", app.command_hint(80))
        app.edit("key", "esc")
        app.edit("text", ":")
        app.edit("key", "tab")
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":connect")
        app.edit("key", "backtab")
        self.assertEqual(app.pane.draft, ":help")
        app.edit("key", "esc")
        app.edit("text", ":xyz")
        self.assertEqual(plain(app.command_hint(80)), "no matching command")
        app.edit("key", "esc")
        app.edit("text", ":he")
        app.edit("key", "enter")
        self.assertEqual(app.help.index, 0)
        app.help = None
        app.edit("text", ":qa")
        app.edit("key", "enter")
        self.assertTrue(app.running)
        self.assertIn("Unknown command :qa", app.status)
        app.open_pane("1")
        app.edit("text", ":close")
        app.edit("key", "enter")
        self.assertEqual((len(app.layout.panes()), app.running), (1, True))
        app.edit("text", ":close")
        app.edit("key", "enter")
        self.assertEqual((len(app.layout.panes()), app.running), (1, True))
        app.open_pane("1")
        app.edit("text", ":quit")
        app.edit("key", "enter")
        self.assertFalse(app.running)

    def test_day_dividers_appear_once_per_day(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        c = self.loaded(app)
        base = time.time()
        for k, ts in enumerate((base - 2 * 86400, base - 2 * 86400 + 60, base)):
            c.items.append(cli.Message("d%d" % k, "", "in", "msg%d" % k, ts))
        rows = [re.sub(r"\x1b\[[0-9;]*m", "", app.palette.row(segs, 60)).rstrip() for segs in app.message_lines("0", 60)]
        dividers = [r for r in rows if r.startswith(" \u2500\u2500")]
        self.assertEqual(len(dividers), 2, rows)
        self.assertTrue(dividers[1].endswith("today \u2500\u2500"))
        self.assertTrue(all(re.match(r" \d\d:\d\d  ", r) for r in rows if "msg" in r), rows)

    def test_up_and_down_recall_this_chat_only(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0")
        for text in ("first", "second"):
            app.edit("text", text)
            app.edit("key", "enter")
        app.edit("text", "draft")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "second")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "first")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "first")
        app.edit("key", "down")
        app.edit("key", "down")
        self.assertEqual(app.pane.draft, "draft")
        app.edit("key", "esc")
        app.edit("key", "tab")
        self.loaded(app, "1")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "")
        app.edit("text", ":he")
        app.edit("key", "enter")
        app.edit("key", "esc")
        app.edit("text", ":")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, ":he")

    def test_clear_asks_first_and_follows_the_helper(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        c = self.loaded(app, "0", "old one")
        app.history_clear = 2
        app.edit("text", ":clear")
        app.edit("key", "enter")
        self.assertEqual(app.confirm.prompt, "Clear this chat? y/n")
        app.edit("text", "n")
        self.assertEqual(app.link.ops("history.clear"), [])
        self.assertEqual(app.status, "Chat kept")
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.edit("text", "y")
        op = app.link.ops("history.clear")[-1]
        self.assertEqual(sorted(op), ["conversation", "id", "key", "op"])
        self.assertEqual((op["conversation"], op["key"]), ("0", A))
        app.on_event({"event": "history.clear.succeeded", "conversation": "0", "key": A, "request": "other",
                      "instance": app.instance})
        app.on_event({"event": "history.clear.succeeded", "conversation": "0", "key": A, "request": op["id"],
                      "instance": "j" * 32})
        self.assertEqual([m.text for m in c.items], ["old one"])
        app.on_event({"event": "history.clear.succeeded", "conversation": "0", "key": A, "request": op["id"],
                      "instance": app.instance})
        self.assertEqual(app.chat.conv("0").items, [])
        self.assertEqual(app.link.ops("history")[-1]["conversation"], "0")
        app.history_clear = 0
        app.edit("text", ":clear")
        app.edit("key", "enter")
        self.assertEqual(app.status, "Helper update required")

    def test_invite_create_show_copy_and_revoke(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.out = __import__("io").StringIO()
        app.edit("text", ":invite")
        app.edit("key", "enter")
        op = app.link.ops("invite.create")[-1]
        self.assertEqual(sorted(op), ["kind", "op", "request", "ttlSec"])
        self.assertEqual((op["kind"], op["ttlSec"]), ("direct", 86400))
        link = "omaq://invite-" + "a" * 40
        app.on_event({"event": "invite", "url": link, "expires": int(time.time()) + 86400, "op": "create",
                      "request": op["request"]})
        self.assertEqual(app.overlay.kind, "invite")
        import base64
        self.assertIn("\x1b]52;c;%s\x07" % base64.b64encode(link.encode()).decode(), app.out.getvalue())
        app.overlay.qr = ["\u2588\u2580\u2584 "]
        app.size = (100, 40)
        frame = app.render()
        self.assertIn(link, frame)
        self.assertIn("\x1b[0;97;40m\u2588\u2580\u2584 ", frame)
        app.edit("key", "esc")
        self.assertIsNone(app.overlay)
        app.edit("text", ":invite revoke")
        app.edit("key", "enter")
        self.assertEqual(app.confirm.prompt, "Revoke this invite? It cannot be used again. y/n")
        app.edit("text", "n")
        self.assertEqual(app.link.ops("invite.revoke"), [])
        app.edit("text", ":invite revoke")
        app.edit("key", "enter")
        app.edit("text", "y")
        self.assertEqual(sorted(app.link.ops("invite.revoke")[-1]), ["op", "request"])

    def test_redeem_and_its_errors(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.edit("text", ":redeem nonsense")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("invite.redeem"), [])
        app.edit("text", ":redeem  omaq://abc ")
        app.edit("key", "enter")
        op = app.link.ops("invite.redeem")[-1]
        self.assertEqual((op["payload"], sorted(op)), ("omaq://abc", ["id", "op", "payload"]))
        app.on_event({"event": "error", "code": "invite_expired", "request": op["id"]})
        self.assertEqual(app.status, "Invite expired. Create a new link.")
        app.edit("text", ":redeem omaq://def")
        app.edit("key", "enter")
        op = app.link.ops("invite.redeem")[-1]
        safety = " / ".join(" ".join("ab%02d" % k for k in range(16)) for _ in range(2))
        app.on_event({"event": "invite.redeemed", "kind": "direct", "safety": safety, "request": op["id"]})
        self.assertIsNone(app.overlay)
        app.edit("text", ":redeem omaq://def")
        app.edit("key", "enter")
        op = app.link.ops("invite.redeem")[-1]
        app.on_event({"event": "invite.redeemed", "kind": "direct", "key": B, "safety": safety, "request": op["id"]})
        self.assertEqual((app.overlay.kind, app.overlay.safety), ("redeemed", safety))

    def test_requests_accept_and_decline(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        safety = " / ".join(" ".join("cd%02d" % k for k in range(16)) for _ in range(2))
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": "broken"})
        self.assertIsNone(app.invites.request)
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": safety})
        self.assertIn("1 request", re.sub(r"\x1b\[[0-9;]*m", "", app.friends_bar(120)))
        app.on_event({"event": "request.conflict", "kind": "direct", "key": B})
        app.edit("text", ":requests")
        app.edit("key", "enter")
        app.size = (100, 30)
        frame = re.sub(r"\x1b\[[0-9;]*m", "", app.render())
        self.assertIn(safety.split(" / ")[0], frame)
        self.assertIn("Another device used this invite link", frame)
        self.assertIn("Compare this code with your friend before accepting", frame)
        app.edit("text", ":accept")
        app.edit("key", "enter")
        op = app.link.ops("contact.decide")[-1]
        self.assertEqual(op, {"op": "contact.decide", "id": app.invites.decide_request, "accept": True, "key": C})
        self.assertTrue(op["id"].startswith("cli-decide-"))
        app.on_event({"event": "invite", "url": "", "expires": 0})
        self.assertIsNone(app.invites.request)
        app.on_event({"event": "request", "kind": "group"})
        app.edit("text", ":decline")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("contact.decide")[-1], {"op": "contact.decide", "id": app.invites.decide_request,
                                                              "accept": False})
        self.assertIsNone(app.invites.request)
        app.edit("text", ":accept")
        app.edit("key", "enter")
        self.assertEqual(app.status, "No pending request")

    def test_request_state_follows_the_helper(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        safety = " / ".join(" ".join("ef%02d" % k for k in range(16)) for _ in range(2))
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": safety})
        self.assertEqual(app.status, "Friend request; :requests shows it")
        app.say("")
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": safety})
        self.assertEqual(app.status, "")
        self.assertTrue(re.sub(r"\x1b\[[0-9;]*m", "", app.friends_bar(40)).startswith(" omaq "))
        self.assertIn("1 request", re.sub(r"\x1b\[[0-9;]*m", "", app.friends_bar(40)))
        for typed in (":a", ":acc", ":d"):
            app.edit("text", typed)
            app.edit("key", "enter")
        self.assertEqual(app.link.ops("contact.decide"), [])
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": "bad"})
        self.assertIsNone(app.invites.request)
        app.on_event({"event": "request", "kind": "direct", "key": C, "safety": safety})
        app.edit("text", ":accept")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "forbidden"})
        self.assertEqual(app.status, "The request could not be accepted.")
        app.on_event({"event": "error", "code": "contact_limit"})
        self.assertEqual(app.status, "The request could not be accepted.")
        app.edit("text", ":accept")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "contact_limit"})
        self.assertEqual(app.status, "The direct-contact limit has been reached.")
        app.invites.redeem_request = "cli-redeem-1"
        app.overlay = cli.RedeemedOverlay(False, "")
        app.drop_link()
        self.assertEqual((app.invites.request, app.overlay, app.invites.redeem_request, app.invites.invite.url), (None, None, "", ""))
        self.assertEqual(app.status, "Invite result unknown; try again")

    def test_focus_marker_and_name_pill(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}, {"id": "1", "key": B, "name": "carol"}])
        self.loaded(app, "0")
        app.size = (80, 12)
        frame = app.render()
        text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", frame)
        self.assertIn("❯ bob ●  online", text)
        self.assertNotIn("chat ›", text)
        self.assertIn("\x1b[0;%s;1;7m bob \x1b[0m" % app.palette.roles["accent"], frame)
        self.assertIn("\x1b[0;%s;1m ❯ \x1b[0m" % app.palette.roles["accent"], frame)
        app.size = (12, 12)
        composer = [r for r in re.findall(r"\x1b\[\d+;1H(.*?)\x1b\[0m\x1b\[K", app.render()) if "❯ " in r][-1]
        self.assertLessEqual(cli.text_width(re.sub(r"\x1b\[[0-9;]*m", "", composer)), 12)

    def test_unread_mark_and_preview_only_off_screen(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}, {"id": "1", "key": B, "name": "dave"}])
        self.loaded(app, "0")
        app.size = (80, 12)
        app.render()
        app.on_event({"event": "message", "conversation": "1", "key": B, "dir": "in", "id": "d1",
                      "text": "bist du da?\nzweite Zeile", "ts": 1700000000})
        self.assertEqual(app.status, "dave: bist du da?")
        app.on_event({"event": "unread", "conversation": "1", "key": B, "count": 1})
        pill = "\x1b[0;%s;1;7m dave \x1b[0m" % app.palette.roles["unread"]
        self.assertIn(pill, app.render())
        app.say("")
        app.on_event({"event": "message", "conversation": "0", "key": A, "dir": "in", "id": "b1", "text": "hallo",
                      "ts": 1700000001})
        app.on_event({"event": "unread", "conversation": "0", "key": A, "count": 1})
        frame = app.render()
        self.assertEqual(app.status, "")
        self.assertNotIn("%s;1;7m bob " % app.palette.roles["unread"], frame)
        self.assertIn("%s;1;7m bob " % app.palette.roles["accent"], frame)
        app.on_event({"event": "message", "conversation": "1", "key": B, "dir": "in", "id": "d2",
                      "text": "\x1b]52;c;eA==\x07" + "x" * 80, "ts": 1700000002})
        self.assertNotIn("\x1b", app.status)
        self.assertTrue(app.status.endswith("…"))
        app.on_event({"event": "unread", "conversation": "1", "key": B, "count": 0})
        self.assertNotIn(pill, app.render())

    def test_emoji_presentation_is_two_cells_and_one_cursor_step(self):
        heart = "\u2764\ufe0f"
        self.assertEqual(cli.text_width(heart), 2)
        self.assertEqual(cli.text_width("\U0001F600\ufe0f"), 2)
        self.assertEqual(cli.clip("a" + heart + "b", 2), ("a", 1))
        self.assertEqual(cli.clip("a" + heart + "b", 3), ("a" + heart, 3))
        self.assertEqual((cli.prev_stop("a" + heart, 3), cli.next_stop("a" + heart, 1)), (1, 3))

    def test_emoticons_become_emoji_but_commands_and_links_stay(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.edit("text", ":) ")
        self.assertEqual(app.pane.draft, "\U0001F642 ")
        app.edit("key", "esc")
        app.edit("text", ":p")
        self.assertNotIn("no matching command", re.sub(r"\x1b\[[0-9;]*m", "", app.command_hint(80)))
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send")[-1]["text"], "\U0001F61B")
        app.edit("text", "siehe https://x.y/:/ und <3")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send")[-1]["text"], "siehe https://x.y/:/ und \u2764\ufe0f")
        app.edit("paste", "ok :) bye")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send")[-1]["text"], "ok \U0001F642 bye")
        app.edit("text", ":connect XD ")
        self.assertEqual(app.pane.draft, ":connect XD ")
        app.edit("key", "esc")
        app.edit("text", "a<3 x <3 ")
        self.assertEqual(app.pane.draft, "a<3 x \u2764\ufe0f ")
        app.edit("key", "backspace")
        app.edit("key", "backspace")
        self.assertEqual(app.pane.draft, "a<3 x ")
        app.edit("key", "esc")
        app.edit("text", ":quit")
        app.edit("key", "enter")
        self.assertFalse(app.running)

    def test_ctrl_k_picker_inserts_at_the_cursor(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (80, 14)
        app.edit("text", "hi ")
        app.edit("key", "left")
        app.edit("ctrl", "k")
        frame = app.render()
        rows = re.findall(r"\x1b\[(\d+);1H(.*?)\x1b\[K", frame)
        head = [int(n) for n, row in rows if "emoji › " in re.sub(r"\x1b\[[0-9;]*m", "", row)]
        self.assertEqual(len(head), 1)
        self.assertEqual(int(re.findall(r"\x1b\[(\d+);\d+H\x1b\[\?25h", frame)[-1]), head[0])
        text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", frame)
        self.assertIn("\U0001F600", text)
        self.assertIn("grinning face", text)
        self.assertIn("❯ bob ●  online", text)
        app.edit("key", "right")
        app.edit("key", "enter")
        self.assertEqual((app.pane.draft, app.picker), ("hi\U0001F642 ", None))
        app.edit("ctrl", "k")
        app.edit("text", "fire")
        self.assertEqual(app.picker_items()[0][0], "\U0001F525")
        app.edit("key", "enter")
        self.assertEqual(app.pane.draft, "hi\U0001F642\U0001F525 ")
        app.edit("ctrl", "k")
        app.edit("text", "cat face")
        self.assertTrue(app.picker_items())
        self.assertTrue(all("cat face" in name for _, name in app.picker_items()))
        self.assertTrue(all(cli.text_width(emoji) == 2 for emoji, _ in cli.emoji_catalog()))
        app.edit("text", "zzzz")
        app.edit("key", "enter")
        self.assertEqual(app.pane.draft, "hi\U0001F642\U0001F525 ")
        app.edit("ctrl", "k")
        app.edit("key", "esc")
        self.assertIsNone(app.picker)
        self.assertEqual(cli.Keys().feed(b"\x0b"), [("ctrl", "k")])

    def test_picker_uses_the_bar_emoji_set(self):
        with open(os.path.join(ROOT, "pages", "ChatPage.qml"), encoding="utf-8") as f:
            qml = f.read()
        bar = re.findall(r'"([^"]+)"', re.search(r"readonly property var emojiSet: \[(.*?)\]", qml, re.S).group(1))
        extra = re.findall(r'"([^"]+)"', re.search(
            r"readonly property var composerEmojiSet: root\.emojiSet\.concat\(\[(.*?)\]\)", qml, re.S).group(1))
        self.assertEqual((cli.EMOJI, extra), (bar + extra, ["\U0001F91F"]))

    def test_confirmation_expires_with_its_prompt(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.history_clear = 2
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.tick(app.confirm.until + 1)
        self.assertEqual((app.confirm, app.status), (None, "Chat kept"))
        app.edit("text", "y")
        self.assertEqual((app.link.ops("history.clear"), app.pane.draft), ([], "y"))
        app.edit("key", "esc")
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.confirm.until = time.monotonic() - 1
        app.edit("text", "y")
        self.assertEqual((app.link.ops("history.clear"), app.pane.draft), ([], "y"))
        app.edit("key", "esc")
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.drop_link()
        self.assertEqual((app.confirm, app.status), (None, "Chat kept"))

    def test_identity_changed_error_keeps_panes_and_drafts(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0")
        app.edit("text", ":connect bob")
        app.edit("key", "enter")
        app.pane.draft, app.pane.cursor = "an bob", 6
        before = [(pane.conv, pane.draft) for pane in app.layout.panes()]
        asked = len(app.link.ops("status"))
        app.on_event({"event": "error", "code": "identity_changed", "conversation": "3"})
        self.assertEqual([(pane.conv, pane.draft) for pane in app.layout.panes()], before)
        self.assertEqual((len(app.link.ops("status")), app.chat.conv("0").loaded), (asked + 1, False))
        app.addr = "a" * 76
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": app.instance, "protocol": 16,
                      "addr": "b" * 76, "online": True})
        self.assertEqual([pane.draft for pane in app.layout.panes()], ["", ""])
        self.assertEqual(app.friends, [])

    def test_long_draft_scrolls_in_linear_time(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.size = (80, 24)
        app.edit("paste", "x" * 50000 + "END")
        start = time.perf_counter()
        frame = app.render()
        self.assertLess(time.perf_counter() - start, 0.5)
        rows = re.findall(r"\x1b\[(\d+);1H(.*?)\x1b\[K", frame)
        composer = re.sub(r"\x1b\[[0-9;]*m", "", rows[-2][1]).rstrip()
        self.assertTrue(composer.endswith("END"))
        column = int(re.findall(r"\x1b\[\d+;(\d+)H\x1b\[\?25h", frame)[-1])
        self.assertEqual(column, cli.text_width(composer) + 1)

    def test_errors_follow_the_clients_own_actions(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.edit("text", ":redeem omaq://broken")
        app.edit("key", "enter")
        rid = app.link.ops("invite.redeem")[-1]["id"]
        app.on_event({"event": "error", "code": "unsupported", "request": rid})
        self.assertEqual(app.status, "That is not an OmaQ invite.")
        app.say("")
        app.invites.action_until = 0.0
        app.on_event({"event": "error", "code": "offline"})
        self.assertEqual(app.status, "")
        app.edit("text", ":redeem omaq://x")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "unsupported"})
        self.assertEqual((app.status, app.invites.redeem_request), ("That is not an OmaQ invite.", ""))

    def test_qr_is_white_on_black_and_checked(self):
        theme = os.path.join(os.environ["HOME"], ".local", "state", "omarchy", "current", "theme")
        os.makedirs(theme)
        with open(os.path.join(theme, "colors.toml"), "w") as f:
            f.write("".join('color%d = "#%02x%02x%02x"\n' % (k, 200 - k * 20, 190 - k * 20, 180 - k * 20)
                            for k in range(8)) + 'accent = "#e33d84"\n')
        self.assertEqual(cli.Palette().roles["qr"], "38;2;255;255;255;48;2;0;0;0")
        calls = []
        real = cli.subprocess.run

        def fake(argv, **kw):
            calls.append(argv)
            return cli.subprocess.CompletedProcess(argv, 0, fake.output, b"")
        fake.output = "\u2588\u2580 \n".encode()
        cli.subprocess.run = fake
        try:
            ok = cli.qr_lines("omaq://x")
            fake.output = "\u2588\x1b[31m\u2580\n".encode()
            bad = cli.qr_lines("omaq://x")
        finally:
            cli.subprocess.run = real
        if calls:
            self.assertEqual((ok, bad), (["\u2588\u2580 "], []))
            self.assertEqual(calls[0][-2:], ["--", "omaq://x"])

    def test_picker_finds_symbols_outside_the_old_ranges(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        for query, emoji in (("star", "\u2b50"), ("watch", "\u231a"), ("vs", "\U0001F19A")):
            app.picker = cli.Picker(query)
            self.assertIn(emoji, [item[0] for item in app.picker_items()])

    def sendfile_home(self):
        home = os.environ["HOME"]
        for folder in ("Pictures/Screenshots", "Documents", ".config", "locked", "my folder"):
            os.makedirs(os.path.join(home, folder))
        for name, size in (("Pictures/night.jpg", 3148331), ("Pictures/bad\x1bname.jpg", 1), ("notes.txt", 120),
                           (".bashrc", 1), ("my folder/file x.txt", 1), ("trail ", 1)):
            with open(os.path.join(home, name), "wb") as f:
                f.write(b"x" * size)
        with open(os.path.join(os.fsencode(home), b"Pictures/caf\xe9.jpg"), "wb") as f:
            f.write(b"x")
        return home

    def typed(self, app, text):
        app.file_picker, app.pane.draft, app.pane.cursor = None, "", 0
        app.edit("text", text)

    def test_sendfile_tab_lists_folders_and_files_and_sends_the_pick(self):
        home = self.sendfile_home()
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (80, 20)
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        self.typed(app, ":sendfile ~/")
        self.assertEqual(plain(app.command_hint(80)).strip(), "tab lists folders and files")
        app.edit("key", "tab")
        self.assertEqual(app.file_picker.items("~/"), [("Documents", True), ("locked", True), ("my folder", True),
                                                       ("Pictures", True), ("notes.txt", False), ("trail ", False)])
        frame = app.render()
        rows = [plain(row) for _, row in re.findall(r"\x1b\[(\d+);1H(.*?)\x1b\[K", frame)]
        head = next(k for k, row in enumerate(rows) if row.startswith(" files › ~/ "))
        self.assertEqual(rows[head + 1].rstrip(), " ▸ Documents/")
        self.assertTrue(rows[head + 5].startswith("   notes.txt ") and rows[head + 5].rstrip().endswith("120 B"))
        self.assertEqual(rows[head + 7].strip(), "1 of 6")
        self.assertTrue(all(cli.text_width(row) <= 80 for row in rows))
        # the cursor stays in the input line, where the path is typed
        line = next(k for k, row in enumerate(rows) if "❯ :sendfile ~/" in row)
        self.assertEqual(int(re.findall(r"\x1b\[(\d+);\d+H\x1b\[\?25h", frame)[-1]), line + 1)
        app.edit("text", "pic")
        self.assertEqual(app.file_picker.items("~/pic"), [("Pictures", True)])
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":sendfile ~/Pictures/")
        self.assertEqual(app.file_picker.items("~/Pictures/"), [("Screenshots", True), ("night.jpg", False)])
        self.assertIn("3.1 MB", plain(app.render()))
        app.render().encode("utf-8")
        app.edit("key", "left")
        self.assertEqual(app.pane.draft, ":sendfile ~/")
        self.assertEqual(app.file_picker.items("~/")[app.file_picker.index], ("Pictures", True))
        app.edit("key", "right")
        app.edit("key", "down")
        app.edit("key", "enter")
        self.assertEqual((app.pane.draft, app.file_picker), (":sendfile ~/Pictures/night.jpg", None))
        self.assertEqual(app.link.ops("file.send"), [])
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.send")[-1]["path"], os.path.join(home, "Pictures/night.jpg"))

    def test_transfer_lines_wrap_instead_of_cutting_the_command(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text).rstrip()
        kinds = {
            "send": (lambda: app.files.sends.__setitem__("0", cli.OutgoingSend("r", "dotonbori-night.jpg", A, "f1", False,
                                                                                    1500000, 3148331)),
                     [":cancelfile", "47%"]),
            "send-start": (lambda: app.files.sends.__setitem__("0", cli.OutgoingSend("r", "dotonbori-night.jpg", A)), [":cancelfile"]),
            "offer": (lambda: app.files.offers.__setitem__("0", cli.FileOffer("f2", "holiday-photos.zip", 52000000, A)),
                      [":acceptfile", ":declinefile", "(52.0 MB)"]),
            "receive": (lambda: app.files.offers.__setitem__("0", cli.FileOffer("f2", "holiday-photos.zip", 52000000, A, True,
                                                                                 False, 26000000)), ["50%"]),
            "canceled": (lambda: app.files.notices.__setitem__("0", (A, "report.pdf")), ["report.pdf", "esc hides"]),
        }
        for kind, (setup, words) in kinds.items():
            app.files.sends.clear()
            app.files.offers.clear()
            app.files.notices.clear()
            setup()
            for cols in (24, 30, 40, 64, 100):
                lines = app.screen.transfer_lines("0", cols)
                self.assertTrue(all(sum(cli.text_width(text) for text, _, _ in segs) <= cols for segs in lines), (kind, cols, lines))
                rows = [plain(app.palette.row(segs, cols)) for segs in lines]
                text = " ".join(" ".join(row.split()) for row in rows)
                for word in words:
                    self.assertIn(word, text, (kind, cols, rows))
        app.files.sends.clear()
        app.files.offers.clear()
        app.files.notices.clear()
        app.files.sends["0"] = cli.OutgoingSend("r", "notes.pdf", A, "f1", False, 700, 1000)
        self.assertEqual([plain(app.palette.row(segs, 80)) for segs in app.screen.transfer_lines("0", 80)],
                         [" \u21e1 Sending notes.pdf  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2591\u2591\u2591 70%  :cancelfile"])

    def test_recall_never_offers_file_or_image_items(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        c = self.loaded(app, "0")
        c.items.append(cli.message_from({"id": "t1", "ts": 1700000000, "from": "me", "text": "hello", "dir": "out"}))
        c.items.append(cli.message_from({"id": "f1", "ts": 1700000010, "from": "me", "text": "/home/me/secret.pdf",
                                         "dir": "out", "kind": "file"}))
        app.on_event({"event": "message", "conversation": "0", "key": A, "id": "i1", "text": "/home/me/photo.png", "dir": "out",
                      "kind": "image", "ts": 1700000020})
        self.assertEqual([m.kind for m in c.items], ["", "file", "image"])
        self.assertEqual(c.items[1].text, "[file] /home/me/secret.pdf")
        self.assertEqual(app.chat.sent_texts("0"), ["hello"])
        app.edit("key", "up")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "hello")
        app.edit("key", "enter")
        self.assertEqual([o["text"] for o in app.link.ops("msg.send")], ["hello"])

    def test_cancelfile_also_cancels_an_incoming_transfer(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (80, 16)
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        offer = lambda fid: app.on_event({"event": "file.offer", "conversation": "0", "key": A, "id": fid, "name": "big.zip",
                                          "size": 4000})
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        self.assertEqual(app.status, "No file transfer in this chat")
        offer("f1")
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        self.assertEqual((app.link.ops("file.cancel")[-1]["id"], app.status), ("f1", "Declined file: big.zip"))
        self.assertNotIn("0", app.files.offers)
        offer("f2")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        self.assertIn("receiving\u2026  :cancelfile", plain(app.render()))
        app.on_event({"event": "file.progress", "conversation": "0", "key": A, "id": "f2", "dir": "in", "done": 1000, "size": 4000})
        frame = plain(app.render())
        self.assertIn("Receiving big.zip", frame)
        self.assertIn("25%  :cancelfile", frame)
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        op = app.link.ops("file.cancel")[-1]
        self.assertEqual((op["id"], op["key"], app.status), ("f2", A, "Canceling\u2026"))
        self.assertIn("0", app.files.offers)
        app.on_event({"event": "file.canceled", "conversation": "0", "key": A, "id": "f2", "dir": "in"})
        self.assertNotIn("0", app.files.offers)
        self.assertIn("File transfer canceled: big.zip", plain(app.render()))
        # with a send and an offer in one chat, the send goes first
        home = os.environ["HOME"]
        os.makedirs(home, exist_ok=True)
        path = os.path.join(home, "notes.txt")
        with open(path, "w") as f:
            f.write("x")
        app.edit("text", ":sendfile " + path)
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.on_event({"event": "file.sending", "id": "s1", "conversation": "0", "key": A, "dir": "out", "request": rid})
        offer("f3")
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.cancel")[-1]["id"], "s1")

    def test_transfer_line_stays_at_the_bottom_while_scrolling(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        c = self.loaded(app, "0", *["line %d" % k for k in range(40)])
        app.size = (80, 14)
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        app.files.sends["0"] = cli.OutgoingSend("r", "photo.jpg", A, "f1", False, 470, 1000)
        rows = lambda: [plain(row) for _, row in re.findall(r"\x1b\[(\d+);1H(.*?)\x1b\[K", app.render())]
        def where():
            current = rows()
            return [k for k, row in enumerate(current) if "Sending photo.jpg" in row], current
        bottom, before = where()
        self.assertEqual(len(bottom), 1)
        app.edit("key", "pgup")
        moved, after = where()
        self.assertEqual(moved, bottom)
        self.assertNotEqual(before, after)
        self.assertGreater(app.pane.scroll, 0)
        self.assertTrue(any("line 39" in row for row in before) and not any("line 39" in row for row in after))

    def test_sendfile_refuses_files_over_the_helper_limit(self):
        with open(os.path.join(ROOT, "helper", "file.h"), encoding="utf-8") as f:
            limit = re.search(r"#define OMAQ_FILE_MAX \((\d+)u \* (\d+)u \* (\d+)u\)", f.read())
        self.assertEqual(cli.FILE_MAX, int(limit.group(1)) * int(limit.group(2)) * int(limit.group(3)))
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        home = os.environ["HOME"]
        os.makedirs(home, exist_ok=True)
        for name, size in (("big.bin", cli.FILE_MAX + 1), ("fits.bin", cli.FILE_MAX)):
            with open(os.path.join(home, name), "wb") as f:
                f.truncate(size)
        app.edit("text", ":sendfile ~/big.bin")
        app.edit("key", "enter")
        self.assertEqual((app.status, app.link.ops("file.send")), ("Files can be at most 8 MiB", []))
        app.edit("text", ":sendfile ~/fits.bin")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.send")[-1]["path"], os.path.join(home, "fits.bin"))

    def test_sendfile_picker_odd_names_and_broken_paths(self):
        home = self.sendfile_home()
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (80, 20)
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        cwd = os.getcwd()
        os.chdir(home)
        try:
            for name in ("'secret'", "secret", "~tilde"):
                with open(os.path.join(home, name), "w") as f:
                    f.write(name)
            os.makedirs(os.path.join(home, " lead", "deep"))
            for name, path in (("'secret'", "./'secret'"), ("~tilde", "./~tilde")):
                self.typed(app, ":sendfile ")
                app.edit("key", "tab")
                app.file_picker.index = [n for n, _ in app.file_picker.items("")].index(name)
                app.edit("key", "enter")
                self.assertEqual(app.pane.draft, ":sendfile " + path)
                app.edit("key", "enter")
                self.assertEqual(app.link.ops("file.send")[-1]["path"], os.path.join(home, name))
                app.files.sends.clear()
            self.typed(app, ":sendfile ")
            app.edit("key", "tab")
            app.file_picker.index = [n for n, _ in app.file_picker.items("")].index(" lead")
            app.edit("key", "tab")
            self.assertEqual(app.file_picker.items(cli.sendfile_path(app.pane.draft)), [("deep", True)])
            self.typed(app, ':sendfile "~/my folder/file x.txt"')
            app.edit("key", "tab")
            self.assertEqual(app.file_picker.items(cli.sendfile_path(app.pane.draft)), [("file x.txt", False)])
            self.typed(app, ":sendfile ~/")
            app.edit("key", "tab")
            app.edit("key", "home")
            app.edit("text", "x")
            self.assertEqual((app.pane.draft, app.file_picker), ("x:sendfile ~/", None))
            # a NUL byte, a deleted working directory and a working directory that is not UTF-8 never crash
            self.typed(app, ":sendfile ")
            app.edit("paste", "/a\x00b/")
            app.edit("key", "tab")
            self.assertIn("This folder cannot be read", plain(app.render()))
            app.edit("paste", "c\x00d")
            self.assertTrue(app.pane.draft.endswith("/a\x00b/cd"))
            gone = os.path.join(home, "gone")
            os.makedirs(gone)
            os.chdir(gone)
            os.rmdir(gone)
            self.typed(app, ":sendfile ")
            app.edit("key", "tab")
            self.assertIn("No such folder", plain(app.render()))
            app.edit("key", "esc")
            self.typed(app, ":sendfile rel.txt")
            app.edit("key", "enter")
            self.assertEqual(app.status, "No readable file at rel.txt")
            odd = os.path.join(os.fsencode(home), b"caf\xe9")
            os.makedirs(odd)
            with open(os.path.join(odd, b"ok.txt"), "w") as f:
                f.write("x")
            os.chdir(odd)
            self.typed(app, ":sendfile ok.txt")
            app.edit("key", "enter")
            self.assertEqual(app.status, "Path is not valid UTF-8")
        finally:
            os.chdir(cwd)

    def test_sendfile_picker_never_takes_what_is_not_shown(self):
        home = self.sendfile_home()
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        app.size = (80, 7)
        self.typed(app, ":sendfile ~/Pictures/")
        app.edit("key", "tab")
        self.assertNotIn("night.jpg", plain(app.render()))
        self.assertEqual(plain(app.command_hint(80)).strip(), "enter sends the line as typed")
        app.edit("key", "enter")
        self.assertEqual((app.pane.draft, app.link.ops("file.send")), ("", []))
        app.size = (80, 20)
        app.overlay = cli.InviteOverlay("omaq://invite/x", ["x"])
        self.typed(app, ":sendfile ~/")
        app.edit("key", "tab")
        app.edit("ctrl", "k")
        self.assertEqual((app.file_picker, app.picker), (None, None))
        app.overlay = None
        app.edit("key", "tab")
        self.assertIsNotNone(app.file_picker)
        app.overlay = cli.InviteOverlay("omaq://invite/x", ["x"])
        app.edit("key", "esc")
        self.assertEqual((app.file_picker, app.overlay), (None, None))
        app.size = (80, 20)
        self.typed(app, ":sendfile ~/zzz")
        app.edit("key", "tab")
        self.assertEqual(plain(app.command_hint(80)).strip(), "no match · enter sends the line as typed")
        many = os.path.join(home, "many")
        os.makedirs(many)
        for k in range(30):
            open(os.path.join(many, "bad\x01%02d" % k), "w").close()
        picker, limit = cli.FilePicker(), cli.FilePicker.LIMIT
        cli.FilePicker.LIMIT = 10
        try:
            picker.scan(many)
        finally:
            cli.FilePicker.LIMIT = limit
        self.assertTrue(picker.full)
        self.assertEqual(picker.entries, [])

    def test_sendfile_picker_edges(self):
        home = self.sendfile_home()
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (80, 20)
        plain = lambda text: re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)
        self.typed(app, ":sendfile ~/Pictures/")
        app.edit("key", "tab")
        app.edit("key", "esc")
        self.assertEqual((app.file_picker, app.pane.draft), (None, ":sendfile ~/Pictures/"))
        self.typed(app, ":sendfile ~/.")
        app.edit("key", "tab")
        self.assertEqual(app.file_picker.items("~/."), [(".config", True), (".bashrc", False)])
        self.typed(app, ":sendfile ~/nope/")
        app.edit("key", "tab")
        self.assertIn("No such folder", plain(app.render()))
        if os.geteuid() != 0:
            os.chmod(os.path.join(home, "locked"), 0)
            try:
                self.typed(app, ":sendfile ~/locked/")
                app.edit("key", "tab")
                self.assertIn("No access to this folder", plain(app.render()))
            finally:
                os.chmod(os.path.join(home, "locked"), 0o700)
        self.typed(app, ":sendfile /")
        app.edit("key", "tab")
        app.edit("key", "left")
        self.assertEqual(app.pane.draft, ":sendfile /")
        # names with spaces need no quotes; a name ending in a space gets them
        self.typed(app, ":sendf ~/my")
        app.edit("key", "tab")
        app.edit("key", "tab")
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":sendf ~/my folder/file x.txt")
        self.typed(app, ":sendfile ~/tra")
        app.edit("key", "tab")
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ':sendfile "~/trail "')
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.send")[-1]["path"], os.path.join(home, "trail "))
        # no match: tab keeps the list, enter runs the line as typed
        self.typed(app, ":sendfile ~/zzz")
        app.edit("key", "tab")
        app.edit("key", "tab")
        self.assertIsNotNone(app.file_picker)
        app.edit("key", "enter")
        self.assertIsNone(app.file_picker)
        self.assertIn("No readable file at ~/zzz", app.status)
        self.typed(app, ":sendfile ~/")
        app.edit("key", "tab")
        for _ in range(10):
            app.edit("key", "backspace")
        self.assertEqual((app.pane.draft, app.file_picker), (":s", None))
        self.typed(app, ":sendfile ~/")
        app.edit("key", "tab")
        app.edit("ctrl", "k")
        self.assertIsNone(app.file_picker)
        self.assertIsNotNone(app.picker)
        app.edit("key", "esc")
        self.typed(app, ":con")
        app.edit("key", "tab")
        self.assertEqual(app.pane.draft, ":connect")
        odd = os.path.join(os.fsencode(home), b"caf\xe9")
        os.makedirs(os.path.join(odd, b"inner"))
        cwd = os.getcwd()
        os.chdir(odd)
        try:
            self.typed(app, ":sendfile ")
            app.edit("key", "tab")
            app.render().encode("utf-8")
            self.typed(app, ":sendfile inner/")
            app.edit("key", "tab")
            app.edit("key", "left")
            self.assertEqual(app.pane.draft, ":sendfile inner/")
            app.render().encode("utf-8")
        finally:
            os.chdir(cwd)
        for cols, rows in ((24, 8), (30, 12)):
            app.size = (cols, rows)
            self.typed(app, ":sendfile ~/")
            app.edit("key", "tab")
            frame = app.render()
            self.assertTrue(all(cli.text_width(plain(row)) <= cols for _, row in re.findall(r"\x1b\[(\d+);1H(.*?)\x1b\[K", frame)))
            self.assertIn(" files › ", plain(frame))

    def test_friends_bar_marks_hidden_friends(self):
        friends = [{"id": str(k), "key": ("%02d" % k) * 32, "name": "friend%02d" % k} for k in range(15)]
        friends[0]["name"] = "n" * 60
        app = self.app(friends)
        app.chat.unread["14"] = 2
        bar = app.friends_bar(100)
        text = re.sub(r"\x1b\[[0-9;]*m", "", bar)
        self.assertIn("n" * 17 + "\u2026", text)
        shown = sum(1 for f in friends[1:] if f["name"] in text)
        self.assertIn("\x1b[0;31;1m+%d" % (14 - shown), bar)
        self.assertLessEqual(cli.text_width(text), 100)

    def test_overlong_link_and_failed_send(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.edit("text", ":redeem omaq://" + "a" * 600)
        app.edit("key", "enter")
        self.assertEqual((app.status, app.link.ops("invite.redeem")), ("Link is too long", []))
        app.link.send = lambda obj: False
        app.edit("text", ":redeem omaq://short")
        app.edit("key", "enter")
        self.assertEqual(app.status, "Could not send the request")
        app.link = None
        app.edit("text", ":invite")
        app.edit("key", "enter")
        self.assertEqual(app.status, "Not connected")

    def test_redeem_result_uses_the_bars_wording(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.size = (100, 20)
        app.edit("text", ":redeem omaq://x")
        app.edit("key", "enter")
        rid = app.link.ops("invite.redeem")[-1]["id"]
        app.on_event({"event": "invite.redeemed", "kind": "group", "request": rid})
        self.assertIn("Group invite checked. Waiting for the incoming group request.", app.render())
        self.assertEqual(app.status, "Group invite checked. Waiting for the incoming group request.")

    def test_invite_countdown_redraws_every_minute(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.out = __import__("io").StringIO()
        app.invites.invite = cli.Invite("omaq://x", int(time.time()) + 86400)
        app.overlay = cli.InviteOverlay("omaq://x", [])
        app.tick(time.monotonic())
        app.dirty = False
        app.overlay.minute -= 1
        app.tick(time.monotonic())
        self.assertTrue(app.dirty)
        app.invites.invite.expires = int(time.time()) - 5
        app.size = (100, 20)
        self.assertIn("expired; :invite creates a new link", app.render())

    def test_widths_follow_the_terminal(self):
        saved = dict(cli.WIDTHS)
        try:
            read_fd, write_fd = os.pipe()
            os.write(write_fd, b"\x1b[5;2R\x1b[5;3Rab")
            typed = cli.measure_widths(read_fd, __import__("io").StringIO(), timeout=1.0)
            os.close(read_fd)
            os.close(write_fd)
            self.assertEqual((typed, cli.WIDTHS), (b"ab", {"vs16": 1, "cluster": True}))
            self.assertEqual(cli.text_width("\u2764\ufe0f"), 1)
            self.assertEqual(cli.text_width("\U0001F469\u200d\U0001F4BB"), 2)
            self.assertEqual(cli.text_width("\U0001F44D\U0001F3FD"), 2)
            self.assertEqual(cli.clip("\U0001F469\u200d\U0001F4BBx", 2), ("\U0001F469\u200d\U0001F4BB", 2))
            cli.WIDTHS.update({"vs16": 2, "cluster": False})
            self.assertEqual(cli.text_width("\U0001F469\u200d\U0001F4BB"), 4)
            self.assertEqual(cli.text_width("\U0001F44D\U0001F3FD"), 4)
        finally:
            cli.WIDTHS.clear()
            cli.WIDTHS.update(saved)

    def test_zoom_is_measured_from_the_cell_size(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text)
        app.track_zoom((58, 96, 673, 934))
        self.assertEqual((app.zoom, app.zoom_until), (100, 0.0))
        self.assertNotIn("Text", plain(app.command_hint(100)))
        app.track_zoom((51, 84, 673, 934))
        self.assertEqual(app.zoom, 112)
        self.assertIn("Text 112%", plain(app.command_hint(100)))
        app.tick(app.zoom_until + 1)
        self.assertNotIn("Text", plain(app.command_hint(100)))
        app.track_zoom((40, 70, 500, 730))
        self.assertEqual((app.zoom, app.zoom_until), (112, 0.0))
        app.track_zoom((60, 100, 700, 980))
        self.assertEqual(app.zoom, 100)
        self.assertIn("Text 100%", plain(app.command_hint(100)))
        for size in (None, (40, 70, 0, 0), (0, 0, 0, 0)):
            app.track_zoom(size)
        self.assertEqual(app.zoom, 100)
        line = app.command_hint(100)
        self.assertTrue(plain(line).startswith(" :help   :quit") and plain(line).endswith("Text 100% "))
        self.assertIn("\x1b[0;%sm" % app.palette.roles["dim"] + "Text 100%", line)
        app.zoom, app.zoom_until = 112, time.monotonic() + 60
        line = app.command_hint(100)
        self.assertIn("\x1b[0;%smText 112%%" % app.palette.roles["accent"], line)
        self.assertEqual(cli.text_width(plain(line)), 100)
        self.assertIn("Text 112%", plain(app.command_hint(80)))
        self.assertNotIn("Text", plain(app.command_hint(20)))
        self.assertLessEqual(cli.text_width(plain(app.command_hint(20))), 20)
        app.edit("text", ":")
        self.assertNotIn("Text 112%", plain(app.command_hint(200)))
        app.edit("key", "esc")
        app.locked = True
        app.size = (100, 20)
        self.assertIn("Text 112%", plain(app.render()))

    def test_tips_rotate_while_focused(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text)
        app.tip, app.tip_at, app.focused = 0, 0.0, True
        app.tick(1.0)
        self.assertEqual(app.tip, 1)
        self.assertIn(" :help   :quit   \u00b7   :) <3 turn into emoji", plain(app.command_hint(100)))
        app.focused = False
        app.tick(app.tip_at + 1)
        self.assertEqual(app.tip, 1)
        app.focused = True
        app.edit("text", ":")
        app.tick(app.tip_at + 1)
        self.assertEqual(app.tip, 1)
        app.edit("key", "esc")
        app.zoom, app.zoom_until = 112, time.monotonic() + 60
        for k, (key, words) in enumerate(cli.TIPS):
            app.tip = k
            line = plain(app.command_hint(96))
            self.assertIn("%s %s" % (key, words), line)
            self.assertTrue(line.rstrip().endswith("Text 112%"))
        app.tip = 0
        self.assertEqual(plain(app.command_hint(30)), " :help   :quit      Text 112% ")

    def test_sendfile_and_offers_follow_the_helper(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text)
        home = os.environ["HOME"]
        os.makedirs(home, exist_ok=True)
        path = os.path.join(home, "my notes.txt")
        with open(path, "w") as f:
            f.write("x")
        app.edit("text", ':sendfile "~/my notes.txt"')
        app.edit("key", "enter")
        op = app.link.ops("file.send")[-1]
        self.assertEqual(sorted(op), ["conversation", "id", "key", "kind", "op", "path"])
        self.assertEqual((op["path"], op["kind"], op["key"]), (path, "file", A))
        app.edit("text", ":sendfile " + path)
        app.edit("key", "enter")
        self.assertEqual(app.status, "A file is already sending")
        app.on_event({"event": "file.sending", "id": "f1", "conversation": "0", "key": A, "dir": "out", "request": op["id"]})
        app.on_event({"event": "file.done", "id": "f1", "conversation": "0", "key": A, "path": path, "dir": "out",
                      "request": op["id"]})
        self.assertEqual((app.status, app.files.sends, app.chat.conv("0").loaded), ("File transfer successful: my notes.txt", {}, False))
        app.edit("text", ":sendfile /nonexistent/x")
        app.edit("key", "enter")
        self.assertEqual(app.status, "No readable file at /nonexistent/x")
        app.edit("text", ":sendfile " + path)
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.on_event({"event": "file.failed", "id": "", "conversation": "0", "dir": "out", "request": rid, "code": "busy"})
        self.assertEqual((app.status, app.files.sends), ("File transfer failed: A file is already sending", {}))
        app.on_event({"event": "file.offer", "id": "in0", "conversation": "0", "key": B, "name": "evil", "size": 1,
                      "dir": "in", "request": ""})
        self.assertEqual(app.files.offers, {})
        app.on_event({"event": "file.offer", "id": "in1", "conversation": "0", "key": A, "name": "report.pdf",
                      "size": 2300000, "dir": "in", "request": ""})
        self.assertEqual(app.status, "File offer from bob: report.pdf (2.3 MB)")
        self.assertIn("1;7m bob ", app.friends_bar(100))
        app.size = (100, 20)
        self.assertIn("File offer: report.pdf (2.3 MB)  :acceptfile or :declinefile", plain(app.render()))
        app.on_event({"event": "file.offer", "id": "in2", "conversation": "0", "key": A, "name": "second", "size": 5,
                      "dir": "in", "request": ""})
        self.assertEqual(app.link.ops("file.cancel")[-1], {"op": "file.cancel", "conversation": "0", "id": "in2", "key": A})
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.accept")[-1], {"op": "file.accept", "conversation": "0", "id": "in1", "key": A})
        saved = os.path.join(home, "Downloads", "omaq", "report.pdf")
        app.on_event({"event": "file.done", "id": "in1", "conversation": "0", "key": A, "path": saved, "dir": "in",
                      "request": ""})
        self.assertEqual((app.status, app.files.offers), ("File transfer successful: ~/Downloads/omaq/report.pdf", {}))
        app.on_event({"event": "file.offer", "id": "in3", "conversation": "0", "key": A, "name": "x.bin", "size": 9,
                      "dir": "in", "request": ""})
        app.edit("text", ":declinefile")
        app.edit("key", "enter")
        self.assertEqual((app.link.ops("file.cancel")[-1]["id"], app.files.offers, app.status), ("in3", {}, "Declined file: x.bin"))
        app.edit("text", ":sendfile " + path)
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        cancels = len(app.link.ops("file.cancel"))
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        self.assertEqual((app.status, len(app.link.ops("file.cancel"))), ("Canceling…", cancels))
        app.on_event({"event": "file.sending", "id": "f2", "conversation": "0", "key": A, "dir": "out", "request": rid})
        self.assertEqual(app.link.ops("file.cancel")[-1]["id"], "f2")
        app.edit("text", ":sendfile " + path)
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.on_event({"event": "file.progress", "id": "f3", "conversation": "0", "key": A, "dir": "out", "request": rid,
                      "done": 700, "size": 1000})
        self.assertIn("Sending my notes.txt  \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2591\u2591\u2591 70%",
                      plain(app.render()))
        app.on_event({"event": "file.progress", "id": "f3", "conversation": "0", "key": B, "dir": "out", "request": rid,
                      "done": 900, "size": 1000})
        self.assertEqual(app.files.sends["0"].done, 700)
        app.on_event({"event": "file.offer", "id": "in5", "conversation": "0", "key": A, "name": "big.iso", "size": 4000,
                      "dir": "in", "request": ""})
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        self.assertIn("File offer: big.iso (4.0 KB)  receiving…", plain(app.render()))
        app.on_event({"event": "file.progress", "id": "in5", "conversation": "0", "key": A, "dir": "in", "request": "",
                      "done": 1000, "size": 4000})
        self.assertIn("Receiving big.iso  \u2588\u2588\u2591\u2591\u2591\u2591\u2591\u2591\u2591\u2591 25%",
                      plain(app.render()))
        self.assertEqual(cli.progress_bar(5, 5), ("\u2588" * 10, "", 100))
        app.files.offers["0"] = cli.FileOffer("in4", "y", 1, A)
        app.drop_link()
        self.assertEqual((set(app.files.offers), set(app.files.sends)), ({"0"}, {"0"}))
        app.link = FakeLink()
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": app.instance, "protocol": 16,
                      "online": True})
        self.assertEqual(app.link.ops("file.status"), [{"op": "file.status", "conversation": "0", "id": rid, "key": A},
                                                       {"op": "file.status", "conversation": "0", "id": "in4", "key": A}])
        self.assertEqual((set(app.files.offers), app.files.offers["0"].unconfirmed), ({"0"}, True))
        app.drop_link()
        app.link = FakeLink()
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": "j" * 32, "protocol": 16, "online": True})
        self.assertEqual((app.files.offers, app.files.sends, app.status), ({}, {}, "File transfer failed: helper_restarted"))
        self.assertEqual(cli.human_size(999), "999 B")

    def test_bar_wording_stays_in_sync(self):
        bar = ""
        for path in ("Panel.qml", "pages/ChatPage.qml", "Service.qml"):
            with open(os.path.join(ROOT, path), encoding="utf-8") as f:
                bar += f.read()
        client = ""
        for folder, _, names in sorted(os.walk(os.path.join(ROOT, "cli"))):
            for name in sorted(names):
                if name.endswith(".py"):
                    with open(os.path.join(folder, name), encoding="utf-8") as f:
                        client += f.read()
        for text in ("That is not an OmaQ invite.", "Finish active calls, transfers, invites, or read receipts first.",
                     "Compare this code with your friend before accepting", "Another device used this invite link",
                     "Revoke this invite? It cannot be used again.",
                     "Invite checked. Waiting for the other person to accept.",
                     "Group invite checked. Waiting for the incoming group request.",
                     "Invite expired. Create a new link.", "This invite belongs to your own identity.",
                     "The direct-contact limit has been reached.", "A file is already sending",
                     "File transfer successful", "File transfer canceled", "File transfer failed: ",
                     "File delivered, but local history could not be saved", "Clear this chat?",
                     "Nickname must contain 1–18 valid characters.", "Nickname update failed. Try again.",
                     "Nickname saved; group updates are unconfirmed.", "Nickname update is unavailable.",
                     "Nickname update timed out. Try again.", cli.HISTORY_ITEM_FAILED, cli.UNSAVED_SENT):
            self.assertIn(text, bar)
            self.assertIn(text, client)

    def test_nickname_follows_the_helper(self):
        app = cli.App("")
        app.link = FakeLink()
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": "n" * 32, "protocol": 16, "online": True,
                      "nickname": "mo"})
        app.command(":nickname")
        self.assertEqual(app.status, "Your nickname: mo")
        for name in ("x" * 19, "a\x7fb", "a\x85b"):
            app.command(":nickname " + name)
            self.assertEqual((app.status, app.link.ops("nickname.set")), ("Nickname must contain 1–18 valid characters.", []))
        app.command(":n Neo")
        self.assertEqual(app.link.ops("nickname.set"), [])
        app.command(":nickname  Neo the One ")
        op = app.link.ops("nickname.set")[-1]
        self.assertEqual((op["nickname"], app.status), ("Neo the One", "Saving nickname…"))
        app.on_event({"event": "nickname", "value": "from the bar"})
        self.assertEqual((app.profile.nickname, app.status), ("from the bar", "Saving nickname…"))
        app.on_event({"event": "nickname", "value": "Neo the One", "request": op["id"]})
        self.assertEqual(app.status, "Nickname changed to Neo the One")
        for code, text in (("nickname_invalid", "Nickname must contain 1–18 valid characters."),
                           ("nickname_update_failed", "Nickname update failed. Try again."),
                           ("unsupported", "Nickname update is unavailable."),
                           ("busy", "Nickname update failed. Try again.")):
            app.command(":nickname Trinity")
            rid = app.link.ops("nickname.set")[-1]["id"]
            app.on_event({"event": "error", "code": code, "request": "someone-else"})
            self.assertEqual(app.status, "Saving nickname…")
            app.on_event({"event": "error", "code": code, "request": rid})
            self.assertEqual(app.status, text)
        app.command(":nickname Trinity")
        rid = app.link.ops("nickname.set")[-1]["id"]
        app.on_event({"event": "nickname", "value": "Trinity"})
        app.on_event({"event": "error", "code": "nickname_group_sync_failed", "request": rid})
        self.assertEqual((app.profile.nickname, app.status), ("Trinity", "Nickname saved; group updates are unconfirmed."))
        app.command(":nickname Morpheus")
        app.tick(time.monotonic() + 11)
        self.assertEqual((app.status, app.profile.request), ("Nickname update timed out. Try again.", ""))
        app.command(":nickname Morpheus")
        app.drop_link()
        self.assertIn("Nickname result unknown; try again", app.status)
        app.command(":nickname Morpheus")
        self.assertEqual(app.status, "Not connected")

    def test_theme_change_repaints_with_the_new_colors(self):
        theme = os.path.join(os.environ["HOME"], ".local", "state", "omarchy", "current", "theme")
        os.makedirs(theme)
        path = os.path.join(theme, "colors.toml")

        def write(accent):
            with open(path, "w", encoding="utf-8") as f:
                f.write('foreground = "#c0caf5"\nbackground = "#1a1b26"\naccent = "%s"\n'
                        'color1 = "#f7768e"\ncolor2 = "#9ece6a"\ncolor3 = "#e0af68"\n' % accent)

        write("#7aa2f7")
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.assertEqual(app.palette.roles["accent"], "38;2;122;162;247")
        write("#549e6a")
        stat = os.stat(path)
        os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns + 10 ** 9))
        app.dirty = False
        app.tick(time.monotonic() + 2)
        self.assertEqual((app.palette.roles["accent"], app.dirty), ("38;2;84;158;106", True))
        palette = app.palette
        app.tick(time.monotonic() + 4)
        self.assertIs(app.palette, palette)
        os.remove(path)
        app.tick(time.monotonic() + 6)
        self.assertIs(app.palette, palette)
        write("#7aa2f7")
        app.tick(time.monotonic() + 8)
        self.assertEqual(app.palette.roles["accent"], "38;2;122;162;247")
        write("#549e6a")
        stat = os.stat(path)
        os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns + 10 ** 9))
        app.tick(time.monotonic() + 10)
        app.size = (80, 20)
        self.assertIn("38;2;84;158;106", app.render())

    def test_every_table_handler_exists(self):
        app = self.app([])
        for handler in list(cli.EVENT_HANDLERS.values()) + list(cli.COMMAND_HANDLERS.values()):
            self.assertTrue(callable(operator.attrgetter(handler)(app)), handler)

    def test_receipt_glyphs(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        c = self.loaded(app, "0")
        for ack in range(4):
            c.items.append(cli.Message("o%d" % ack, "", "out", "m%d" % ack, time.time(), ack=ack))
        c.items.append(cli.Message("f", "", "out", "failed", time.time(), failed="offline"))
        segs = [s for s in app.message_lines("0", 80) if not s[0][0].startswith(" \u2500")]
        labels = [(s[-1][0], s[-1][1], s[-1][2]) for s in segs]
        self.assertEqual(labels[0][:2], ("·", "dim"))
        self.assertEqual(labels[1][:2], ("→", "dim"))
        self.assertEqual(labels[2], ("⇉", "dim", ""))
        self.assertEqual(labels[3], ("⇉", "online", "1"))
        self.assertEqual(labels[4][:2], ("✕ Contact is offline", "unread"))


class SessionRules(unittest.TestCase):
    def test_rebound_reports_moved_and_vanished_keys(self):
        old = [{"id": "0", "key": A}, {"id": "1", "key": B}, {"id": "2", "key": C}]
        new = [{"id": "0", "key": B}, {"id": "2", "key": C}, {"id": "3", "key": A}]
        self.assertEqual(cli.rebound(old, new), [("0", "3"), ("1", "0")])
        self.assertEqual(cli.rebound(old, old), [])

    def test_merge_history_keeps_unresolved_messages_once(self):
        sent = cli.Message("m1", "r1", "out", "kept by id", 1.0)
        unknown = cli.Message("", "r2", "out", "unknown", 2.0, failed="delivery_unknown")
        pending = cli.Message("", "r3", "out", "pending", 3.0)
        saved = cli.Message("m4", "r4", "out", "sent", 4.0)
        merged = cli.merge_history([sent, unknown, saved], [{"id": "m1", "dir": "out", "text": "kept by id"}],
                                   [pending, unknown])
        self.assertEqual([m.text for m in merged], ["kept by id", "unknown", "pending"])
        self.assertIsNot(merged[0], sent)

    def test_unsaved_note_follows_its_message(self):
        first, second = cli.Message("a", "ra", "out", "a", 5.0), cli.Message("b", "rb", "out", "b", 6.0)
        items = [first, second]
        self.assertIs(cli.find_sent(items, "ra"), first)
        cli.mark_unsaved(items, first, 9.0)
        self.assertEqual([(m.dir, m.text, m.ts) for m in items],
                         [("out", "a", 5.0), ("sys", cli.UNSAVED_SENT, 5.0), ("out", "b", 6.0)])
        self.assertEqual((first.failed, first.ack, first.local), ("", 1, True))


class Blockers(Sandbox):
    def plain(self, text):
        return re.sub(r"\x1b\[[0-9;]*m", "", text)

    def snapshot(self, app, instance=None):
        app.link = FakeLink()
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": instance or app.instance, "protocol": 16,
                      "online": True})

    def offer(self, app, conversation, key, fid, name="old.txt"):
        app.on_event({"event": "file.offer", "id": fid, "conversation": conversation, "key": key, "name": name,
                      "size": 4, "dir": "in", "request": ""})

    def file(self, name="a.txt"):
        home = os.environ["HOME"]
        os.makedirs(home, exist_ok=True)
        path = os.path.join(home, name)
        with open(path, "w") as f:
            f.write("x")
        return path

    def test_r01_recall_ends_with_the_identity(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.edit("text", "private Alice message")
        app.edit("key", "enter")
        app.edit("text", ":requests")
        app.edit("key", "enter")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "private Alice message")
        app.on_event({"event": "identity", "op": "import"})
        app.handshake = True
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"}])
        self.loaded(app, "0")
        app.edit("key", "up")
        app.edit("key", "enter")
        app.edit("key", "down")
        app.edit("key", "enter")
        self.assertEqual(len(app.link.ops("msg.send")), 1)
        app.edit("text", ":")
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, ":")

    def test_r01_recall_ends_when_the_number_moves(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0")
        app.edit("text", "for alice")
        app.edit("key", "enter")
        app.edit("key", "up")
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"},
                           {"id": "1", "key": B, "name": "bob", "online": True, "status": "online"}])
        self.loaded(app, app.pane.conv)
        app.edit("key", "up")
        app.edit("key", "enter")
        self.assertEqual([o["key"] for o in app.link.ops("msg.send")], [A])

    def test_r02_clear_confirmation_follows_the_contact(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.history_clear = 2
        app.edit("text", ":clear")
        app.edit("key", "enter")
        self.assertEqual((app.confirm.key, app.confirm.instance), (A, app.instance))
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"}])
        self.assertEqual((app.confirm, app.status), (None, "Chat kept"))
        app.edit("text", "y")
        app.edit("key", "esc")
        self.assertEqual(app.link.ops("history.clear"), [])
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.confirm.key = A
        app.edit("text", "y")
        self.assertEqual((app.link.ops("history.clear"), app.status), ([], "Chat kept"))
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.edit("text", "y")
        self.assertEqual([(o["conversation"], o["key"]) for o in app.link.ops("history.clear")], [("0", C)])

    def test_r03_background_status_keeps_the_prompt(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0")
        app.history_clear = 2
        app.size = (100, 20)
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.on_event({"event": "message", "conversation": "1", "key": B, "dir": "in", "id": "m1", "text": "hello",
                      "ts": time.time()})
        self.assertEqual(app.status, "bob: hello")
        frame = self.plain(app.render())
        self.assertIn("Clear this chat? y/n", frame)
        self.assertNotIn("bob: hello", frame)
        app.edit("text", "y")
        self.assertEqual(len(app.link.ops("history.clear")), 1)
        locked = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(locked, "0")
        locked.history_clear = 2
        locked.edit("text", ":clear")
        locked.edit("key", "enter")
        locked.on_event({"event": "error", "code": "locked"})
        self.assertEqual((locked.confirm, locked.status), (None, "Chat kept"))

    def test_r04_nospam_change_keeps_the_identity_state(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.pane.draft = "draft"
        app.addr = "d" * 64 + "11111111" + "2222"
        app.handshake_request()
        app.on_event({"event": "snapshot", "request": app.nonce, "instance": app.instance, "protocol": 16,
                      "addr": "d" * 64 + "33333333" + "4444", "online": True})
        self.assertEqual((app.pane.draft, len(app.friends), app.handshake), ("draft", 1, True))
        self.assertEqual(app.addr, "d" * 64 + "33333333" + "4444")

    def test_r05_unknown_delivery_survives_the_reload(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.edit("text", "unsaved text")
        app.edit("key", "enter")
        app.edit("text", "confirmed")
        app.edit("key", "enter")
        confirmed = app.link.ops("msg.send")[-1]["id"]
        app.on_event({"event": "message", "conversation": "0", "key": A, "dir": "out", "id": "m9",
                      "request": confirmed, "text": "confirmed", "ts": 1700000000})
        app.drop_link()
        app.link, app.handshake = FakeLink(), True
        app.chat.load("0")
        request = app.link.ops("history")[-1]["id"]
        app.on_event({"event": "history", "request": request, "conversation": "0", "key": A,
                      "items": [{"id": "m9", "dir": "out", "text": "confirmed", "ts": 1700000000}]})
        self.assertEqual([(m.text, m.failed) for m in app.chat.conv("0").items],
                         [("confirmed", ""), ("unsaved text", "delivery_unknown")])
        app.edit("key", "up")
        self.assertEqual(app.pane.draft, "unsaved text")

    def test_r06_file_errors_follow_the_helper(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        self.offer(app, "0", A, "0:65536")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        app.on_event({"event": "file.failed", "id": "0:65536", "conversation": "0", "key": A, "dir": "in",
                      "request": "", "code": "accept_failed"})
        app.on_event({"event": "error", "code": "forbidden"})
        self.assertEqual((app.files.offers, app.status), ({}, "File transfer failed: accept_failed"))
        self.offer(app, "0", A, "0:65537", "report.pdf")
        self.assertEqual((app.files.offers["0"].id, app.link.ops("file.cancel")), ("0:65537", []))
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        app.on_event({"event": "file.done", "id": "0:65537", "conversation": "0", "key": A,
                      "path": "/srv/x/report.pdf", "dir": "in", "request": ""})
        app.on_event({"event": "error", "code": "history_failed", "conversation": "0"})
        self.assertEqual(app.status, "File transfer successful: /srv/x/report.pdf \u00b7 " + cli.HISTORY_ITEM_FAILED)
        app.files.result = ("0", "old", time.monotonic() - 10)
        app.on_event({"event": "error", "code": "history_failed", "conversation": "0"})
        self.assertEqual(app.status, cli.HISTORY_ITEM_FAILED)

    def test_r07_reconnect_keeps_transfer_control(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.edit("text", ":sendfile " + self.file())
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.edit("text", ":cancelfile")
        app.edit("key", "enter")
        app.drop_link()
        self.snapshot(app)
        self.assertEqual(app.link.ops("file.status"), [{"op": "file.status", "conversation": "0", "id": rid, "key": A}])
        app.on_event({"event": "file.sending", "id": "0:3", "conversation": "0", "key": A, "dir": "out", "request": rid})
        self.assertEqual(app.link.ops("file.cancel"), [{"op": "file.cancel", "conversation": "0", "id": "0:3", "key": A}])
        app.on_event({"event": "file.canceled", "id": "0:3", "conversation": "0", "key": A, "dir": "out", "request": rid})
        self.assertEqual((app.files.sends, app.status), ({}, "File transfer canceled"))

    def test_r08_rebind_drops_offers_and_sends(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        self.offer(app, "0", A, "0:65536")
        app.edit("text", ":sendfile " + self.file())
        app.edit("key", "enter")
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"}])
        self.assertEqual((app.files.offers, app.files.sends), ({}, {}))
        self.assertEqual(app.status, "File transfer failed: " + cli.FAILURES["identity_changed"])
        self.offer(app, "0", C, "0:65536", "carol.txt")
        app.files.offers["0"].key = A
        app.edit("text", ":declinefile")
        app.edit("key", "enter")
        self.assertEqual((app.link.ops("file.cancel"), app.status, app.files.offers), ([], "No file offer in this chat", {}))

    def test_r09_file_commands_stay_in_the_focused_chat(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.loaded(app, "0")
        self.offer(app, "1", B, "1:65536")
        for verb in ("acceptfile", "declinefile"):
            app.edit("text", ":" + verb)
            app.edit("key", "enter")
            self.assertEqual(app.status, "No file offer in this chat")
        self.assertEqual((app.link.ops("file.accept"), app.link.ops("file.cancel")), ([], []))
        app.edit("key", "tab")
        self.assertEqual(app.pane.conv, "1")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("file.accept"), [{"op": "file.accept", "conversation": "1", "id": "1:65536",
                                                         "key": B}])

    def test_r10_foreign_errors_and_finished_actions_are_ignored(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.edit("text", ":redeem omaq://x")
        app.edit("key", "enter")
        rid = app.link.ops("invite.redeem")[-1]["id"]
        app.on_event({"event": "error", "code": "unsupported", "request": "bar-request"})
        app.on_event({"event": "error", "code": "forbidden", "conversation": "0"})
        self.assertEqual((app.invites.redeem_request, app.status), (rid, "Checking the invite\u2026"))
        app.on_event({"event": "invite.redeemed", "kind": "group", "request": rid})
        app.on_event({"event": "error", "code": "unsupported"})
        self.assertEqual(app.status, cli.REDEEMED_GROUP)
        app.invites.request = cli.PendingRequest("direct", B, "", "")
        app.edit("text", ":accept")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "forbidden", "request": "x"})
        self.assertEqual(app.status, "Accepted")
        app.on_event({"event": "error", "code": "forbidden"})
        self.assertEqual(app.status, "The request could not be accepted.")
        app.on_event({"event": "contact.decided", "request": "other", "ok": True})
        self.assertEqual(app.status, "The request could not be accepted.")
        app.on_event({"event": "contact.decided", "request": app.invites.decide_request, "ok": True})
        app.on_event({"event": "error", "code": "forbidden"})
        self.assertEqual((app.status, app.invites.decide_request), ("Accepted", ""))
        app.invites.request = cli.PendingRequest("direct", B, "", "")
        app.edit("text", ":accept")
        app.edit("key", "enter")
        app.on_event({"event": "contact.decided", "request": app.invites.decide_request, "ok": False, "code": "forbidden"})
        self.assertEqual(app.status, "The request could not be accepted.")

    def test_r11_quit_before_the_handshake_settles_it(self):
        sent, (readable, writable) = [], os.pipe()
        os.write(writable, b"x")

        class Sock:
            def __init__(self, path):
                pass

            def send(self, obj):
                sent.append(obj)
                return True

            def read(self):
                status = [o for o in sent if o.get("op") == "status"]
                return [{"event": "snapshot", "request": status[-1]["id"], "instance": "k" * 32, "protocol": 16,
                         "online": True}] if status else []

            def fileno(self):
                return readable

            def close(self):
                pass

        try:
            app = cli.App("")
            app.open_link = Sock
            app.settle()
            self.assertEqual(sent, [])
            app.helper.proc = type("Proc", (), {"poll": lambda self: None, "pid": 1})()
            app.settle()
            self.assertEqual((app.handshake, app.instance), (True, "k" * 32))
            self.assertEqual([o["op"] for o in sent], ["status", "identity.ready"])
            app.settle()
            self.assertEqual(len(sent), 2)
        finally:
            os.close(readable)
            os.close(writable)

    def test_r13_canceled_transfer_stays_until_dismissed(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        app.size = (100, 20)
        self.offer(app, "0", A, "0:65536", "notes.pdf")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        app.on_event({"event": "file.canceled", "id": "0:65536", "conversation": "0", "key": A, "dir": "in"})
        app.tick(time.monotonic() + 60)
        line = "\u2715 File transfer canceled: notes.pdf  esc hides"
        self.assertIn(line, self.plain(app.render()))
        app.edit("text", "draft")
        app.edit("key", "esc")
        self.assertEqual(app.pane.draft, "")
        self.assertIn(line, self.plain(app.render()))
        app.edit("key", "esc")
        self.assertNotIn("\u2715 File transfer canceled", self.plain(app.render()))
        app.edit("text", ":sendfile " + self.file("report.txt"))
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.on_event({"event": "file.canceled", "id": "0:1", "conversation": "0", "key": A, "dir": "out", "request": rid})
        self.assertIn("File transfer canceled: report.txt", self.plain(app.render()))
        self.offer(app, "0", A, "0:65537", "next.pdf")
        frame = self.plain(app.render())
        self.assertEqual(("\u2715 File transfer canceled" in frame, "File offer: next.pdf" in frame), (False, True))
        app.on_event({"event": "file.canceled", "id": "0:65537", "conversation": "0", "key": A, "dir": "in"})
        self.assertIn("File transfer canceled: next.pdf", self.plain(app.render()))
        app.apply_friends([{"id": "0", "key": B, "name": "bob", "online": True, "status": "online"}])
        self.assertNotIn("\u2715 File transfer canceled", self.plain(app.render()))

    def test_r14_cursor_steps_over_drawn_clusters(self):
        family = "\U0001F468\u200d\U0001F469\u200d\U0001F467"
        thumb = "\U0001F44D\U0001F3FF"
        saved = dict(cli.WIDTHS)
        try:
            cli.WIDTHS.update({"vs16": 2, "cluster": True})
            pane = cli.Pane("0")
            pane.insert("a" + family + thumb + "b")
            pane.edit("key", "left")
            self.assertEqual(pane.draft[pane.cursor:], "b")
            pane.edit("key", "left")
            self.assertEqual(pane.draft[pane.cursor:], thumb + "b")
            pane.edit("key", "backspace")
            self.assertEqual((pane.draft, pane.cursor), ("a" + thumb + "b", 1))
            pane.edit("key", "delete")
            self.assertEqual(pane.draft, "ab")
            cli.WIDTHS.update({"cluster": False})
            pane.draft, pane.cursor = family, len(family)
            pane.edit("key", "backspace")
            self.assertEqual(pane.draft, "\U0001F468\u200d\U0001F469\u200d")
        finally:
            cli.WIDTHS.clear()
            cli.WIDTHS.update(saved)

    def test_r12_small_windows_wrap_codes_and_scroll(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        safety = " / ".join(" ".join("%04x" % (k + h * 16) for k in range(16)) for h in range(2))
        groups = safety.replace(" / ", " ").split()
        app.invites.request = cli.PendingRequest("direct", C, safety, "")
        app.overlay = cli.RequestOverlay()
        app.size = (40, 16)
        frame = self.plain(app.render())
        self.assertEqual([g for g in groups if g not in frame], [])
        self.assertIn(" " + " ".join(groups[8:16]) + "\x1b[K", frame)
        app.size = (160, 16)
        self.assertIn(" " + " ".join(groups[:16]) + " ", self.plain(app.render()))
        app.size = (40, 8)
        self.assertRegex(self.plain(app.render()), "\u2193 \\d+ more \u00b7 pgdn")
        app.edit("key", "pgdn")
        self.assertGreater(app.overlay.top, 0)
        self.assertRegex(self.plain(app.render()), "\u2191 \\d+ more \u00b7 pgup")
        app.edit("key", "pgup")
        self.assertEqual(app.overlay.top, 0)
        app.overlay = None
        app.size = (80, 24)
        app.edit("key", "f1")
        frame = self.plain(app.render())
        self.assertNotIn("New pane with the next friend", frame)
        self.assertRegex(frame, "\u2193 \\d+ more \u00b7 pgdn")
        app.edit("key", "pgdn")
        self.assertIn("New pane with the next friend", self.plain(app.render()))
        app.edit("key", "esc")
        app.edit("key", "f1")
        self.assertEqual(app.help.top, 0)
        app.size = (80, 12)
        for _ in range([command for command, _, _ in cli.COMMANDS].index(":close")):
            app.edit("key", "down")
        self.assertIn("\u25b8 :close", self.plain(app.render()))
        app.edit("key", "esc")
        app.size = (100, 60)
        app.edit("key", "f1")
        app.edit("key", "pgdn")
        frame = self.plain(app.render())
        self.assertNotIn(" more \u00b7 pg", frame)
        self.assertIn("New pane with the next friend", frame)

    def test_success_notice_is_short_and_warnings_stay(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}])
        self.loaded(app, "0")
        self.offer(app, "0", A, "0:65536", "short.txt")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        app.on_event({"event": "file.done", "id": "0:65536", "conversation": "0", "key": A, "path": "/srv/short.txt",
                      "dir": "in", "request": ""})
        self.assertEqual((app.status, app.status_seconds), ("File transfer successful: /srv/short.txt", 7.5))
        app.edit("text", ":sendfile " + self.file())
        app.edit("key", "enter")
        rid = app.link.ops("file.send")[-1]["id"]
        app.on_event({"event": "file.done", "id": "0:1", "conversation": "0", "key": A, "path": "", "dir": "out",
                      "request": rid, "code": "history_failed"})
        self.assertEqual((app.status, app.status_seconds), (cli.FILE_ERRORS["history_failed"], 15))

    def test_f02_incoming_transfers_resync_after_a_reconnect(self):
        app = self.app([{"id": "0", "key": A, "name": "bob"}, {"id": "1", "key": B, "name": "carol"}])
        self.loaded(app, "0")
        app.size = (100, 20)
        self.offer(app, "0", A, "0:65536")
        app.edit("text", ":acceptfile")
        app.edit("key", "enter")
        self.offer(app, "1", B, "1:65536", "pending.txt")
        app.drop_link()
        self.snapshot(app)
        asked = {o["id"]: o["conversation"] for o in app.link.ops("file.status")}
        self.assertEqual(asked, {"0:65536": "0", "1:65536": "1"})
        self.assertIn("File offer: old.txt (4 B)  checking\u2026", self.plain(app.render()))
        self.offer(app, "0", A, "0:131072", "new.txt")
        self.assertEqual((app.files.offers["0"].name, app.link.ops("file.cancel")), ("new.txt", []))
        app.on_event({"event": "file.state", "id": "1:65536", "conversation": "1", "key": B, "dir": "in",
                      "state": "offer", "name": "pending.txt", "size": 4, "done": 0})
        self.assertEqual((app.files.offers["1"].unconfirmed, app.files.offers["1"].accepted), (False, False))
        app.files.offers["1"].unconfirmed = True
        app.on_event({"event": "file.state", "id": "1:65536", "conversation": "1", "key": B, "dir": "in",
                      "state": "receiving", "name": "", "size": 4000, "done": 1000})
        self.assertEqual((app.files.offers["1"].accepted, app.files.offers["1"].done), (True, 1000))
        app.files.offers["1"].unconfirmed = True
        app.on_event({"event": "file.state", "id": "1:65536", "conversation": "1", "key": A, "dir": "in",
                      "state": "unknown", "name": "", "size": 0, "done": 0})
        self.assertIn("1", app.files.offers)
        app.on_event({"event": "file.state", "id": "1:65536", "conversation": "1", "key": B, "dir": "in",
                      "state": "unknown", "name": "", "size": 0, "done": 0})
        self.assertEqual(("1" in app.files.offers, app.status), (False, "File transfer status unknown; check the chat"))

    def test_f03_notices_behind_a_confirmation_are_kept(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.history_clear = 2
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "history_failed", "conversation": "0"})
        app.edit("text", "n")
        self.assertEqual(app.status, "Chat kept \u00b7 " + cli.HISTORY_ITEM_FAILED)
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.on_event({"event": "error", "code": "history_failed", "conversation": "0"})
        app.tick(app.confirm.until + 1)
        self.assertEqual(app.status, "Chat kept \u00b7 " + cli.HISTORY_ITEM_FAILED)
        app.say("")
        app.edit("text", ":clear")
        app.edit("key", "enter")
        app.edit("text", "n")
        self.assertEqual(app.status, "Chat kept")

    def test_f05_f07_unconfirmed_exits_say_so(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        sent = []
        app.owned_by_cli = lambda: True
        app.bar_attached = lambda: False
        app.presence.others_alive = lambda: False
        app.link = type("L", (), {"send": lambda s, o: sent.append(o) or True, "read": lambda s: [],
                                  "fileno": lambda s: os.open("/dev/null", os.O_RDONLY), "close": lambda s: None})()
        original = cli.select.select
        try:
            cli.select.select = lambda r, w, x, t: ([], [], [])
            original_monotonic = cli.time.monotonic
            clock = [original_monotonic()]
            cli.time.monotonic = lambda: clock.__setitem__(0, clock[0] + 1) or clock[0]
            app.exit_helper()
        finally:
            cli.select.select = original
            cli.time.monotonic = original_monotonic
        self.assertEqual([o["op"] for o in sent], ["helper.shutdown_if_no_groups"])
        self.assertEqual(app.exit_note, "%s: %s" % (cli.COMMAND, cli.UNCONFIRMED_EXIT))
        other = self.app([{"id": "0", "key": A, "name": "alice"}])
        calls = []
        other.presence.exit_lock = lambda seconds: None
        other.exit_helper = lambda: calls.append("exit")
        fd = os.open("/dev/null", os.O_RDONLY)
        try:
            other.cleanup(fd, None, __import__("io").StringIO())
        except BaseException:
            pass
        finally:
            os.close(fd)
        self.assertEqual((calls, other.exit_note), ([], "%s: %s" % (cli.COMMAND, cli.UNCOORDINATED_EXIT)))

    def test_f06_help_enter_never_runs_a_hidden_command(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.size = (80, 12)
        app.edit("key", "f1")
        for _ in range([command for command, _, _ in cli.COMMANDS].index(":quit")):
            app.edit("key", "down")
        app.render()
        app.edit("key", "pgup")
        app.render()
        app.edit("key", "enter")
        self.assertEqual((app.running, app.help is not None), (True, True))
        self.assertIn("\u25b8 :quit", self.plain(app.render()))
        app.edit("key", "enter")
        self.assertFalse(app.running)
        for height in (1, 2, 3, 4):
            rows, top, span = app.window(["r%d" % k for k in range(10)], 20, height, 0)
            self.assertEqual(len(rows), height)
        self.assertEqual(app.window(["a", "b"], 20, 5, 0)[2], 2)

    def test_reopened_help_waits_for_a_frame_before_enter(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        app.size = (80, 24)
        app.edit("key", "f1")
        app.render()
        app.edit("key", "esc")
        app.edit("key", "f1")
        app.edit("key", "down")
        app.edit("key", "enter")
        self.assertEqual((app.help is not None, (app.pane or app.scratch).draft), (True, ""))
        app.render()
        app.edit("key", "enter")
        self.assertEqual((app.help, (app.pane or app.scratch).draft), (None, ":connect "))

    def test_r12_notes_wrap_texts_and_separate_halves(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        safety = " / ".join(" ".join("%04x" % (k + h * 16) for k in range(16)) for h in range(2))
        app.invites.request = cli.PendingRequest("direct", C, safety, B)
        app.overlay = cli.RequestOverlay()
        app.size = (40, 24)
        rows = [re.sub(r"\x1b\[[0-9;]*m", "", r) for r in app.overlay_rows(40, 20)]
        self.assertIn(" Compare this code with your friend", [r.rstrip() for r in rows])
        self.assertIn(" before accepting", [r.rstrip() for r in rows])
        codes = [k for k, r in enumerate(rows) if r.strip()[:4] in ("0000", "0008", "0010", "0018")]
        self.assertEqual(len(codes), 4)
        self.assertEqual(rows[codes[2] - 1].strip(), "")
        app.size = (160, 24)
        wide = [re.sub(r"\x1b\[[0-9;]*m", "", r).rstrip() for r in app.overlay_rows(160, 20)]
        halves = [k for k, r in enumerate(wide) if r.strip().startswith(("0000", "0010"))]
        self.assertEqual(halves[1] - halves[0], 1)

    def test_m09_delivered_but_unsaved_message_is_shown(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.edit("text", "hello")
        app.edit("key", "enter")
        rid = app.link.ops("msg.send")[-1]["id"]
        app.on_event({"event": "message", "conversation": "0", "key": A, "dir": "out", "id": "m5", "request": rid,
                      "text": "hello", "ts": 0})
        app.on_event({"event": "message.failed", "conversation": "0", "request": rid, "code": "history_failed",
                      "delivered": True})
        shown = [(m.dir, m.text, m.failed, m.ack) for m in app.chat.conv("0").items]
        self.assertEqual(shown, [("out", "hello", "", 1), ("sys", cli.UNSAVED_SENT, "", 0)])
        self.assertIn(cli.UNSAVED_SENT, "".join(seg[0] for line in app.message_lines("0", 100) for seg in line))
        app.drop_link()
        app.link, app.handshake = FakeLink(), True
        app.chat.load("0")
        app.on_event({"event": "history", "request": app.link.ops("history")[-1]["id"], "conversation": "0", "key": A,
                      "items": []})
        self.assertEqual([m.text for m in app.chat.conv("0").items], ["hello", cli.UNSAVED_SENT])
        app.edit("text", "later")
        app.edit("key", "enter")
        rid = app.link.ops("msg.send")[-1]["id"]
        app.on_event({"event": "message.failed", "conversation": "0", "request": rid, "code": "offline",
                      "delivered": False})
        self.assertEqual(app.chat.conv("0").items[-1].failed, "offline")

    def test_m03_presence_is_per_state_and_exits_take_turns(self):
        import fcntl
        one = cli.Presence(os.path.join(self.base, "state-one"))
        two = cli.Presence(os.path.join(self.base, "state-two"))
        root = os.path.join(os.environ["XDG_RUNTIME_DIR"], "omaq-cli")
        self.assertNotEqual(one.dir, two.dir)
        self.assertEqual({os.path.dirname(one.dir), os.path.dirname(two.dir)}, {root})
        self.assertFalse(one.others_alive())
        held = one.exit_lock(1.0)
        self.assertIsNotNone(held)
        start = time.monotonic()
        self.assertIsNone(one.exit_lock(0.3))
        self.assertGreaterEqual(time.monotonic() - start, 0.3)
        os.close(held)
        again = one.exit_lock(0.3)
        self.assertIsNotNone(again)
        os.close(again)
        self.assertFalse(one.others_alive())
        self.assertTrue(os.path.exists(os.path.join(one.dir, "exit.mutex")))
        peer = os.open(os.path.join(one.dir, "424242.lock"), os.O_RDWR | os.O_CREAT, 0o600)
        fcntl.flock(peer, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.assertTrue(one.others_alive())
        self.assertFalse(two.others_alive())
        os.close(peer)
        self.assertFalse(one.others_alive())
        for presence in (one, two):
            presence.release()


class Replies(Sandbox):
    def plain(self, text):
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)

    def rows(self, app, width=80):
        return [self.plain(app.palette.row(segs, width)) for segs in app.message_lines("0", width)]

    def chat(self, app):
        c = app.chat.conv("0")
        c.loaded, c.request = True, ""
        message = cli.Message
        c.items = [message("m1", "", "in", "first from alice", 1700000000.0),
                   message("f1", "", "in", "[file] notes.txt", 1700000001.0, kind="file"),
                   message("", "", "sys", "a note", 1700000002.0, local=True),
                   message("m2", "r2", "out", "my answer", 1700000003.0, ack=2),
                   message("x1", "", "in", "", 1700000004.0, deleted=True),
                   message("m3", "", "in", "latest from alice", 1700000005.0),
                   message("", "r9", "out", "still sending", 1700000006.0),
                   message("", "r8", "out", "did not go", 1700000007.0, failed="offline")]
        return c

    def test_ctrl_r_walks_only_messages_that_can_be_answered(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.chat(app)
        self.assertEqual([m.id for m in app.chat.targets("0")], ["m1", "m2", "m3"])
        app.edit("ctrl", "r")
        self.assertEqual(app.pane.select, "m3")
        app.edit("key", "up")
        self.assertEqual(app.pane.select, "m2")
        app.edit("ctrl", "r")
        self.assertEqual(app.pane.select, "m1")
        app.edit("key", "up")
        self.assertEqual(app.pane.select, "m1")
        app.edit("key", "end")
        self.assertEqual(app.pane.select, "m3")
        app.edit("key", "pgup")
        self.assertEqual(app.pane.select, "m1")
        app.edit("key", "pgdn")
        self.assertEqual(app.pane.select, "m3")
        app.edit("key", "home")
        self.assertEqual(app.pane.select, "m1")

    def test_enter_picks_and_the_reply_goes_with_the_next_message(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.chat(app)
        app.size = (80, 16)
        app.edit("ctrl", "r")
        self.assertIn("↑/↓ choose · enter reply · esc cancel", self.plain(app.render()))
        app.edit("key", "up")
        app.edit("key", "enter")
        self.assertEqual((app.pane.select, app.pane.reply), (None, "m2"))
        frame = self.plain(app.render())
        self.assertIn("↩ Reply: my answer", frame)
        self.assertNotIn("enter reply", frame)
        app.edit("text", "yes")
        app.edit("key", "enter")
        sent = app.link.ops("msg.send")[-1]
        self.assertEqual((sent["text"], sent["reply"], sent["key"]), ("yes", "m2", A))
        self.assertEqual((app.pane.reply, app.chat.conv("0").items[-1].reply), ("", "m2"))
        self.assertNotIn("Reply:", self.plain(app.render()))
        app.edit("ctrl", "r")
        app.edit("text", "r")
        self.assertEqual(app.pane.reply, "m3")
        app.edit("key", "esc")
        app.edit("text", "plain")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send")[-1]["reply"], "")

    def test_esc_order_and_paste_never_picks(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.chat(app)
        app.edit("text", "draft")
        app.edit("ctrl", "r")
        app.edit("paste", "r")
        self.assertEqual((app.pane.select, app.pane.reply, app.pane.draft), (None, "", "draftr"))
        app.edit("ctrl", "r")
        app.edit("text", "rx")
        self.assertEqual((app.pane.select, app.pane.reply, app.pane.draft), (None, "", "draftrrx"))
        app.edit("ctrl", "r")
        app.edit("key", "esc")
        self.assertEqual((app.pane.select, app.pane.reply, app.pane.draft), (None, "", "draftrrx"))
        app.edit("ctrl", "r")
        app.edit("key", "enter")
        self.assertEqual(app.link.ops("msg.send"), [])
        app.edit("key", "esc")
        self.assertEqual((app.pane.reply, app.pane.draft), ("", "draftrrx"))
        app.edit("key", "esc")
        self.assertEqual(app.pane.draft, "")

    def test_without_messages_or_chat_it_says_why(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0")
        app.edit("ctrl", "r")
        self.assertEqual((app.pane.select, app.status), (None, "No message to reply to"))
        self.chat(app)
        app.edit("text", ":reply")
        app.edit("key", "enter")
        self.assertEqual(app.pane.select, "m3")
        empty = cli.App("")
        empty.edit("ctrl", "r")
        self.assertEqual(empty.status, "No chat is open")
        self.assertTrue(any(key == "ctrl+r" for key, _ in cli.KEYS))
        self.assertTrue(any(command == ":reply" for command, _, _ in cli.COMMANDS))

    def test_the_selection_stays_in_view_and_incoming_messages_do_not_move_it(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.loaded(app, "0", *["line %d" % k for k in range(30)])
        app.edit("ctrl", "r")
        for _ in range(20):
            app.edit("key", "up")
        self.assertEqual(app.pane.select, "m9")
        rows = [self.plain(r) for r in app.screen.pane_rows(app.pane, 60, 10, True)]
        marked = [r for r in rows if r.startswith("▌")]
        self.assertEqual(len(marked), 1, rows)
        self.assertIn("line 9", marked[0])
        scroll = app.pane.scroll
        app.on_event({"event": "message", "conversation": "0", "key": A, "dir": "in", "id": "late",
                      "text": "late", "ts": 1700000100})
        self.assertEqual(app.pane.scroll, scroll)
        rows = [self.plain(r) for r in app.screen.pane_rows(app.pane, 60, 10, True)]
        self.assertTrue(any(r.startswith("▌") and "line 9" in r for r in rows), rows)

    def test_replies_quote_the_loaded_message_like_the_bar(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        c = self.loaded(app, "0")
        message = cli.Message
        quoted = "word   \n\tnext " + "x" * 130
        c.items = [message("q1", "", "in", quoted, 1700000000.0),
                   message("a1", "", "out", "answer", 1700000001.0, ack=1, reply="q1"),
                   message("a2", "", "in", "orphan", 1700000002.0, reply="not-loaded"),
                   message("a3", "", "in", "to a deleted one", 1700000003.0, reply="d1"),
                   message("d1", "", "in", "", 1700000004.0, deleted=True)]
        rows = self.rows(app, 300)
        quotes = [k for k, r in enumerate(rows) if "↩" in r]
        self.assertEqual(len(quotes), 2, rows)
        self.assertIn("↩ " + cli.compact_preview(quoted), rows[quotes[0]])
        self.assertIn("answer", rows[quotes[0] + 1])
        self.assertIn("↩ Message deleted", rows[quotes[1]])
        self.assertTrue(all(cli.text_width(r) <= 40 for r in self.rows(app, 40)))

    def test_reply_preview_follows_the_bar_rules(self):
        self.assertEqual(cli.compact_preview("  a \n\t b  "), "a b")
        self.assertEqual(cli.compact_preview("a\u00a0\ufeffb"), "a b")
        self.assertEqual(cli.compact_preview("x" * 120), "x" * 120)
        self.assertEqual(cli.compact_preview("x" * 121), "x" * 120 + "…")
        for unit in ("\U0001F1E9\U0001F1EA", "\U0001F468\u200d\U0001F469\u200d\U0001F467", "e\u0301",
                     "\U0001F44D\U0001F3FD"):
            self.assertEqual(cli.compact_preview(unit * 121), unit * 120 + "…", repr(unit))

    def test_edits_and_deletes_arrive_live_and_end_a_reply_to_them(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        self.chat(app)
        app.edit("ctrl", "r")
        app.edit("key", "enter")
        app.on_event({"event": "message.updated", "conversation": "0", "key": A, "id": "m1",
                      "text": "first, fixed", "deleted": False, "edited": True})
        self.assertTrue(any("first, fixed" in r and "(edited)" in r for r in self.rows(app)), self.rows(app))
        app.on_event({"event": "message.updated", "conversation": "0", "key": B, "id": "m3", "text": "",
                      "deleted": True, "edited": False})
        self.assertEqual(app.pane.reply, "m3")
        app.on_event({"event": "message.updated", "conversation": "0", "key": A, "id": "m3", "text": "",
                      "deleted": True, "edited": False})
        self.assertEqual((app.pane.reply, app.status), ("", "Reply canceled: the message was deleted"))
        self.assertEqual([m.id for m in app.chat.targets("0")], ["m1", "m2"])
        self.assertEqual(sum("Message deleted" in r for r in self.rows(app)), 2)
        self.assertFalse(any("latest from alice" in r for r in self.rows(app)))

    def test_history_items_keep_reply_edit_and_delete(self):
        m = cli.message_from({"id": "a", "dir": "in", "text": "hi", "reply": "b", "edited": True})
        self.assertEqual((m.reply, m.edited, m.deleted, m.text), ("b", True, False, "hi"))
        gone = cli.message_from({"id": "c", "dir": "in", "text": "secret", "deleted": True})
        self.assertEqual((gone.text, gone.deleted, cli.replyable(gone), cli.shown_text(gone)),
                         ("", True, False, cli.DELETED))

    def test_reply_state_follows_the_chat(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        self.chat(app)
        app.edit("ctrl", "r")
        app.edit("key", "enter")
        app.edit("key", "tab")
        self.assertEqual((app.pane.conv, app.pane.reply, app.pane.select), ("1", "", None))
        app.edit("key", "tab")
        app.edit("ctrl", "r")
        app.edit("key", "enter")
        app.apply_friends([{"id": "0", "key": C, "name": "carol", "online": True, "status": "online"},
                           {"id": "5", "key": A, "name": "alice", "online": True, "status": "online"},
                           {"id": "1", "key": B, "name": "bob", "online": True, "status": "online"}])
        self.assertEqual((app.pane.conv, app.pane.reply), ("5", ""))
        self.chat(app)
        app.show("0")
        app.edit("ctrl", "r")
        app.reset_identity()
        self.assertEqual((app.pane.reply, app.pane.select), ("", None))


class Input(unittest.TestCase):
    def feed(self, *chunks):
        keys = cli.Keys()
        out = []
        for chunk in chunks:
            out += keys.feed(chunk)
        out += keys.feed(b"", final=True)
        return out

    def test_text_and_utf8_split(self):
        self.assertEqual(self.feed("hä".encode()[:2], "hä".encode()[2:]), [("text", "h"), ("text", "ä")])

    def test_special_keys(self):
        self.assertEqual(self.feed(b"\x1b[A\x1b[5~\x1b[Z\r\x7f\x03"),
                         [("key", "up"), ("key", "pgup"), ("key", "backtab"), ("key", "enter"),
                          ("key", "backspace"), ("ctrl", "c")])

    def test_alt_arrows_and_focus(self):
        self.assertEqual(self.feed(b"\x1b[1;3D\x1b[I\x1b[O"),
                         [("key", "alt-left"), ("key", "focus-in"), ("key", "focus-out")])

    def test_lone_escape(self):
        self.assertEqual(self.feed(b"\x1b"), [("key", "esc")])

    def test_modifiers_f1_and_shift_enter(self):
        self.assertEqual(self.feed(b"\x1b[1;4C\x1b[13;2u\x1bOP\x1b[11~\x1bh\x1bL\x1b="),
                         [("key", "alt-shift-right"), ("key", "shift-enter"), ("key", "f1"), ("key", "f1"),
                          ("alt", "h"), ("alt", "L"), ("alt", "=")])

    def test_bracketed_paste_split(self):
        self.assertEqual(self.feed(b"\x1b[200~a\r\nb", b"\x1b[20", b"1~x"),
                         [("paste", "a\nb"), ("text", "x")])


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False, verbosity=1).result.wasSuccessful() else 1)
