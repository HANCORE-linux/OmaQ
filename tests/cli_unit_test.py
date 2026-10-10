#!/usr/bin/env python3
import importlib.util
import json
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

    def test_stamp(self):
        now = 1760000000.0
        today = cli.time.strftime("%Y-%m-%d", cli.time.localtime(now))
        self.assertEqual(len(cli.stamp(now, today)), 5)
        self.assertEqual(len(cli.stamp(now - 3 * 86400, today)), 16)
        self.assertEqual(cli.stamp(0, today), "")


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
            shutil.copy2(os.path.join(ROOT, "manifest.json"), root / "manifest.json")
            module.link_cli(root)
            link = os.path.join(base, "home", ".local", "bin", "omaq-connect")
            self.assertEqual(os.readlink(link), str(root / "cli" / "omaq.py"))
            module.link_cli(Path(base) / "missing")
            self.assertEqual(os.readlink(link), str(root / "cli" / "omaq.py"))
        finally:
            os.environ["HOME"] = old
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

    def loaded(self, app, conversation, *texts):
        c = app.conv(conversation)
        c["loaded"], c["request"] = True, ""
        for k, text in enumerate(texts):
            c["items"].append(cli.Message("m%d" % k, "", "in", text, 1700000000.0))
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
        self.assertNotIn("for alice", [m.text for m in app.conv("0")["items"]])
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
        request = app.conv("0")["request"]
        app.on_event({"event": "history.failed", "conversation": "0", "request": request, "code": "history_failed"})
        self.assertEqual(app.conv("0")["request"], "")
        app.tick(time.monotonic())
        self.assertEqual(len(app.link.ops("history")), 1)
        app.retry_at["0"] = 0.0
        app.tick(time.monotonic())
        self.assertEqual(len(app.link.ops("history")), 2)
        self.loaded(app, "0", "old")
        app.edit("text", "in flight")
        app.edit("key", "enter")
        app.drop_link()
        self.assertEqual(app.conv("0")["items"][-1].failed, "delivery_unknown")
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
        app.unread["0"] = 1
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
        c["items"].append(cli.Message("m", "", "in", "the important body", time.time() - 5 * 86400))
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
                    'yellow = "#ffff00"\naccent = "#0000ff"\n')
        roles = cli.Palette().roles
        self.assertEqual(roles["unread"], "38;2;255;0;0")
        self.assertEqual(roles["name"], "38;2;255;255;0")
        self.assertEqual(roles["accent"], "38;2;0;0;255")

    def test_command_hint_and_colon_suggestions(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}, {"id": "1", "key": B, "name": "bob"}])
        plain = lambda text: re.sub(r"\x1b\[[0-9;]*m", "", text).strip()
        self.assertEqual(plain(app.command_hint(80)), ":help   :connect   :split   :quit   :qa")
        app.edit("text", ":")
        self.assertEqual(plain(app.command_hint(80)).split(), [":help", ":connect", ":split", ":quit", ":qa"])
        self.assertIn("1;7m:help", app.command_hint(80))
        app.edit("text", "c")
        self.assertEqual(plain(app.command_hint(80)).split(), [":connect"])
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
        self.assertEqual(app.help, 0)

    def test_receipt_glyphs(self):
        app = self.app([{"id": "0", "key": A, "name": "alice"}])
        c = self.loaded(app, "0")
        for ack in range(4):
            c["items"].append(cli.Message("o%d" % ack, "", "out", "m%d" % ack, time.time(), ack=ack))
        c["items"].append(cli.Message("f", "", "out", "failed", time.time(), failed="offline"))
        segs = app.message_lines("0", 80)
        labels = [(s[-1][0], s[-1][1], s[-1][2]) for s in segs]
        self.assertEqual(labels[0][:2], ("·", "dim"))
        self.assertEqual(labels[1][:2], ("→", "dim"))
        self.assertEqual(labels[2], ("⇉", "dim", ""))
        self.assertEqual(labels[3], ("⇉", "online", "1"))
        self.assertEqual(labels[4][:2], ("✕ Contact is offline", "unread"))


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
