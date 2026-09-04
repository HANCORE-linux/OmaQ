#!/usr/bin/env python3
"""Offline adversarial checks for the shell-off source installer."""

from __future__ import annotations

import contextlib
import http.server
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT / "scripts/install-omaq.py"
SPEC = importlib.util.spec_from_file_location("omaq_source_install", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeShell:
    def __init__(self, events: list[str]):
        self.events = events
        self.omarchy = "/usr/bin/omarchy"
        self.ipc_env = {}
        self.live_root = None
        self.started = object()

    def refuse_locked_session(self):
        self.events.append("unlocked")

    def assert_running(self):
        self.events.append("assert-running")
        return object()

    def assert_same_shell(self, _expected):
        self.events.append("assert-same-shell")

    def assert_stopped(self):
        self.events.append("assert-stopped")

    def stop(self, *, require_running):
        self.events.append(f"stop:{require_running}")

    def start(self):
        self.events.append("start")
        return self.started

    def wait_running(self):
        self.events.append("wait-running")
        return self.started

    def journal_cursor(self):
        self.events.append("journal-cursor")
        return "cursor"

    def journal_failed(self, _cursor):
        self.events.append("journal-check")
        return ""

    def consumer_ready(self, *_args, **_kwargs):
        self.events.append("consumer-ready")
        return {
            "state": "current",
            "running_sha256": "b" * 64,
            "available_sha256": "b" * 64,
        }


class SourceInstallTests(unittest.TestCase):
    def test_wrapper_uses_fixed_system_interpreter_and_dirname(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_bin = Path(directory) / "bin"
            marker = Path(directory) / "executed"
            fake_bin.mkdir()
            for name in ("python3", "dirname"):
                command = fake_bin / name
                command.write_text(
                    f"#!/bin/sh\ntouch {marker}\nexit 99\n", encoding="utf-8"
                )
                command.chmod(0o755)
            bash_env = Path(directory) / "bash-env"
            bash_env.write_text(f"/usr/bin/touch {marker}\n", encoding="utf-8")
            (fake_bin / "sitecustomize.py").write_text(
                f"from pathlib import Path\nPath({str(marker)!r}).touch()\n",
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["PATH"] = f"{fake_bin}:/usr/bin:/bin"
            environment["BASH_ENV"] = str(bash_env)
            environment["ENV"] = str(bash_env)
            environment["PYTHONHOME"] = str(fake_bin)
            environment["PYTHONPATH"] = str(fake_bin)
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            checkout_scripts = Path(directory) / "checkout/scripts"
            checkout_scripts.mkdir(parents=True)
            for name in ("install-omaq.sh", "install-omaq.py", "update-omaq.py"):
                source = ROOT / "scripts" / name
                copied = checkout_scripts / name
                copied.write_bytes(source.read_bytes())
                copied.chmod(source.stat().st_mode & 0o777)
            cache = checkout_scripts / "__pycache__"
            for command in (
                checkout_scripts / "install-omaq.sh",
                checkout_scripts / "install-omaq.py",
            ):
                result = subprocess.run(
                    [str(command), "--help"],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    env=environment,
                )
                self.assertEqual(result.returncode, 0)
            self.assertFalse(cache.exists())
            self.assertFalse(marker.exists())

    def test_install_section_is_optional_and_strict(self):
        self.assertEqual(MODULE.parse_args(["--yes"]).section, "")
        self.assertEqual(
            MODULE.parse_args(["--section", "left", "--yes"]).section,
            "left",
        )
        for arguments in (
            ["--section", "top", "--yes"],
            ["--section", "", "--yes"],
            ["--expect-commit", "", "--yes"],
        ):
            with (
                self.subTest(arguments=arguments),
                contextlib.redirect_stderr(io.StringIO()),
                self.assertRaises(SystemExit),
            ):
                MODULE.parse_args(arguments)

    def test_preflight_only_does_not_build_or_install(self):
        with (
            mock.patch.object(MODULE, "Installer") as installer_type,
            contextlib.redirect_stdout(io.StringIO()),
        ):
            installer = installer_type.return_value
            installer.preflight.return_value = ("a" * 40, 14, object())
            self.assertEqual(
                MODULE.main(["--preflight-only", "--section", "right", "--yes"]),
                0,
            )
        installer_type.assert_called_once_with("", "right")
        installer.preflight.assert_called_once_with()
        installer.install.assert_not_called()
        installer.close.assert_called_once_with()

    def test_build_repeats_preflight_before_invoking_make(self):
        class StopBeforeBuild(Exception):
            pass

        events: list[str] = []
        installer = object.__new__(MODULE.Installer)
        installer.source = Path("/tmp/omaq-preflight-order")

        def preflight():
            events.append("preflight")
            return "a" * 40, 14, object()

        def stop_at_build(*_args, **_kwargs):
            events.append("build")
            raise StopBeforeBuild

        installer.preflight = preflight
        with (
            mock.patch.object(MODULE.core, "command_path", return_value="/usr/bin/make"),
            mock.patch.object(
                MODULE.core, "run_tree_bounded", side_effect=stop_at_build
            ),
            self.assertRaises(StopBeforeBuild),
        ):
            installer.preflight_and_build()
        self.assertEqual(events, ["preflight", "build"])

    def test_git_environment_disables_replace_objects(self):
        self.assertEqual(
            MODULE.core.git_environment().get("GIT_NO_REPLACE_OBJECTS"), "1"
        )

    def test_documented_bootstrap_is_syntax_checked_and_bounded(self):
        documentation = (ROOT / "docs/INSTALLATION.md").read_text(encoding="utf-8")
        marker = (
            "/usr/bin/python3 -I - \"$bootstrap\" "
            "\"$expected_commit\" <<'PY'\n"
        )
        self.assertEqual(documentation.count(marker), 1)
        self.assertIn("mode=update", documentation)
        self.assertNotIn("OMAQ_BOOTSTRAP_AUTH", documentation)
        self.assertNotIn("/usr/bin/gh auth", documentation)
        self.assertIn('case "$state_home" in', documentation)
        start = documentation.index(marker) + len(marker)
        end = documentation.index("\nPY\n", start)
        compile(documentation[start:end], "documented-install-bootstrap", "exec")
        bootstrap = documentation[start:end]
        self.assertLess(
            bootstrap.index("remote[0] != expected"),
            bootstrap.index('[*git, "clone"'),
        )
        for required in (
            "maximum_output = 1024 * 1024",
            "maximum_entries = 50000",
            "maximum_tree = 512 * 1024 * 1024",
            "time.monotonic() + timeout",
            "os.killpg(process.pid, signal.SIGKILL)",
            "http.followRedirects=false",
            "Expected commit must be complete lowercase 40-hex",
            'network_home = root + ".network-home"',
            "network_parent = os.path.dirname(network_home)",
            '"HOME": network_home',
            '"GIT_CEILING_DIRECTORIES": network_parent',
            '"/usr/bin/git", "-c", "core.hooksPath=/dev/null"',
            '"-c", "credential.helper="',
            '"-c", "http.extraHeader="',
            "env=env, cwd=network_home",
            "os.rmdir(network_home)",
        ):
            self.assertIn(required, bootstrap)
        self.assertNotIn('"HOME": os.path.expanduser("~")', bootstrap)
        self.assertLess(
            bootstrap.index("os.mkdir(network_home, mode=0o700)"),
            bootstrap.index('[*git, "ls-remote"'),
        )

    def test_public_bootstrap_home_does_not_send_netrc_credentials(self):
        class CredentialHandler(http.server.BaseHTTPRequestHandler):
            authorizations: list[str | None] = []

            def do_GET(self):
                authorization = self.headers.get("Authorization")
                self.authorizations.append(authorization)
                if authorization is None:
                    self.send_response(401)
                    self.send_header("WWW-Authenticate", 'Basic realm="omaq-test"')
                else:
                    self.send_response(404)
                self.end_headers()

            def log_message(self, _format, *_args):
                return

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), CredentialHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                credential_home = root / "credential-home"
                isolated_home = root / "isolated-home"
                credential_home.mkdir(mode=0o700)
                isolated_home.mkdir(mode=0o700)
                netrc = credential_home / ".netrc"
                netrc.write_text(
                    "machine 127.0.0.1 login leaked password secret\n",
                    encoding="utf-8",
                )
                netrc.chmod(0o600)
                url = f"http://127.0.0.1:{server.server_port}/repo.git"

                def request(home: Path) -> list[str | None]:
                    CredentialHandler.authorizations.clear()
                    subprocess.run(
                        ["/usr/bin/git", "ls-remote", url],
                        check=False,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                        timeout=5,
                        env={
                            "HOME": str(home),
                            "PATH": "/usr/bin:/bin",
                            "GIT_CONFIG_GLOBAL": "/dev/null",
                            "GIT_CONFIG_NOSYSTEM": "1",
                            "GIT_TERMINAL_PROMPT": "0",
                        },
                    )
                    return list(CredentialHandler.authorizations)

                self.assertTrue(any(request(credential_home)))
                self.assertFalse(any(request(isolated_home)))
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

    def test_shell_config_parser_binds_only_enabled_locations(self):
        disabled = {
            "bar": {
                "layout": {
                    "left": ["omarchy.workspaces"],
                    "center": [],
                    "right": [{"id": "omarchy.tray"}],
                }
            },
            "plugins": [],
            "disabledPlugins": [MODULE.core.PLUGIN_ID],
        }
        self.assertFalse(MODULE.plugin_enabled_in_shell_config(disabled))

        for enabled in (
            {"bar": {"id": MODULE.core.PLUGIN_ID}},
            {
                "bar": {
                    "layout": {
                        "left": [],
                        "center": [],
                        "right": [{"id": MODULE.core.PLUGIN_ID}],
                    }
                }
            },
            {"plugins": [{"id": MODULE.core.PLUGIN_ID}]},
        ):
            with self.subTest(enabled=enabled):
                self.assertTrue(MODULE.plugin_enabled_in_shell_config(enabled))

        with self.assertRaisesRegex(MODULE.InstallError, "ambiguous bar entry"):
            MODULE.plugin_enabled_in_shell_config(
                {"bar": {"layout": {"left": [7]}}}
            )
        with self.assertRaisesRegex(MODULE.InstallError, "plugins field"):
            MODULE.plugin_enabled_in_shell_config({"plugins": {}})

        invalid_ids = ([MODULE.core.PLUGIN_ID], [[MODULE.core.PLUGIN_ID]], 7, {}, None)
        for identifier in invalid_ids:
            with self.subTest(location="bar.id", identifier=identifier):
                with self.assertRaisesRegex(MODULE.InstallError, "bar id"):
                    MODULE.plugin_enabled_in_shell_config(
                        {"bar": {"id": identifier}}
                    )
            with self.subTest(location="layout", identifier=identifier):
                with self.assertRaisesRegex(MODULE.InstallError, "ambiguous bar"):
                    MODULE.plugin_enabled_in_shell_config(
                        {"bar": {"layout": {"right": [{"id": identifier}]}}}
                    )
            with self.subTest(location="plugins", identifier=identifier):
                with self.assertRaisesRegex(MODULE.InstallError, "ambiguous plugin"):
                    MODULE.plugin_enabled_in_shell_config(
                        {"plugins": [{"id": identifier}]}
                    )

    def test_persisted_shell_config_is_rechecked_without_following_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            config_dir = home / ".config/omarchy"
            config_dir.mkdir(parents=True)
            config = config_dir / "shell.json"
            config.write_text(
                '{"version":1,"bar":{"layout":{"left":[],"center":[],'
                '"right":[{"id":"hancore.omaq"}]}},"plugins":[]}\n',
                encoding="utf-8",
            )
            config.chmod(0o600)
            with mock.patch.object(MODULE.Path, "home", return_value=home):
                with self.assertRaisesRegex(
                    MODULE.InstallError, "enables OmaQ before installation"
                ):
                    MODULE.require_persisted_plugin_disabled()

            for value in ({}, {"version": True}, {"version": 2}):
                config.write_text(
                    MODULE.core.json.dumps(value), encoding="utf-8"
                )
                with mock.patch.object(MODULE.Path, "home", return_value=home):
                    with self.assertRaisesRegex(
                        MODULE.InstallError, "unsupported version"
                    ):
                        MODULE.persisted_shell_config()

            config.unlink()
            with mock.patch.object(MODULE.Path, "home", return_value=home):
                self.assertIsNone(MODULE.persisted_shell_config())
            config.symlink_to(home / "attacker-config")
            with mock.patch.object(MODULE.Path, "home", return_value=home):
                with self.assertRaises(OSError):
                    MODULE.persisted_shell_config()

    def test_atomic_move_never_replaces_an_existing_target(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            target_parent = base / "plugins"
            target = target_parent / MODULE.core.PLUGIN_ID
            source.mkdir()
            target_parent.mkdir()
            source_identity = MODULE.core.directory_identity(source)
            callbacks = []
            MODULE.move_tree_noreplace(
                source, target, lambda: callbacks.append("stopped")
            )
            self.assertEqual(callbacks, ["stopped"])
            self.assertFalse(source.exists())
            self.assertEqual(MODULE.core.directory_identity(target), source_identity)

            another = base / "another"
            another.mkdir()
            with self.assertRaisesRegex(MODULE.InstallError, "already exists"):
                MODULE.move_tree_noreplace(
                    another, target, lambda: callbacks.append("unexpected")
                )
            self.assertTrue(another.is_dir())
            self.assertEqual(callbacks, ["stopped"])

    def test_atomic_move_does_not_overwrite_a_raced_target(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            target_parent = base / "plugins"
            target = target_parent / MODULE.core.PLUGIN_ID
            source.mkdir()
            target_parent.mkdir()

            def race_target():
                target.mkdir()

            with self.assertRaisesRegex(MODULE.InstallError, "target appeared"):
                MODULE.move_tree_noreplace(source, target, race_target)
            self.assertTrue(source.is_dir())
            self.assertTrue(target.is_dir())

    def test_complete_checkout_rejects_sparse_promisor_and_alternates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            subprocess.run(
                ["/usr/bin/git", "init", "--initial-branch=main", str(root)],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            for key, value in (
                ("user.name", "Test"),
                ("user.email", "test@example.invalid"),
            ):
                subprocess.run(
                    ["/usr/bin/git", "-C", str(root), "config", key, value],
                    check=True,
                )
            (root / "file").write_text("fixture\n", encoding="utf-8")
            subprocess.run(
                ["/usr/bin/git", "-C", str(root), "add", "file"], check=True
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(root), "commit", "-m", "fixture"],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            MODULE.validate_complete_git_checkout(root)

            external_common = Path(directory) / "common"
            external_common.mkdir()
            (root / ".git/commondir").write_text(
                str(external_common), encoding="utf-8"
            )
            with mock.patch.object(
                MODULE.core,
                "git_output",
                side_effect=[
                    str(root),
                    str(root / ".git"),
                    str(external_common),
                ],
            ):
                with self.assertRaisesRegex(
                    MODULE.InstallError, "common metadata is outside"
                ):
                    MODULE.validate_complete_git_checkout(root)
            (root / ".git/commondir").unlink()

            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "update-index",
                    "--assume-unchanged",
                    "file",
                ],
                check=True,
            )
            (root / "file").write_text("substituted\n", encoding="utf-8")
            with self.assertRaisesRegex(MODULE.InstallError, "index contains"):
                MODULE.validate_complete_git_checkout(root)
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "update-index",
                    "--no-assume-unchanged",
                    "file",
                ],
                check=True,
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(root), "checkout", "--", "file"],
                check=True,
            )

            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "update-index",
                    "--skip-worktree",
                    "file",
                ],
                check=True,
            )
            (root / "file").unlink()
            with self.assertRaisesRegex(MODULE.InstallError, "index contains"):
                MODULE.validate_complete_git_checkout(root)
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "update-index",
                    "--no-skip-worktree",
                    "file",
                ],
                check=True,
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(root), "checkout", "--", "file"],
                check=True,
            )

            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "config",
                    "core.sparseCheckout",
                    "true",
                ],
                check=True,
            )
            with self.assertRaisesRegex(MODULE.InstallError, "sparse checkout"):
                MODULE.validate_complete_git_checkout(root)
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "config",
                    "--unset",
                    "core.sparseCheckout",
                ],
                check=True,
            )

            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "config",
                    "remote.origin.promisor",
                    "true",
                ],
                check=True,
            )
            with self.assertRaisesRegex(MODULE.InstallError, "promisor remote"):
                MODULE.validate_complete_git_checkout(root)
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(root),
                    "config",
                    "--unset",
                    "remote.origin.promisor",
                ],
                check=True,
            )

            alternates = root / ".git/objects/info/alternates"
            alternates.write_text("/tmp/untrusted-objects\n", encoding="utf-8")
            with self.assertRaisesRegex(
                MODULE.InstallError, "unsupported Git metadata"
            ):
                MODULE.validate_complete_git_checkout(root)

    def test_complete_checkout_rejects_shallow_and_gitlink_trees(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            origin = base / "origin"
            subprocess.run(
                ["/usr/bin/git", "init", "--initial-branch=main", str(origin)],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            for key, value in (
                ("user.name", "Test"),
                ("user.email", "test@example.invalid"),
            ):
                subprocess.run(
                    ["/usr/bin/git", "-C", str(origin), "config", key, value],
                    check=True,
                )
            (origin / "file").write_text("one\n", encoding="utf-8")
            subprocess.run(
                ["/usr/bin/git", "-C", str(origin), "add", "file"], check=True
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(origin), "commit", "-m", "one"],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            (origin / "file").write_text("two\n", encoding="utf-8")
            subprocess.run(
                ["/usr/bin/git", "-C", str(origin), "commit", "-am", "two"],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            shallow = base / "shallow"
            subprocess.run(
                [
                    "/usr/bin/git",
                    "clone",
                    "--depth=1",
                    f"file://{origin}",
                    str(shallow),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            with self.assertRaisesRegex(
                MODULE.InstallError, "must not be shallow"
            ):
                MODULE.validate_complete_git_checkout(shallow)

            commit = subprocess.check_output(
                ["/usr/bin/git", "-C", str(origin), "rev-parse", "HEAD"],
                text=True,
            ).strip()
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(origin),
                    "update-index",
                    "--add",
                    "--cacheinfo",
                    f"160000,{commit},nested",
                ],
                check=True,
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(origin), "commit", "-m", "gitlink"],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            with self.assertRaisesRegex(MODULE.InstallError, "gitlink"):
                MODULE.validate_complete_git_checkout(origin)
            subprocess.run(
                [
                    "/usr/bin/git",
                    "-C",
                    str(origin),
                    "rm",
                    "--cached",
                    "nested",
                ],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            subprocess.run(
                ["/usr/bin/git", "-C", str(origin), "commit", "-m", "remove"],
                check=True,
                stdout=subprocess.DEVNULL,
            )
            ancestor = subprocess.check_output(
                ["/usr/bin/git", "-C", str(origin), "rev-parse", "HEAD~2"],
                text=True,
            ).strip()
            (origin / ".git/objects" / ancestor[:2] / ancestor[2:]).unlink()
            with self.assertRaisesRegex(MODULE.InstallError, "object graph"):
                MODULE.validate_complete_git_checkout(origin)

    def test_atomic_move_rechecks_stopped_state_immediately_before_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            target_parent = base / "plugins"
            target = target_parent / MODULE.core.PLUGIN_ID
            source.mkdir()
            target_parent.mkdir()
            renamed = []

            def reject_running_shell():
                raise MODULE.InstallError("shell restarted")

            def renamer(*_args):
                renamed.append(True)

            with self.assertRaisesRegex(MODULE.InstallError, "shell restarted"):
                MODULE.move_tree_noreplace(
                    source, target, reject_running_shell, rename_fn=renamer
                )
            self.assertTrue(source.is_dir())
            self.assertFalse(target.exists())
            self.assertEqual(renamed, [])

    def test_startup_discovery_poll_tolerates_only_missing_entries(self):
        events: list[str] = []
        shell = FakeShell(events)
        valid = {
            "id": MODULE.core.PLUGIN_ID,
            "enabled": False,
            "firstParty": False,
            "kinds": ["bar-widget"],
        }
        with mock.patch.object(
            MODULE, "plugin_entries", side_effect=[[], [], [valid]]
        ) as listed:
            MODULE.wait_plugin_state(
                shell,
                enabled=False,
                expected_shell=shell.started,
                cursor="cursor",
                timeout=2,
            )
        self.assertEqual(listed.call_count, 3)

        active = dict(valid, enabled=True)
        with mock.patch.object(MODULE, "plugin_entries", return_value=[active]):
            with self.assertRaises(MODULE.ActivationPossibleError):
                MODULE.wait_plugin_state(
                    shell,
                    enabled=False,
                    expected_shell=shell.started,
                    cursor="cursor",
                    timeout=1,
                )

    def test_install_uses_startup_discovery_then_one_enable(self):
        source_text = MODULE_PATH.read_text(encoding="utf-8")
        self.assertNotIn("rescanPlugins", source_text)

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.section = "left"
            installer.preflight_and_build = lambda: (staged, object())
            installer.validate_pre_activation_helper = lambda _staged: {
                "state": "inactive",
                "available_sha256": "b" * 64,
            }

            def move(source_path, target_path, assert_stopped):
                events.append(f"move:{source_path.name}->{target_path.name}")
                assert_stopped()
                os.rename(source_path, target_path)

            def run(args, **_kwargs):
                if args[1:3] == ["plugin", "enable"]:
                    self.assertEqual(
                        args,
                        [
                            shell.omarchy,
                            "plugin",
                            "enable",
                            MODULE.core.PLUGIN_ID,
                            "--section",
                            "left",
                        ],
                    )
                    events.append("enable")
                else:
                    self.fail(f"unexpected command: {args}")
                return subprocess.CompletedProcess(args, 0, b"", b"")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: events.append("absent")),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: events.append("persisted-disabled")),
                mock.patch.object(MODULE, "require_persisted_plugin_enabled", lambda: events.append("persisted-enabled")),
                mock.patch.object(MODULE, "wait_plugin_state", lambda shell, enabled, expected_shell, cursor: events.append(f"state:{enabled}")),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: events.append("validate-plugin")),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
                mock.patch.object(MODULE.core, "run", run),
            ):
                with contextlib.redirect_stdout(io.StringIO()):
                    helper = installer.install()

            self.assertEqual(helper["running_sha256"], "b" * 64)
            self.assertTrue(root.is_dir())
            self.assertFalse(source.exists())
            self.assertEqual(events.count("start"), 1)
            self.assertEqual(events.count("enable"), 1)
            self.assertLess(events.index("stop:True"), events.index(f"move:{source.name}->{root.name}"))
            self.assertLess(events.index("start"), events.index("state:False"))
            self.assertLess(events.index("state:False"), events.index("enable"))
            self.assertLess(events.index("enable"), events.index("consumer-ready"))
            self.assertLess(
                events.index("consumer-ready"), events.index("persisted-enabled")
            )

    def test_raced_target_keeps_shell_stopped_and_is_not_discovered(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())

            def raced_move(_source, target, assert_stopped):
                assert_stopped()
                target.mkdir()
                raise MODULE.InstallError("installation target appeared")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: None),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "move_tree_noreplace", raced_move),
            ):
                with self.assertRaisesRegex(
                    MODULE.InstallError, "shell remains stopped"
                ):
                    installer.install()

            self.assertTrue(source.is_dir())
            self.assertTrue(root.is_dir())
            self.assertTrue(installer.shell_stopped)
            self.assertNotIn("start", events)
            self.assertNotIn("enable", events)

    def test_running_helper_before_enable_retains_installed_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())

            def move(source_path, target_path, assert_stopped):
                assert_stopped()
                os.rename(source_path, target_path)

            def run(args, **_kwargs):
                events.append(args[2])
                return subprocess.CompletedProcess(args, 0, b"", b"")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: None),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "wait_plugin_state", lambda *args, **kwargs: None),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: None),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
                mock.patch.object(
                    MODULE.core,
                    "helper_call",
                    return_value={
                        "state": "current",
                        "available_sha256": "b" * 64,
                        "running_sha256": "b" * 64,
                    },
                ),
                mock.patch.object(MODULE.core, "run", run),
            ):
                with self.assertRaisesRegex(MODULE.InstallError, "was disabled"):
                    installer.install()

            self.assertFalse(source.exists())
            self.assertTrue(root.is_dir())
            self.assertTrue(installer.activation_possible)
            self.assertEqual(events.count("disable"), 1)

    def test_uncertain_helper_status_retains_installed_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())

            def move(source_path, target_path, assert_stopped):
                assert_stopped()
                os.rename(source_path, target_path)

            def run(args, **_kwargs):
                events.append(args[2])
                return subprocess.CompletedProcess(args, 0, b"", b"")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: None),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "wait_plugin_state", lambda *args, **kwargs: None),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: None),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
                mock.patch.object(
                    MODULE.core,
                    "helper_call",
                    side_effect=MODULE.InstallError("incomplete runtime markers"),
                ),
                mock.patch.object(MODULE.core, "run", run),
            ):
                with self.assertRaisesRegex(MODULE.InstallError, "was disabled"):
                    installer.install()

            self.assertFalse(source.exists())
            self.assertTrue(root.is_dir())
            self.assertTrue(installer.activation_possible)
            self.assertEqual(events.count("disable"), 1)

    def test_failure_before_enable_restores_external_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())
            installer.validate_pre_activation_helper = lambda _staged: {}

            def move(source_path, target_path, assert_stopped):
                events.append(f"move:{source_path.name}->{target_path.name}")
                assert_stopped()
                os.rename(source_path, target_path)

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: events.append("absent")),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "wait_plugin_state", side_effect=MODULE.InstallError("not disabled")),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: None),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
            ):
                with self.assertRaisesRegex(
                    MODULE.InstallError, "external checkout was restored"
                ):
                    installer.install()

            self.assertTrue(source.is_dir())
            self.assertFalse(root.exists())
            self.assertEqual(events.count("start"), 2)
            self.assertIn(f"move:{root.name}->{source.name}", events)

    def test_failure_after_enable_disables_but_keeps_installed_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            shell.consumer_ready = mock.Mock(side_effect=MODULE.InstallError("consumer failed"))
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())
            installer.validate_pre_activation_helper = lambda _staged: {}

            def move(source_path, target_path, assert_stopped):
                assert_stopped()
                os.rename(source_path, target_path)

            def run(args, **_kwargs):
                events.append(args[2])
                return subprocess.CompletedProcess(args, 0, b"", b"")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: None),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "wait_plugin_state", lambda shell, enabled, expected_shell, cursor: events.append(f"state:{enabled}")),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: None),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
                mock.patch.object(MODULE.core, "run", run),
            ):
                with self.assertRaisesRegex(MODULE.InstallError, "was disabled"):
                    installer.install()

            self.assertFalse(source.exists())
            self.assertTrue(root.is_dir())
            self.assertEqual(events.count("enable"), 1)
            self.assertEqual(events.count("disable"), 1)
            self.assertIn("state:False", events)

    def test_persisted_disable_mismatch_stops_the_shell(self):
        events: list[str] = []
        shell = FakeShell(events)
        installer = MODULE.Installer.__new__(MODULE.Installer)
        installer.shell = shell
        installer.shell_stopped = False

        with (
            mock.patch.object(
                MODULE.core,
                "run",
                return_value=subprocess.CompletedProcess([], 0, b"", b""),
            ),
            mock.patch.object(MODULE, "wait_plugin_state", lambda *args, **kwargs: None),
            mock.patch.object(
                MODULE,
                "require_persisted_plugin_disabled",
                side_effect=MODULE.InstallError("persisted config still enabled"),
            ),
        ):
            with self.assertRaisesRegex(MODULE.InstallError, "shell was stopped"):
                installer.retain_after_activation_failure(
                    MODULE.InstallError("consumer failed")
                )

        self.assertTrue(installer.shell_stopped)
        self.assertIn("stop:False", events)

    def test_failed_post_enable_disable_stops_the_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            plugins = base / "plugins"
            root = plugins / MODULE.core.PLUGIN_ID
            source.mkdir()
            plugins.mkdir()
            events: list[str] = []
            shell = FakeShell(events)
            shell.consumer_ready = mock.Mock(
                side_effect=MODULE.InstallError("consumer failed")
            )
            staged = MODULE.core.StagedTree(source, "a" * 40, "b" * 64, 14)
            installer = MODULE.Installer.__new__(MODULE.Installer)
            installer.source = source
            installer.root = root
            installer.plugins_dir = plugins
            installer.shell = shell
            installer.moved = False
            installer.shell_stopped = False
            installer.enable_attempted = False
            installer.activation_possible = False
            installer.preflight_and_build = lambda: (staged, object())
            installer.validate_pre_activation_helper = lambda _staged: {}

            def move(source_path, target_path, assert_stopped):
                assert_stopped()
                os.rename(source_path, target_path)

            def run(args, **_kwargs):
                if args[2] == "disable":
                    raise MODULE.InstallError("disable failed")
                events.append(args[2])
                return subprocess.CompletedProcess(args, 0, b"", b"")

            with (
                mock.patch.object(MODULE, "target_absent", lambda path: None),
                mock.patch.object(MODULE, "require_plugin_absent", lambda shell: None),
                mock.patch.object(MODULE, "require_persisted_plugin_disabled", lambda: None),
                mock.patch.object(MODULE, "wait_plugin_state", lambda *args, **kwargs: None),
                mock.patch.object(MODULE, "move_tree_noreplace", move),
                mock.patch.object(MODULE.core, "validate_plugin", lambda root: None),
                mock.patch.object(MODULE.core, "validate_git_checkout", lambda *args, **kwargs: "a" * 40),
                mock.patch.object(MODULE.core, "run", run),
            ):
                with self.assertRaisesRegex(MODULE.InstallError, "shell was stopped"):
                    installer.install()

            self.assertTrue(root.is_dir())
            self.assertTrue(installer.shell_stopped)
            self.assertIn("stop:False", events)


if __name__ == "__main__":
    unittest.main()
