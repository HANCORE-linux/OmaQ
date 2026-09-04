#!/usr/bin/env python3
"""Offline adversarial checks for the shell-off source updater."""

from __future__ import annotations

import contextlib
import fcntl
import hashlib
import http.server
import io
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT / "scripts/update-omaq.py"
SPEC = importlib.util.spec_from_file_location("omaq_source_update", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SourceUpdateTests(unittest.TestCase):
    def test_strict_json_rejects_duplicate_keys(self):
        with self.assertRaisesRegex(MODULE.UpdateError, "duplicate JSON key"):
            MODULE.strict_json('{"state":"current","state":"inactive"}', "fixture")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "manifest.json").write_text(
                '{"id":"hancore.omaq","id":"other"}\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "duplicate JSON key"):
                MODULE.validate_manifest(root)

    def test_captured_command_output_is_bounded(self):
        with self.assertRaisesRegex(MODULE.UpdateError, "oversized output"):
            MODULE.run(
                [sys.executable, "-c", "import sys; sys.stdout.write('x' * 1100000)"],
                capture=True,
                timeout=5,
            )

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
            result = subprocess.run(
                [str(ROOT / "scripts/update-omaq.sh"), "--help"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=environment,
            )
            self.assertEqual(result.returncode, 0)
            self.assertFalse(marker.exists())
            direct = subprocess.run(
                [str(ROOT / "scripts/update-omaq.py"), "--help"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=environment,
            )
            self.assertEqual(direct.returncode, 0)
            self.assertFalse(marker.exists())

    def test_runtime_tool_is_copied_outside_the_replaceable_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "helper-runtime.py"
            destination = base / "runtime"
            destination.mkdir(mode=0o700)
            source.write_text("print('bound')\n", encoding="utf-8")
            source.chmod(0o700)
            copied = MODULE.copy_runtime_tool(source, destination)
            source.unlink()
            self.assertEqual(copied.read_text(encoding="utf-8"), "print('bound')\n")
            self.assertEqual(copied.stat().st_mode & 0o777, 0o700)

    def test_required_protocol_is_one_literal_declaration(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Service.qml"
            path.write_text(
                "QtObject {\n"
                "  readonly property int requiredHelperProtocol: 7\n"
                "  property bool ready: version >= requiredHelperProtocol\n"
                "}\n",
                encoding="utf-8",
            )
            self.assertEqual(MODULE.parse_required_helper_protocol(path), 7)

            path.write_text(
                "QtObject {\n"
                "/* readonly property int requiredHelperProtocol: 7 */\n"
                "property string decoy: \"readonly property int "
                "requiredHelperProtocol: 8\"\n"
                "readonly property int requiredHelperProtocol: base + 1\n"
                "}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "ambiguous"):
                MODULE.parse_required_helper_protocol(path)

            path.write_text(
                "QtObject {\n"
                "// readonly property int requiredHelperProtocol: 6\n"
                "readonly property int requiredHelperProtocol: 7\n"
                "property string decoy: `readonly property int "
                "requiredHelperProtocol: 8`\n"
                "}\n",
                encoding="utf-8",
            )
            self.assertEqual(MODULE.parse_required_helper_protocol(path), 7)

            path.write_text(
                "QtObject {\n"
                "readonly property int requiredHelperProtocol: 7\n"
                "readonly property int requiredHelperProtocol: 8\n"
                "}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "ambiguous"):
                MODULE.parse_required_helper_protocol(path)

            path.write_text(
                "QtObject {\n"
                "property int requiredHelperProtocol: base + 1\n"
                "component Decoy: QtObject {\n"
                "readonly property int requiredHelperProtocol: 7\n"
                "}\n"
                "}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "ambiguous"):
                MODULE.parse_required_helper_protocol(path)

            path.write_text(
                "QtObject {\n"
                "property var braceRegex: /}/\n"
                "readonly property int requiredHelperProtocol: 15\n"
                "component Decoy: QtObject {\n"
                "readonly property int requiredHelperProtocol: 7\n"
                "}\n"
                "}\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "ambiguous"):
                MODULE.parse_required_helper_protocol(path)

    def test_protocol_gate_blocks_newer_qml_before_swap(self):
        MODULE.require_protocol_compatible(7, 14)
        MODULE.require_protocol_compatible(14, 14)
        with self.assertRaisesRegex(MODULE.UpdateError, "requires helper protocol 15"):
            MODULE.require_protocol_compatible(15, 14)
        with self.assertRaises(MODULE.UpdateError):
            MODULE.require_protocol_compatible(True, 14)

    def test_incompatible_protocol_aborts_before_shell_stop(self):
        helper = {
            "state": "current",
            "running_pid": 123,
            "running_protocol": 14,
            "running_sha256": "a" * 64,
            "available_sha256": "a" * 64,
        }

        class FakeShell:
            def __init__(self):
                self.stopped = False

            def refuse_locked_session(self):
                return None

            def stop(self, *, require_running):
                self.stopped = True

        updater = MODULE.Updater.__new__(MODULE.Updater)
        updater.root = Path("/tmp/live")
        updater.update_base = Path("/tmp/update-base")
        updater.expected_commit = ""
        updater.runtime_tool = Path("/tmp/runtime.py")
        updater.shell = FakeShell()
        updater.preflight = lambda: ("a" * 40, helper, object())
        original_resolve = MODULE.resolve_remote_main
        original_stage = MODULE.stage_update
        original_identity = MODULE.directory_identity
        original_validate = MODULE.validate_git_checkout
        original_helper_call = MODULE.helper_call
        MODULE.resolve_remote_main = lambda *args: "b" * 40
        MODULE.stage_update = lambda *args: MODULE.StagedTree(
            Path("/tmp/staged"), "b" * 40, "b" * 64, 15
        )
        MODULE.directory_identity = lambda path: (1, 2)
        MODULE.validate_git_checkout = lambda *args, **kwargs: "a" * 40
        MODULE.helper_call = lambda *args, **kwargs: helper
        try:
            with self.assertRaisesRegex(MODULE.UpdateError, "requires helper protocol 15"):
                updater.update()
        finally:
            MODULE.resolve_remote_main = original_resolve
            MODULE.stage_update = original_stage
            MODULE.directory_identity = original_identity
            MODULE.validate_git_checkout = original_validate
            MODULE.helper_call = original_helper_call
        self.assertFalse(updater.shell.stopped)

    def test_exchange_preflight_failure_aborts_before_shell_stop(self):
        helper = {
            "state": "current",
            "running_pid": 123,
            "running_protocol": 14,
            "running_sha256": "a" * 64,
            "available_sha256": "a" * 64,
        }

        class FakeShell:
            def __init__(self):
                self.stopped = False

            def refuse_locked_session(self):
                return None

            def stop(self, *, require_running):
                self.stopped = True

        updater = MODULE.Updater.__new__(MODULE.Updater)
        updater.root = Path("/tmp/live")
        updater.update_base = Path("/tmp/update-base")
        updater.expected_commit = ""
        updater.runtime_tool = Path("/tmp/runtime.py")
        updater.shell = FakeShell()
        updater.preflight = lambda: ("a" * 40, helper, object())
        original_resolve = MODULE.resolve_remote_main
        original_stage = MODULE.stage_update
        original_identity = MODULE.directory_identity
        original_validate = MODULE.validate_git_checkout
        original_helper_call = MODULE.helper_call
        original_exchange_preflight = MODULE.preflight_exchange_support
        MODULE.resolve_remote_main = lambda *args: "b" * 40
        MODULE.stage_update = lambda *args: MODULE.StagedTree(
            Path("/tmp/staged"), "b" * 40, "b" * 64, 14
        )
        MODULE.directory_identity = lambda path: (1, 2)
        MODULE.validate_git_checkout = lambda *args, **kwargs: "a" * 40
        MODULE.helper_call = lambda *args, **kwargs: helper

        def reject_exchange(*_args):
            raise MODULE.UpdateError("exchange unavailable")

        MODULE.preflight_exchange_support = reject_exchange
        try:
            with self.assertRaisesRegex(MODULE.UpdateError, "exchange unavailable"):
                updater.update()
        finally:
            MODULE.resolve_remote_main = original_resolve
            MODULE.stage_update = original_stage
            MODULE.directory_identity = original_identity
            MODULE.validate_git_checkout = original_validate
            MODULE.helper_call = original_helper_call
            MODULE.preflight_exchange_support = original_exchange_preflight
        self.assertFalse(updater.shell.stopped)

    def test_same_commit_skips_staging_and_shell_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Service.qml").write_text(
                "QtObject {\n"
                "readonly property int requiredHelperProtocol: 7\n"
                "}\n",
                encoding="utf-8",
            )
            running = {
                "state": "update-pending",
                "running_pid": 123,
                "running_protocol": 14,
                "running_sha256": "a" * 64,
                "available_sha256": "b" * 64,
            }
            activated = {
                "state": "activated",
                "running_pid": 124,
                "running_protocol": 14,
                "running_sha256": "b" * 64,
                "available_sha256": "b" * 64,
            }
            expected_shell = object()
            actions = []

            class FakeShell:
                def __init__(self):
                    self.stopped = False
                    self.consumed = False

                def assert_same_shell(self, expected):
                    self.asserted = expected

                def refuse_locked_session(self):
                    return None

                def journal_cursor(self):
                    return "cursor"

                def consumer_ready(self, *args, **kwargs):
                    self.consumed = True
                    return activated

                def stop(self, *, require_running):
                    self.stopped = True

            updater = MODULE.Updater.__new__(MODULE.Updater)
            updater.root = root
            updater.update_base = root.parent
            updater.expected_commit = ""
            updater.runtime_tool = root / "runtime.py"
            updater.shell = FakeShell()
            updater.preflight = lambda: ("a" * 40, running, expected_shell)
            original_resolve = MODULE.resolve_remote_main
            original_stage = MODULE.stage_update
            original_validate = MODULE.validate_git_checkout
            original_helper_call = MODULE.helper_call
            MODULE.resolve_remote_main = lambda *args: "a" * 40
            MODULE.stage_update = lambda *args: self.fail("same commit was staged")
            MODULE.validate_git_checkout = lambda *args, **kwargs: "a" * 40

            def fake_helper_call(_root, action, **_kwargs):
                actions.append(action)
                return running if action == "status" else activated

            MODULE.helper_call = fake_helper_call
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    result = updater.update()
            finally:
                MODULE.resolve_remote_main = original_resolve
                MODULE.stage_update = original_stage
                MODULE.validate_git_checkout = original_validate
                MODULE.helper_call = original_helper_call
            self.assertEqual(result, activated)
            self.assertEqual(actions, ["status", "activate"])
            self.assertIs(updater.shell.asserted, expected_shell)
            self.assertTrue(updater.shell.consumed)
            self.assertFalse(updater.shell.stopped)

    def test_exchange_uses_directory_exchange_and_is_reversible(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            live = parent / "live"
            staging = parent / "staging"
            live.mkdir()
            staging.mkdir()
            (live / "old").write_text("old\n", encoding="utf-8")
            (staging / "new").write_text("new\n", encoding="utf-8")
            live_identity = MODULE.directory_identity(live)
            staging_identity = MODULE.directory_identity(staging)
            checks = []

            MODULE.exchange_trees(staging, live, lambda: checks.append("stopped"))
            self.assertEqual(checks, ["stopped"])
            self.assertEqual(MODULE.directory_identity(live), staging_identity)
            self.assertEqual(MODULE.directory_identity(staging), live_identity)
            self.assertTrue((live / "new").is_file())
            self.assertTrue((staging / "old").is_file())

            MODULE.exchange_trees(staging, live, lambda: None)
            self.assertEqual(MODULE.directory_identity(live), live_identity)
            self.assertEqual(MODULE.directory_identity(staging), staging_identity)
            self.assertTrue((live / "old").is_file())
            self.assertTrue((staging / "new").is_file())

    def test_exchange_command_requires_T_exchange_and_no_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            live = parent / "live"
            staging = parent / "staging"
            live.mkdir()
            staging.mkdir()
            observed = []
            original_run = MODULE.run

            def reject_command(args, **kwargs):
                observed.append(args)
                return subprocess.CompletedProcess(args, 1)

            MODULE.run = reject_command
            try:
                with self.assertRaisesRegex(MODULE.UpdateError, "without a copy fallback"):
                    MODULE.exchange_trees(staging, live, lambda: None)
            finally:
                MODULE.run = original_run
            self.assertEqual(
                observed,
                [[
                    "/usr/bin/mv",
                    "-T",
                    "--exchange",
                    "--no-copy",
                    "--",
                    str(staging),
                    str(live),
                ]],
            )

    def test_restart_between_check_and_swap_aborts_without_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            live = parent / "live"
            staging = parent / "staging"
            live.mkdir()
            staging.mkdir()
            (live / "old").write_text("old\n", encoding="utf-8")
            (staging / "new").write_text("new\n", encoding="utf-8")
            live_identity = MODULE.directory_identity(live)
            staging_identity = MODULE.directory_identity(staging)
            shell_running = False

            def assert_stopped():
                if shell_running:
                    raise MODULE.UpdateError("supervisor restarted")

            # Earlier post-stop check succeeds. The injected supervisor appears
            # before exchange_trees performs its final cooperative check.
            assert_stopped()
            shell_running = True
            with self.assertRaisesRegex(MODULE.UpdateError, "supervisor restarted"):
                MODULE.exchange_trees(staging, live, assert_stopped)

            self.assertEqual(MODULE.directory_identity(live), live_identity)
            self.assertEqual(MODULE.directory_identity(staging), staging_identity)
            self.assertTrue((live / "old").is_file())
            self.assertTrue((staging / "new").is_file())

    def test_runtime_and_state_staging_refuse_the_monitored_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            plugins = Path(directory) / "plugins"
            plugins.mkdir(mode=0o700)
            outside = Path(directory) / "outside"
            outside.mkdir(mode=0o700)
            MODULE.refuse_monitored_path(outside, plugins, "outside")
            with self.assertRaisesRegex(MODULE.UpdateError, "outside the monitored"):
                MODULE.refuse_monitored_path(plugins / "state", plugins, "state")

            previous = os.environ.get("XDG_RUNTIME_DIR")
            os.environ["XDG_RUNTIME_DIR"] = str(plugins)
            try:
                with self.assertRaisesRegex(MODULE.UpdateError, "XDG_RUNTIME_DIR"):
                    MODULE.UpdateLock(plugins)
            finally:
                if previous is None:
                    os.environ.pop("XDG_RUNTIME_DIR", None)
                else:
                    os.environ["XDG_RUNTIME_DIR"] = previous
            self.assertFalse((plugins / "omaq-source-update").exists())

    def test_source_update_lock_serializes_source_and_helper_updaters(self):
        with tempfile.TemporaryDirectory() as directory:
            previous = os.environ.get("XDG_RUNTIME_DIR")
            os.environ["XDG_RUNTIME_DIR"] = directory
            first = None
            helper_fd = -1
            try:
                first = MODULE.UpdateLock()
                with self.assertRaisesRegex(MODULE.UpdateError, "source update"):
                    MODULE.UpdateLock()
                first.close()
                first = None

                helper_dir = Path(directory) / "omaq-helper-update"
                helper_dir.mkdir(exist_ok=True, mode=0o700)
                helper_fd = os.open(helper_dir, os.O_RDONLY | os.O_DIRECTORY)
                fcntl.flock(helper_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaisesRegex(MODULE.UpdateError, "helper update"):
                    MODULE.UpdateLock()
            finally:
                if first is not None:
                    first.close()
                if helper_fd >= 0:
                    os.close(helper_fd)
                if previous is None:
                    os.environ.pop("XDG_RUNTIME_DIR", None)
                else:
                    os.environ["XDG_RUNTIME_DIR"] = previous

    def test_staging_acquisition_is_bounded_while_command_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory) / "tree"
            tree.mkdir()
            previous = MODULE.MAX_TREE_BYTES
            MODULE.MAX_TREE_BYTES = 1024
            try:
                with self.assertRaisesRegex(MODULE.UpdateError, "command failed|exceeds"):
                    MODULE.run_tree_bounded(
                        [
                            sys.executable,
                            "-c",
                            "from pathlib import Path; import sys; "
                            "p=Path('large'); "
                            "exec(\"try:\\n p.write_bytes(b'x' * 4096)\\n"
                            "except OSError:\\n sys.exit(1)\")",
                        ],
                        tree,
                        timeout=5,
                        cwd=tree,
                    )
            finally:
                MODULE.MAX_TREE_BYTES = previous

    def test_staging_timeout_terminates_descendant_writers(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory) / "tree"
            tree.mkdir()
            child = (
                "import time\n"
                "from pathlib import Path\n"
                "p=Path('descendant')\n"
                "while True:\n"
                " with p.open('ab') as f: f.write(b'x')\n"
                " time.sleep(0.01)\n"
            )
            parent = (
                "import os,sys,time\n"
                "if os.fork() == 0:\n"
                f" os.execv(sys.executable, [sys.executable, '-c', {child!r}])\n"
                "time.sleep(10)\n"
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "timed out"):
                MODULE.run_tree_bounded(
                    [sys.executable, "-c", parent], tree, timeout=0.2, cwd=tree
                )
            written = (tree / "descendant").stat().st_size
            time.sleep(0.1)
            self.assertEqual((tree / "descendant").stat().st_size, written)

    def test_update_storage_reserves_space_and_limits_retained_trees(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first"
            first.mkdir()
            (first / "data").write_bytes(b"x" * 70)
            previous_trees = MODULE.MAX_UPDATE_TREES
            previous_total = MODULE.MAX_UPDATE_BYTES
            previous_tree_bytes = MODULE.MAX_TREE_BYTES
            try:
                MODULE.MAX_UPDATE_TREES = 3
                MODULE.MAX_UPDATE_BYTES = 100
                MODULE.MAX_TREE_BYTES = 40
                with self.assertRaisesRegex(MODULE.UpdateError, "retain another"):
                    MODULE.check_update_storage(root)

                MODULE.MAX_UPDATE_BYTES = 1000
                second = root / "second"
                second.mkdir()
                MODULE.MAX_UPDATE_TREES = 2
                with self.assertRaisesRegex(MODULE.UpdateError, "retained-tree"):
                    MODULE.check_update_storage(root)
            finally:
                MODULE.MAX_UPDATE_TREES = previous_trees
                MODULE.MAX_UPDATE_BYTES = previous_total
                MODULE.MAX_TREE_BYTES = previous_tree_bytes

    def test_exchange_capability_is_probed_before_live_exchange(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            live = parent / "live"
            staging = parent / "staging"
            live.mkdir()
            staging.mkdir()
            MODULE.preflight_exchange_support(staging, live)
            self.assertEqual(list(parent.glob("exchange-probe-*")), [])
            with self.assertRaisesRegex(MODULE.UpdateError, "mount boundary"):
                MODULE.preflight_exchange_support(
                    staging,
                    live,
                    mount_resolver=lambda path: 1 if path == staging else 2,
                )
            self.assertEqual(list(parent.glob("exchange-probe-*")), [])

            unsupported = parent / "mv-without-exchange"
            unsupported.write_text("#!/bin/sh\nexit 64\n", encoding="utf-8")
            unsupported.chmod(0o755)
            with self.assertRaisesRegex(MODULE.UpdateError, "exchange is unavailable"):
                MODULE.preflight_exchange_support(
                    staging, live, mv_path=str(unsupported)
                )
            self.assertEqual(list(parent.glob("exchange-probe-*")), [])

    def test_mount_identity_uses_the_deepest_mountpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            nested = base / "mount with space"
            target = nested / "tree"
            target.mkdir(parents=True)
            escaped = str(nested).replace(" ", r"\040")
            mountinfo = (
                "1 0 0:1 / / rw - rootfs rootfs rw\n"
                f"2 1 0:2 / {escaped} rw - tmpfs tmpfs rw\n"
            )
            self.assertEqual(MODULE.mount_id_for(target, mountinfo), 2)

    def test_cross_device_exchange_fails_without_copy(self):
        shared = Path("/dev/shm")
        if not shared.is_dir():
            self.skipTest("/dev/shm is unavailable")
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory(
            dir=shared
        ) as second:
            live = Path(first) / "live"
            staging = Path(second) / "staging"
            live.mkdir()
            staging.mkdir()
            if live.stat().st_dev == staging.stat().st_dev:
                self.skipTest("test paths are on one filesystem")
            (live / "old").write_text("old\n", encoding="utf-8")
            (staging / "new").write_text("new\n", encoding="utf-8")
            with self.assertRaisesRegex(MODULE.UpdateError, "different filesystems"):
                MODULE.preflight_exchange_support(staging, live)
            with self.assertRaisesRegex(MODULE.UpdateError, "different filesystems"):
                MODULE.exchange_trees(staging, live, lambda: None)
            self.assertTrue((live / "old").is_file())
            self.assertTrue((staging / "new").is_file())

    def test_shell_process_binding_includes_supervisor_path_and_parent(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        controller.session_path = Path("/usr/share/omarchy")
        controller.shell_dir = Path("/usr/share/omarchy/shell")
        controller.launcher = Path("/usr/share/omarchy/bin/omarchy-launch-shell").resolve()
        controller.quickshell = Path("/usr/bin/quickshell").resolve()
        controller.plugins_dir = Path.home() / ".config/omarchy/plugins"

        launcher = MODULE.ProcessInfo(
            100,
            10,
            1000,
            ("/bin/bash", "/usr/share/omarchy/bin/omarchy-launch-shell"),
            {"OMARCHY_PATH": "/usr/share/omarchy"},
            "/usr/bin/bash",
        )
        shell = MODULE.ProcessInfo(
            101,
            100,
            1001,
            ("quickshell", "-n", "-p", "/usr/share/omarchy/shell"),
            {"OMARCHY_PATH": "/usr/share/omarchy"},
            str(Path("/usr/bin/quickshell").resolve()),
        )
        wrong_session = MODULE.ProcessInfo(
            102,
            100,
            1002,
            shell.argv,
            {"OMARCHY_PATH": "/tmp/not-the-session"},
            shell.executable,
        )
        self.assertTrue(controller._is_launcher(launcher))
        self.assertTrue(controller._is_shell(shell))
        self.assertFalse(controller._is_shell(wrong_session))
        self.assertEqual(shell.ppid, launcher.pid)

    def test_shell_start_waits_for_delayed_readiness_after_wrapper_failure(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        controller.omarchy = "/usr/bin/omarchy"
        controller.ipc_env = {}
        expected = MODULE.ShellProcesses((), (), ())
        attempts = []
        lock_checks = []
        controller.refuse_locked_session = lambda: lock_checks.append(True)

        def delayed_running():
            attempts.append(True)
            if len(attempts) < 3:
                raise MODULE.UpdateError("shell still loading")
            return expected

        original_run = MODULE.run
        original_sleep = MODULE.time.sleep
        try:
            MODULE.run = lambda *args, **kwargs: subprocess.CompletedProcess(
                args[0],
                1,
                b"",
                b"Omarchy shell did not become ready after restart.\n",
            )
            MODULE.time.sleep = lambda _seconds: None
            controller.assert_running = delayed_running
            self.assertEqual(controller.start(), expected)
        finally:
            MODULE.run = original_run
            MODULE.time.sleep = original_sleep
        self.assertEqual(len(attempts), 3)
        self.assertEqual(lock_checks, [True])

    def test_delayed_shell_start_rechecks_the_session_lock(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        controller.omarchy = "/usr/bin/omarchy"
        controller.ipc_env = {}
        controller.wait_running = lambda: MODULE.ShellProcesses((), (), ())

        def locked_session():
            raise MODULE.UpdateError(
                "refusing to update while the Hyprland session is locked"
            )

        controller.refuse_locked_session = locked_session
        original_run = MODULE.run
        try:
            MODULE.run = lambda *args, **kwargs: subprocess.CompletedProcess(
                args[0],
                1,
                b"",
                b"Omarchy shell did not become ready after restart.\n",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "session is locked"):
                controller.start()
        finally:
            MODULE.run = original_run

    def test_shell_start_rejects_non_readiness_wrapper_failure(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        controller.omarchy = "/usr/bin/omarchy"
        controller.ipc_env = {}
        waited = []
        controller.wait_running = lambda: waited.append(True)
        original_run = MODULE.run
        try:
            MODULE.run = lambda *args, **kwargs: subprocess.CompletedProcess(
                args[0],
                1,
                b"",
                b"Omarchy shell restarted, but the session lock was not re-secured.\n",
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "lock was not re-secured"):
                controller.start()
        finally:
            MODULE.run = original_run
        self.assertEqual(waited, [])

    def test_shell_stop_terminates_a_supervisor_that_reappears(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        controller.quickshell = Path("/usr/bin/quickshell")
        controller.shell_dir = Path("/usr/share/omarchy/shell")
        controller.ipc_env = {}
        first_launcher = MODULE.ProcessInfo(100, 10, 1000, ("launcher",), {}, "/bin/bash")
        first_shell = MODULE.ProcessInfo(
            101, 100, 1001, ("quickshell",), {}, "/usr/bin/quickshell"
        )
        second_launcher = MODULE.ProcessInfo(
            200, 10, 2000, ("launcher",), {}, "/bin/bash"
        )
        second_shell = MODULE.ProcessInfo(
            201, 200, 2001, ("quickshell",), {}, "/usr/bin/quickshell"
        )
        first = MODULE.ShellProcesses((first_launcher,), (first_shell,), ())
        second = MODULE.ShellProcesses((second_launcher,), (second_shell,), ())
        stopped = MODULE.ShellProcesses((), (), ())
        states = iter((first, second, stopped))
        terminated = []
        kills = []
        controller.assert_running = lambda: first
        controller.scan = lambda: next(states)
        controller.ping = lambda: False
        controller._terminate_bound = lambda process: terminated.append(process.pid)
        original_run = MODULE.run
        original_sleep = MODULE.time.sleep
        try:
            MODULE.run = lambda *args, **kwargs: (
                kills.append(args[0])
                or subprocess.CompletedProcess(args[0], 0, b"", b"")
            )
            MODULE.time.sleep = lambda _seconds: None
            controller.stop(require_running=True)
        finally:
            MODULE.run = original_run
            MODULE.time.sleep = original_sleep
        self.assertEqual(terminated, [100, 200])
        self.assertEqual(len(kills), 2)

    def test_stopped_check_rejects_supervisor_backoff_without_quickshell(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        launcher = MODULE.ProcessInfo(
            100,
            10,
            1000,
            ("/bin/bash", "/usr/share/omarchy/bin/omarchy-launch-shell"),
            {"OMARCHY_PATH": "/usr/share/omarchy"},
            "/usr/bin/bash",
        )
        controller.scan = lambda: MODULE.ShellProcesses((launcher,), (), ())
        controller.ping = lambda: False
        with self.assertRaisesRegex(MODULE.UpdateError, "supervisor restarted"):
            controller.assert_stopped()

    def test_restarted_shell_identity_cannot_change_during_acceptance(self):
        controller = MODULE.ShellController.__new__(MODULE.ShellController)
        launcher = MODULE.ProcessInfo(100, 10, 1000, ("launcher",), {}, "/bin/bash")
        shell = MODULE.ProcessInfo(101, 100, 1001, ("quickshell",), {}, "/usr/bin/quickshell")
        watcher = MODULE.ProcessInfo(103, 101, 1003, ("inotifywait",), {}, "/usr/bin/inotifywait")
        expected = MODULE.ShellProcesses((launcher,), (shell,), (watcher,))
        controller.scan = lambda: expected
        controller.assert_same_shell(expected)

        replacement = MODULE.ProcessInfo(102, 100, 1002, shell.argv, {}, shell.executable)
        controller.scan = lambda: MODULE.ShellProcesses(
            (launcher,), (replacement,), (watcher,)
        )
        with self.assertRaisesRegex(MODULE.UpdateError, "changed during consumer"):
            controller.assert_same_shell(expected)

        replacement_watcher = MODULE.ProcessInfo(
            104, 101, 1004, watcher.argv, {}, watcher.executable
        )
        controller.scan = lambda: MODULE.ShellProcesses(
            (launcher,), (shell,), (replacement_watcher,)
        )
        with self.assertRaisesRegex(MODULE.UpdateError, "changed during consumer"):
            controller.assert_same_shell(expected)

    def test_failed_stop_recovers_shell_and_interrupt_is_transactional(self):
        events = []

        class FakeShell:
            def stop(self, *, require_running):
                events.append(("stop", require_running))
                raise MODULE.UpdateError("watcher remained")

            def start(self):
                events.append(("start", True))

        updater = MODULE.Updater.__new__(MODULE.Updater)
        updater.shell = FakeShell()
        updater.shell_stopped = False
        with self.assertRaisesRegex(MODULE.UpdateError, "watcher remained"):
            updater.stop_shell_for_transaction(require_running=True)
        self.assertEqual(events, [("stop", True), ("start", True)])
        self.assertFalse(updater.shell_stopped)
        with self.assertRaises(MODULE.InterruptedUpdate):
            MODULE.interrupted(signal.SIGINT, None)

    def test_tree_rollback_accepts_the_original_pending_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            live = base / "live"
            previous = base / "previous"
            for tree, marker in ((live, b"staged"), (previous, b"available-old")):
                (tree / "helper").mkdir(parents=True)
                (tree / "helper/omaq").write_bytes(marker)
                (tree / "helper/omaq").chmod(0o755)
                (tree / "Service.qml").write_text(
                    "QtObject {\n"
                    "readonly property int requiredHelperProtocol: 7\n"
                    "}\n",
                    encoding="utf-8",
                )
            staged_hash = hashlib.sha256(b"staged").hexdigest()
            running_hash = hashlib.sha256(b"running-old").hexdigest()
            expected_available = hashlib.sha256(b"available-old").hexdigest()

            class FakeShell:
                def __init__(self):
                    self.allowed = None

                def stop(self, *, require_running):
                    self.assert_stopped()

                def assert_stopped(self):
                    return None

                def journal_cursor(self):
                    return "cursor"

                def start(self):
                    return object()

                def consumer_ready(
                    self, cursor, expected_helper, started_shell, **kwargs
                ):
                    self.allowed = kwargs["allowed_running"]
                    self.expected_helper = expected_helper

            updater = MODULE.Updater.__new__(MODULE.Updater)
            updater.root = live
            updater.staged = MODULE.StagedTree(previous, "a" * 40, staged_hash, 7)
            updater.original_helper = {"running_sha256": running_hash}
            updater.runtime_tool = base / "runtime.py"
            updater.shell = FakeShell()
            updater.exchanged = True
            updater.shell_stopped = False
            original_validate = MODULE.validate_plugin
            MODULE.validate_plugin = lambda root: None
            try:
                with self.assertRaisesRegex(MODULE.UpdateError, "previous tree was restored"):
                    updater.rollback_tree(MODULE.UpdateError("consumer failed"))
            finally:
                MODULE.validate_plugin = original_validate
            self.assertEqual(updater.shell.expected_helper, expected_available)
            self.assertEqual(
                updater.shell.allowed,
                {expected_available, staged_hash, running_hash},
            )

    def test_inactive_helper_is_not_a_successful_consumer(self):
        with self.assertRaisesRegex(MODULE.UpdateError, "running Protocol-9"):
            MODULE.validate_helper_status(
                {
                    "state": "inactive",
                    "available_sha256": "a" * 64,
                }
            )
        with self.assertRaisesRegex(MODULE.UpdateError, "running verified helper"):
            MODULE.validate_activation_result(
                {"state": "inactive", "available_sha256": "a" * 64},
                "a" * 64,
                {"b" * 64},
                7,
            )

    def test_consumer_rejects_a_replaced_helper_pid(self):
        helper = {
            "state": "current",
            "available_sha256": "a" * 64,
            "running_sha256": "a" * 64,
            "running_protocol": 14,
            "running_pid": 124,
        }
        with self.assertRaisesRegex(MODULE.UpdateError, "helper process changed"):
            MODULE.validate_consumer_helper(
                helper,
                "a" * 64,
                allowed_running={"a" * 64},
                expected_running_pid=123,
                required_protocol=14,
            )

    def test_activated_helper_must_match_hash_and_qml_protocol(self):
        activation = {
            "state": "activated",
            "available_sha256": "a" * 64,
            "running_sha256": "a" * 64,
            "running_protocol": 14,
            "running_pid": 123,
        }
        self.assertEqual(
            MODULE.validate_activation_result(activation, "a" * 64, {"b" * 64}, 14),
            ("activated", "a" * 64),
        )
        activation["running_protocol"] = 8
        with self.assertRaisesRegex(MODULE.UpdateError, "unsupported running protocol"):
            MODULE.validate_activation_result(
                activation, "a" * 64, {"b" * 64}, 7
            )
        activation["running_protocol"] = 13
        with self.assertRaisesRegex(MODULE.UpdateError, "requires helper protocol 14"):
            MODULE.validate_activation_result(
                activation, "a" * 64, {"b" * 64}, 14
            )
        activation["running_protocol"] = 14
        activation["running_sha256"] = "c" * 64
        with self.assertRaisesRegex(MODULE.UpdateError, "expected helper image"):
            MODULE.validate_activation_result(
                activation, "a" * 64, {"b" * 64}, 14
            )

    def test_network_git_rejects_a_relative_parent(self):
        with self.assertRaisesRegex(MODULE.UpdateError, "must be absolute"):
            with MODULE.git_network_environment(Path("relative")):
                self.fail("relative network home unexpectedly opened")

    def test_network_git_uses_a_private_temporary_home(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            with MODULE.git_network_environment(parent) as environment:
                network_home = Path(environment["HOME"])
                self.assertEqual(network_home.parent, parent)
                self.assertNotEqual(network_home, Path.home())
                self.assertEqual(network_home.stat().st_mode & 0o777, 0o700)
                self.assertEqual(
                    environment["GIT_CEILING_DIRECTORIES"], str(parent)
                )
                self.assertEqual(list(network_home.iterdir()), [])
                self.assertNotIn("GIT_CONFIG_COUNT", environment)
                self.assertNotIn("GIT_CONFIG_KEY_0", environment)
                self.assertNotIn("GIT_CONFIG_VALUE_0", environment)
            self.assertFalse(network_home.exists())

    def test_network_git_rejects_a_modified_temporary_home(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            with self.assertRaisesRegex(
                MODULE.UpdateError, "isolated network home changed"
            ):
                with MODULE.git_network_environment(parent) as environment:
                    Path(environment["HOME"], ".netrc").write_text(
                        "machine example.invalid login user password secret\n",
                        encoding="utf-8",
                    )

    def test_network_git_timeout_terminates_descendants_before_home_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            pid_path = parent / "child.pid"
            program = (
                "import pathlib, subprocess, sys, time; "
                "child = subprocess.Popen([sys.executable, '-c', "
                "'import time; time.sleep(60)']); "
                "pathlib.Path(sys.argv[1]).write_text(str(child.pid)); "
                "time.sleep(60)"
            )
            with self.assertRaisesRegex(MODULE.UpdateError, "command timed out"):
                with MODULE.git_network_environment(parent) as environment:
                    network_home = Path(environment["HOME"])
                    MODULE.run(
                        [sys.executable, "-c", program, str(pid_path)],
                        capture=True,
                        timeout=0.5,
                        env=environment,
                        process_group=True,
                    )
            self.assertFalse(network_home.exists())
            child_pid = int(pid_path.read_text(encoding="utf-8"))
            deadline = time.monotonic() + 5
            while Path(f"/proc/{child_pid}").exists() and time.monotonic() < deadline:
                time.sleep(0.05)
            child_alive = Path(f"/proc/{child_pid}").exists()
            if child_alive:
                os.kill(child_pid, signal.SIGKILL)
            self.assertFalse(child_alive, "network Git descendant survived timeout")

    def test_remote_resolution_does_not_use_the_real_home_netrc(self):
        class AuthenticationHandler(http.server.BaseHTTPRequestHandler):
            authorizations: list[str | None] = []

            def do_GET(self):
                type(self).authorizations.append(self.headers.get("Authorization"))
                self.send_response(401)
                self.send_header("WWW-Authenticate", 'Basic realm="fixture"')
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, _format, *_arguments):
                return

        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            credential_home = parent / "credential-home"
            credential_home.mkdir(mode=0o700)
            netrc = credential_home / ".netrc"
            netrc.write_text(
                "machine 127.0.0.1 login fixture password secret\n",
                encoding="utf-8",
            )
            netrc.chmod(0o600)
            server = http.server.ThreadingHTTPServer(
                ("127.0.0.1", 0), AuthenticationHandler
            )
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            origin = (
                f"http://127.0.0.1:{server.server_address[1]}/repository.git"
            )
            original_origin = MODULE.CANONICAL_ORIGIN
            try:
                control_environment = MODULE.git_environment()
                control_environment["HOME"] = str(credential_home)
                subprocess.run(
                    [
                        "/usr/bin/git",
                        *MODULE.GIT_NETWORK_CONFIG,
                        "ls-remote",
                        "--exit-code",
                        origin,
                        "refs/heads/main",
                    ],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=10,
                    env=control_environment,
                )
                self.assertTrue(
                    any(AuthenticationHandler.authorizations),
                    "positive control did not send .netrc credentials",
                )

                marker = parent / "credential-helper-ran"
                credential_helper = parent / "credential-helper"
                credential_helper.write_text(
                    "#!/bin/sh\ntouch -- " + str(marker) + "\n",
                    encoding="utf-8",
                )
                credential_helper.chmod(0o700)
                subprocess.run(
                    ["/usr/bin/git", "-C", str(parent), "init", "-b", "main"],
                    check=True,
                    stdout=subprocess.DEVNULL,
                )
                subprocess.run(
                    [
                        "/usr/bin/git",
                        "-C",
                        str(parent),
                        "config",
                        "credential.helper",
                        f"!{credential_helper}",
                    ],
                    check=True,
                )
                subprocess.run(
                    [
                        "/usr/bin/git",
                        "-C",
                        str(parent),
                        "config",
                        "url.http://127.0.0.1:9/blocked.insteadOf",
                        origin,
                    ],
                    check=True,
                )

                AuthenticationHandler.authorizations.clear()
                MODULE.CANONICAL_ORIGIN = origin
                with self.assertRaisesRegex(MODULE.UpdateError, "command failed"):
                    MODULE.resolve_remote_main(parent, "")
                self.assertTrue(AuthenticationHandler.authorizations)
                self.assertFalse(marker.exists())
                self.assertTrue(
                    all(
                        authorization is None
                        for authorization in AuthenticationHandler.authorizations
                    )
                )
            finally:
                MODULE.CANONICAL_ORIGIN = original_origin
                server.shutdown()
                thread.join(timeout=5)
                server.server_close()

    def test_git_checkout_requires_main_origin_and_clean_complete_git_dir(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            checkout = base / "checkout"
            source.mkdir()
            subprocess.run(["git", "-C", str(source), "init", "-b", "main"], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run(
                ["git", "-C", str(source), "config", "user.name", "Test"], check=True
            )
            subprocess.run(
                ["git", "-C", str(source), "config", "user.email", "test@example.invalid"],
                check=True,
            )
            (source / "manifest.json").write_text(json.dumps({"id": "hancore.omaq"}) + "\n",
                                                   encoding="utf-8")
            subprocess.run(["git", "-C", str(source), "add", "manifest.json"], check=True)
            subprocess.run(["git", "-C", str(source), "commit", "-m", "fixture"], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run(["git", "clone", "--", str(source), str(checkout)], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            head = MODULE.validate_git_checkout(checkout, expected_origin=str(source))
            self.assertRegex(head, r"^[0-9a-f]{40}$")
            self.assertTrue((checkout / ".git").is_dir())

            poisoned = {
                "GIT_DIR": str(source / ".git"),
                "GIT_WORK_TREE": str(source),
                "GIT_CONFIG_COUNT": "1",
                "GIT_CONFIG_KEY_0": "core.bare",
                "GIT_CONFIG_VALUE_0": "true",
                "GIT_SSL_NO_VERIFY": "1",
            }
            previous = {key: os.environ.get(key) for key in poisoned}
            os.environ.update(poisoned)
            try:
                self.assertEqual(
                    MODULE.validate_git_checkout(checkout, expected_origin=str(source)),
                    head,
                )
            finally:
                for key, value in previous.items():
                    if value is None:
                        os.environ.pop(key, None)
                    else:
                        os.environ[key] = value

            (checkout / "dirty").write_text("dirty\n", encoding="utf-8")
            with self.assertRaisesRegex(MODULE.UpdateError, "local changes"):
                MODULE.validate_git_checkout(checkout, expected_origin=str(source))

    def test_external_stage_binds_commit_git_tree_protocol_and_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            updates = base / "updates"
            source.mkdir()
            updates.mkdir(mode=0o700)
            subprocess.run(["git", "-C", str(source), "init", "-b", "main"], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run(
                ["git", "-C", str(source), "config", "user.name", "Test"], check=True
            )
            subprocess.run(
                ["git", "-C", str(source), "config", "user.email", "test@example.invalid"],
                check=True,
            )
            manifest = {
                "schemaVersion": 1,
                "id": "hancore.omaq",
                "name": "OmaQ",
                "version": "test",
                "kinds": ["bar-widget"],
                "entryPoints": {"barWidget": "Panel.qml"},
            }
            (source / "manifest.json").write_text(json.dumps(manifest) + "\n", encoding="utf-8")
            (source / "Panel.qml").write_text("import QtQuick\nItem {}\n", encoding="utf-8")
            (source / "Service.qml").write_text(
                "QtObject {\n  readonly property int requiredHelperProtocol: 7\n}\n",
                encoding="utf-8",
            )
            (source / ".gitignore").write_text("helper/omaq\n", encoding="utf-8")
            (source / "Makefile").write_text(
                ".PHONY: helper\n"
                "helper:\n"
                "\t@mkdir -p helper\n"
                "\t@printf '#!/bin/sh\\nexit 0\\n' >helper/omaq\n"
                "\t@chmod 755 helper/omaq\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "-C", str(source), "add", "."], check=True)
            subprocess.run(["git", "-C", str(source), "commit", "-m", "fixture"], check=True,
                           stdout=subprocess.DEVNULL)
            head = subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
            ).strip()
            original_origin = MODULE.CANONICAL_ORIGIN
            original_network = MODULE.GIT_NETWORK_CONFIG
            MODULE.CANONICAL_ORIGIN = str(source)
            MODULE.GIT_NETWORK_CONFIG = (
                "-c",
                "core.hooksPath=/dev/null",
                "-c",
                "protocol.file.allow=always",
            )
            try:
                staged = MODULE.stage_update(updates, head, head)
            finally:
                MODULE.CANONICAL_ORIGIN = original_origin
                MODULE.GIT_NETWORK_CONFIG = original_network
            self.assertEqual(staged.commit, head)
            self.assertEqual(staged.required_protocol, 7)
            self.assertRegex(staged.helper_hash, r"^[0-9a-f]{64}$")
            self.assertTrue((staged.path / ".git").is_dir())
            self.assertEqual(
                subprocess.check_output(
                    ["git", "-C", str(staged.path), "remote", "get-url", "origin"],
                    text=True,
                ).strip(),
                str(source),
            )

    def test_staged_build_allows_only_the_ignored_helper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "-C", str(root), "init", "-b", "main"], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run(
                ["git", "-C", str(root), "config", "user.name", "Test"], check=True
            )
            subprocess.run(
                ["git", "-C", str(root), "config", "user.email", "test@example.invalid"],
                check=True,
            )
            (root / ".gitignore").write_text("helper/omaq\n*.tmp\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", ".gitignore"], check=True)
            subprocess.run(["git", "-C", str(root), "commit", "-m", "fixture"], check=True,
                           stdout=subprocess.DEVNULL)
            (root / "helper").mkdir()
            (root / "helper/omaq").write_bytes(b"helper")
            MODULE.validate_build_artifacts(root)
            (root / "unexpected.tmp").write_text("unexpected\n", encoding="utf-8")
            with self.assertRaisesRegex(MODULE.UpdateError, "unexpected"):
                MODULE.validate_build_artifacts(root)

    def test_tree_entry_limit_stops_consuming_the_iterator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            regular = root / "regular"
            regular.write_bytes(b"")
            regular_info = regular.stat()

            class Entry:
                path = str(regular)

                def stat(self, *, follow_symlinks):
                    if follow_symlinks:
                        raise AssertionError("scanner followed a fixture entry")
                    return regular_info

            class BoundedIterator:
                def __init__(self):
                    self.calls = 0

                def __enter__(self):
                    return self

                def __exit__(self, *_arguments):
                    return False

                def __iter__(self):
                    return self

                def __next__(self):
                    self.calls += 1
                    if self.calls > MODULE.MAX_TREE_ENTRIES + 1:
                        raise AssertionError("scanner consumed beyond the entry limit")
                    return Entry()

            iterator = BoundedIterator()
            with mock.patch.object(MODULE.os, "scandir", return_value=iterator):
                with self.assertRaisesRegex(MODULE.UpdateError, "50000-entry limit"):
                    MODULE.check_tree_bounds(root)
            self.assertEqual(iterator.calls, MODULE.MAX_TREE_ENTRIES + 1)

    def test_tree_boundaries_reject_symlinks_and_special_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "regular").write_text("ok\n", encoding="utf-8")
            MODULE.check_tree_bounds(root)
            (root / "linked").symlink_to(root / "regular")
            with self.assertRaisesRegex(MODULE.UpdateError, "symlink"):
                MODULE.check_tree_bounds(root)
            (root / "linked").unlink()
            os.mkfifo(root / "fifo")
            with self.assertRaisesRegex(MODULE.UpdateError, "special file"):
                MODULE.check_tree_bounds(root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
