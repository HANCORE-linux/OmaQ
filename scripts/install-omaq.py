#!/usr/bin/python3 -I
"""Build and install a complete OmaQ checkout while the shell is stopped."""

from __future__ import annotations

import argparse
import ctypes
import errno
import hashlib
import importlib.util
import os
from pathlib import Path
import signal
import stat
import sys
import time

sys.dont_write_bytecode = True

CORE_PATH = Path(__file__).absolute().parent / "update-omaq.py"
SPEC = importlib.util.spec_from_file_location("omaq_source_install_core", CORE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load the OmaQ source transaction core")
core = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = core
SPEC.loader.exec_module(core)

RENAME_NOREPLACE = 1


class InstallError(core.UpdateError):
    """A fail-closed first-install error."""


class ActivationPossibleError(InstallError):
    """The installed tree may already have an active consumer."""


def fail(message: str) -> None:
    raise InstallError(message)


def target_absent(path: Path) -> None:
    try:
        path.lstat()
    except FileNotFoundError:
        return
    except OSError as error:
        fail(f"cannot inspect installation target: {error}")
    fail(f"OmaQ installation target already exists: {path}")


def plugin_enabled_in_shell_config(value: object) -> bool:
    if not isinstance(value, dict):
        fail("shell config is not an object")

    bar = value.get("bar", {})
    if not isinstance(bar, dict):
        fail("shell config bar field is not an object")
    if "id" in bar:
        if not isinstance(bar["id"], str):
            fail("shell config bar id is not a string")
        if bar["id"] == core.PLUGIN_ID:
            return True

    layout = bar.get("layout", {})
    if not isinstance(layout, dict):
        fail("shell config bar layout is not an object")
    for section in ("left", "center", "right"):
        entries = layout.get(section, [])
        if not isinstance(entries, list):
            fail(f"shell config bar layout {section} field is not an array")
        for entry in entries:
            if isinstance(entry, str):
                identifier = entry
            elif isinstance(entry, dict) and isinstance(entry.get("id"), str):
                identifier = entry["id"]
            else:
                fail("shell config contains an ambiguous bar entry")
            if identifier == core.PLUGIN_ID:
                return True

    plugins = value.get("plugins", [])
    if not isinstance(plugins, list):
        fail("shell config plugins field is not an array")
    for entry in plugins:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            fail("shell config contains an ambiguous plugin entry")
        if entry["id"] == core.PLUGIN_ID:
            return True
    return False


def persisted_shell_config() -> object | None:
    path = Path.home() / ".config/omarchy/shell.json"
    try:
        fd = os.open(
            path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
        )
    except FileNotFoundError:
        return None
    try:
        info = os.fstat(fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_nlink != 1
            or info.st_mode & 0o022
            or info.st_size > core.MAX_CAPTURE
        ):
            fail("unsafe persisted shell config")
        raw = os.read(fd, core.MAX_CAPTURE + 1)
        if len(raw) != info.st_size:
            fail("persisted shell config changed while reading")
    finally:
        os.close(fd)
    value = core.strict_json(
        core.bounded_text(raw, "persisted shell config"),
        "persisted shell config",
    )
    if (
        not isinstance(value, dict)
        or type(value.get("version")) is not int
        or value.get("version") != 1
    ):
        fail("persisted shell config has an unsupported version")
    return value


def require_persisted_plugin_disabled() -> None:
    config = persisted_shell_config()
    if config is not None and plugin_enabled_in_shell_config(config):
        fail("persisted shell config enables OmaQ before installation")


def require_persisted_plugin_enabled() -> None:
    config = persisted_shell_config()
    if config is None or not plugin_enabled_in_shell_config(config):
        fail("persisted shell config does not enable OmaQ")


def plugin_entries(shell: core.ShellController) -> list[dict]:
    result = core.run(
        [shell.omarchy, "plugin", "list", "--json"],
        capture=True,
        timeout=5,
        env=shell.ipc_env,
    )
    value = core.strict_json(
        core.bounded_text(result.stdout, "plugin list"), "plugin list"
    )
    if not isinstance(value, list) or any(not isinstance(item, dict) for item in value):
        fail("plugin list is not an array of objects")
    return [item for item in value if item.get("id") == core.PLUGIN_ID]


def require_plugin_absent(shell: core.ShellController) -> None:
    if plugin_entries(shell):
        fail("the running shell already knows an OmaQ plugin")
    result = core.run(
        [shell.omarchy, "shell", "shell", "listShellConfig"],
        capture=True,
        timeout=5,
        env=shell.ipc_env,
    )
    config = core.strict_json(
        core.bounded_text(result.stdout, "shell config"), "shell config"
    )
    if plugin_enabled_in_shell_config(config):
        fail("shell config still enables OmaQ while its source tree is absent")


def validate_plugin_entries(entries: list[dict], *, enabled: bool) -> None:
    if len(entries) != 1:
        fail("plugin list does not contain exactly one OmaQ entry")
    entry = entries[0]
    if entry.get("firstParty") is not False:
        fail("OmaQ is not a third-party plugin")
    actual_enabled = entry.get("enabled")
    if actual_enabled is not enabled:
        state = "enabled" if enabled else "disabled"
        if not enabled and actual_enabled is True:
            raise ActivationPossibleError(
                "OmaQ became enabled before the controlled enable operation"
            )
        fail(f"OmaQ is not a {state} third-party plugin")
    kinds = entry.get("kinds")
    if not isinstance(kinds, list) or "bar-widget" not in kinds:
        fail("OmaQ bar-widget kind is unavailable")


def require_plugin_state(shell: core.ShellController, *, enabled: bool) -> None:
    validate_plugin_entries(plugin_entries(shell), enabled=enabled)


def enable_plugin(shell: core.ShellController, section: str) -> None:
    command = [shell.omarchy, "plugin", "enable", core.PLUGIN_ID]
    if section:
        command.extend(["--section", section])
    result = core.run(
        command,
        check=False,
        capture=True,
        timeout=10,
        env=shell.ipc_env,
    )
    if result.returncode == 0:
        return
    stdout = core.bounded_text(result.stdout, "plugin enable stdout").strip()
    stderr = core.bounded_text(result.stderr, "plugin enable stderr").strip()
    if (
        result.returncode == 1
        and result.stdout == b""
        and result.stderr == b"omarchy-shell is not responding\n"
    ):
        # Enabling the plugin rewrites shell.json and can reload the IPC handler
        # before its reply arrives. This result is uncertain, not successful;
        # the caller must still prove every consumer and persisted postcondition.
        return
    detail = stderr or stdout
    suffix = f": {detail}" if detail else ""
    fail(f"plugin enable command failed ({result.returncode}){suffix}")


def wait_plugin_state(
    shell: core.ShellController,
    *,
    enabled: bool,
    expected_shell: core.ShellProcesses,
    cursor: str,
    timeout: float = 20,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        shell.assert_same_shell(expected_shell)
        failed_line = shell.journal_failed(cursor)
        if failed_line:
            fail(f"new shell reported an OmaQ loader failure: {failed_line}")
        entries = plugin_entries(shell)
        if not entries:
            time.sleep(0.1)
            continue
        validate_plugin_entries(entries, enabled=enabled)
        shell.assert_same_shell(expected_shell)
        return
    fail("plugin discovery did not produce an OmaQ entry")


def ignored_records(root: Path) -> list[bytes]:
    result = core.run(
        [
            core.command_path("git"),
            "-c",
            "core.hooksPath=/dev/null",
            "-c",
            "core.fsmonitor=false",
            "-C",
            str(root),
            "status",
            "--porcelain=v1",
            "--ignored=matching",
            "--untracked-files=all",
            "-z",
        ],
        capture=True,
        timeout=30,
        env=core.git_environment(),
    )
    return [record for record in result.stdout.split(b"\0") if record]


def local_git_config(root: Path, arguments: list[str], source: str) -> str:
    result = core.run(
        [
            core.command_path("git"),
            "-c",
            "core.hooksPath=/dev/null",
            "-C",
            str(root),
            "config",
            "--local",
            *arguments,
        ],
        check=False,
        capture=True,
        timeout=30,
        env=core.git_environment(),
    )
    if result.returncode not in {0, 1}:
        fail(f"cannot inspect {source}")
    return core.bounded_text(result.stdout, source).strip()


def validate_checkout_permissions(root: Path) -> None:
    stack = [root]
    while stack:
        current = stack.pop()
        with os.scandir(current) as children:
            for child in children:
                info = child.stat(follow_symlinks=False)
                if info.st_uid != os.geteuid() or info.st_mode & 0o022:
                    fail(f"installer checkout contains an unsafe entry: {child.path}")
                if stat.S_ISDIR(info.st_mode):
                    stack.append(Path(child.path))
                elif stat.S_ISREG(info.st_mode):
                    if info.st_nlink != 1:
                        fail(
                            "installer checkout contains a hard-linked regular file"
                        )
                else:
                    fail("installer checkout contains a symlink or special file")


def parse_tree_records(raw: bytes, source: str) -> dict[bytes, tuple[bytes, bytes]]:
    result: dict[bytes, tuple[bytes, bytes]] = {}
    for record in (value for value in raw.split(b"\0") if value):
        metadata, separator, path = record.partition(b"\t")
        fields = metadata.split()
        if separator != b"\t" or len(fields) != 3 or not path:
            fail(f"{source} contains an ambiguous entry")
        mode, kind, object_id = fields
        try:
            valid_hash = core.HEX_40.fullmatch(
                object_id.decode("ascii", "strict")
            )
        except UnicodeDecodeError:
            valid_hash = None
        if (
            mode not in {b"100644", b"100755"}
            or kind != b"blob"
            or valid_hash is None
            or path.startswith(b"/")
            or any(part in {b"", b".", b".."} for part in path.split(b"/"))
            or path in result
        ):
            fail(
                f"{source} contains a symlink, gitlink, duplicate, or "
                "unsupported tree entry"
            )
        result[path] = (mode, object_id)
    if not result:
        fail(f"{source} is empty")
    return result


def parse_index_records(raw: bytes) -> dict[bytes, tuple[bytes, bytes]]:
    result: dict[bytes, tuple[bytes, bytes]] = {}
    for record in (value for value in raw.split(b"\0") if value):
        metadata, separator, path = record.partition(b"\t")
        fields = metadata.split()
        if separator != b"\t" or len(fields) != 3 or not path:
            fail("Git index contains an ambiguous entry")
        mode, object_id, stage = fields
        if stage != b"0" or path in result:
            fail("Git index contains an unmerged or duplicate entry")
        result[path] = (mode, object_id)
    return result


def validate_index_flags(root: Path, expected_paths: set[bytes]) -> None:
    result = core.run(
        [
            core.command_path("git"),
            "-c",
            "core.hooksPath=/dev/null",
            "-C",
            str(root),
            "ls-files",
            "-v",
            "-z",
            "--cached",
        ],
        capture=True,
        timeout=30,
        env=core.git_environment(),
    )
    paths = set()
    for record in (value for value in result.stdout.split(b"\0") if value):
        if not record.startswith(b"H ") or len(record) <= 2:
            fail("Git index contains assume-unchanged or skip-worktree entries")
        path = record[2:]
        if path in paths:
            fail("Git index contains a duplicate path")
        paths.add(path)
    if paths != expected_paths:
        fail("Git index flag paths do not match the committed tree")


def validate_worktree_blobs(
    root: Path, expected: dict[bytes, tuple[bytes, bytes]]
) -> None:
    root_fd = os.open(
        root, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
    )
    try:
        for path, (mode, object_id) in expected.items():
            fd = os.open(
                os.fsdecode(path),
                os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK,
                dir_fd=root_fd,
            )
            try:
                before = os.fstat(fd)
                if (
                    not stat.S_ISREG(before.st_mode)
                    or before.st_uid != os.geteuid()
                    or before.st_nlink != 1
                    or before.st_mode & 0o7022
                    or before.st_size > core.MAX_TREE_BYTES
                    or bool(before.st_mode & 0o111) != (mode == b"100755")
                ):
                    fail(f"unsafe tracked checkout file: {os.fsdecode(path)}")
                digest = hashlib.sha1(
                    f"blob {before.st_size}\0".encode("ascii"),
                    usedforsecurity=False,
                )
                total = 0
                while True:
                    chunk = os.read(
                        fd,
                        min(1024 * 1024, core.MAX_TREE_BYTES + 1 - total),
                    )
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > core.MAX_TREE_BYTES:
                        fail("tracked checkout file exceeds the tree byte limit")
                    digest.update(chunk)
                after = os.fstat(fd)
                if (
                    total != before.st_size
                    or (after.st_dev, after.st_ino, after.st_size)
                    != (before.st_dev, before.st_ino, before.st_size)
                    or digest.hexdigest().encode("ascii") != object_id
                ):
                    fail(
                        f"tracked checkout file differs from HEAD: "
                        f"{os.fsdecode(path)}"
                    )
            finally:
                os.close(fd)
    finally:
        os.close(root_fd)


def validate_complete_git_checkout(root: Path) -> None:
    top = core.git_output(root, ["rev-parse", "--show-toplevel"], "Git worktree")
    git_dir = core.git_output(root, ["rev-parse", "--absolute-git-dir"], "Git directory")
    common_dir = core.git_output(
        root, ["rev-parse", "--git-common-dir"], "Git common directory"
    )
    if Path(top).resolve(strict=True) != root.resolve(strict=True):
        fail("Git worktree root does not match the installer checkout")
    expected_git_dir = (root / ".git").resolve(strict=True)
    if Path(git_dir).resolve(strict=True) != expected_git_dir:
        fail("Git metadata is outside the installer checkout")
    common_path = Path(common_dir)
    if not common_path.is_absolute():
        common_path = root / common_path
    if (
        common_path.resolve(strict=True) != expected_git_dir
        or os.path.lexists(root / ".git/commondir")
    ):
        fail("Git common metadata is outside the installer checkout")
    if core.git_output(
        root, ["rev-parse", "--show-object-format"], "Git object format"
    ) != "sha1":
        fail("installer checkout uses an unsupported Git object format")
    if core.git_output(
        root, ["rev-parse", "--is-shallow-repository"], "Git shallow state"
    ) != "false":
        fail("installer checkout must not be shallow")

    sparse = local_git_config(
        root,
        ["--bool", "--get", "core.sparseCheckout"],
        "sparse-checkout config",
    )
    if sparse not in {"", "false"}:
        fail("installer checkout must not use sparse checkout")
    if local_git_config(
        root, ["--get", "extensions.partialClone"], "partial-clone config"
    ):
        fail("installer checkout must not be a partial clone")
    if local_git_config(
        root,
        ["--get-regexp", r"^remote\..*\.(promisor|partialclonefilter)$"],
        "promisor config",
    ):
        fail("installer checkout must not use a promisor remote")

    for metadata in (
        root / ".git/objects/info/alternates",
        root / ".git/info/grafts",
    ):
        if os.path.lexists(metadata):
            fail(
                f"installer checkout contains unsupported Git metadata: "
                f"{metadata.name}"
            )

    git = core.command_path("git")
    tree_result = core.run(
        [
            git,
            "-c",
            "core.hooksPath=/dev/null",
            "-C",
            str(root),
            "ls-tree",
            "-r",
            "-z",
            "HEAD",
        ],
        capture=True,
        timeout=30,
        env=core.git_environment(),
    )
    tree = parse_tree_records(tree_result.stdout, "Git tree")
    index_result = core.run(
        [
            git,
            "-c",
            "core.hooksPath=/dev/null",
            "-C",
            str(root),
            "ls-files",
            "--stage",
            "-z",
        ],
        capture=True,
        timeout=30,
        env=core.git_environment(),
    )
    index = parse_index_records(index_result.stdout)
    if index != tree:
        fail("Git index does not exactly match the committed tree")
    validate_index_flags(root, set(tree))
    validate_worktree_blobs(root, tree)

    fsck = core.run(
        [
            git,
            "-c",
            "core.hooksPath=/dev/null",
            "-c",
            "fsck.skipList=/dev/null",
            "-C",
            str(root),
            "fsck",
            "--full",
            "--strict",
            "--no-progress",
            "--no-dangling",
        ],
        check=False,
        capture=True,
        timeout=60,
        env=core.git_environment(),
    )
    if fsck.returncode != 0:
        fail("Git object graph is incomplete or invalid")


def renameat2_noreplace(
    source_dir_fd: int,
    source_name: bytes,
    target_dir_fd: int,
    target_name: bytes,
) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    try:
        renameat2 = libc.renameat2
    except AttributeError:
        fail("renameat2 with RENAME_NOREPLACE is unavailable")
    renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    renameat2.restype = ctypes.c_int
    if renameat2(
        source_dir_fd,
        source_name,
        target_dir_fd,
        target_name,
        RENAME_NOREPLACE,
    ) != 0:
        error = ctypes.get_errno()
        if error in {errno.EEXIST, errno.ENOTEMPTY}:
            fail("installation target appeared before the atomic rename")
        if error == errno.EXDEV:
            fail("staging and installation paths crossed a filesystem boundary")
        if error in {errno.ENOSYS, errno.EINVAL, errno.ENOTSUP}:
            fail("atomic no-replace directory rename is unavailable")
        raise OSError(error, os.strerror(error))


def move_tree_noreplace(
    source: Path,
    target: Path,
    assert_shell_stopped,
    *,
    rename_fn=renameat2_noreplace,
) -> None:
    source = source.absolute()
    target = target.absolute()
    if source == target or source in target.parents or target in source.parents:
        fail("staging and installation paths overlap")
    source_identity = core.directory_identity(source)
    source_parent_identity = core.directory_identity(source.parent)
    target_parent_identity = core.directory_identity(target.parent)
    target_absent(target)
    if source_identity[0] != target_parent_identity[0]:
        fail("staging and installation paths are on different filesystems")
    if core.mount_id_for(source) != core.mount_id_for(target.parent):
        fail("staging and installation paths cross a mount boundary")

    source_fd = os.open(
        source.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
    )
    target_fd = os.open(
        target.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
    )
    try:
        source_parent = os.fstat(source_fd)
        target_parent = os.fstat(target_fd)
        if (source_parent.st_dev, source_parent.st_ino) != source_parent_identity:
            fail("staging parent changed before the atomic rename")
        if (target_parent.st_dev, target_parent.st_ino) != target_parent_identity:
            fail("installation parent changed before the atomic rename")
        assert_shell_stopped()
        rename_fn(
            source_fd,
            os.fsencode(source.name),
            target_fd,
            os.fsencode(target.name),
        )
        os.fsync(source_fd)
        if target_fd != source_fd:
            os.fsync(target_fd)
    finally:
        os.close(target_fd)
        os.close(source_fd)

    if core.directory_identity(target) != source_identity:
        fail("atomic installation rename returned an unexpected directory identity")
    try:
        source.lstat()
    except FileNotFoundError:
        return
    fail("staging path remained after the atomic installation rename")


def preflight_install_support(source: Path, target: Path) -> None:
    target_absent(target)
    if core.directory_identity(source)[0] != core.directory_identity(target.parent)[0]:
        fail("staging and installation paths are on different filesystems")
    if core.mount_id_for(source) != core.mount_id_for(target.parent):
        fail("staging and installation paths cross a mount boundary")

    token = f"install-probe-{os.getpid()}-{core.secrets.token_hex(8)}"
    probe_source = source.parent / f"{token}-source"
    probe_target = source.parent / f"{token}-target"
    try:
        probe_source.mkdir(mode=0o700)
        move_tree_noreplace(probe_source, probe_target, lambda: None)
        probe_target.rmdir()
    finally:
        for path in (probe_source, probe_target):
            try:
                path.rmdir()
            except FileNotFoundError:
                pass
        core.fsync_directory(source.parent)


class Installer:
    def __init__(self, expected_commit: str, section: str):
        self.source = Path(__file__).absolute().parent.parent
        self.root = Path.home() / ".config/omarchy/plugins" / core.PLUGIN_ID
        self.plugins_dir = self.root.parent
        core.lstat_directory(self.plugins_dir)
        core.refuse_monitored_path(
            self.source, self.plugins_dir, "installer checkout"
        )
        core.lstat_directory(self.source)
        core.lstat_directory(self.source.parent)
        target_absent(self.root)
        self.expected_commit = expected_commit
        self.section = section
        self.lock = core.UpdateLock(self.plugins_dir)
        self.shell = core.ShellController(self.plugins_dir)
        self.shell.live_root = self.root
        self.moved = False
        self.shell_stopped = False
        self.enable_attempted = False
        self.activation_possible = False

    def stop_shell_for_transaction(self, *, require_running: bool) -> None:
        self.shell_stopped = True
        try:
            self.shell.stop(require_running=require_running)
        except Exception as stop_error:
            try:
                self.restore_shell_without_move()
            except Exception as restart_error:
                fail(
                    f"shell stop failed ({stop_error}); shell recovery failed "
                    f"({restart_error})"
                )
            raise

    def restore_shell_without_move(self) -> None:
        if not self.shell_stopped:
            return
        try:
            self.shell.start()
        finally:
            self.shell_stopped = False

    def preflight(self) -> tuple[str, int, core.ShellProcesses]:
        if os.geteuid() == 0:
            fail("refusing to install OmaQ as root")
        self.shell.refuse_locked_session()
        initial_shell = self.shell.assert_running()
        target_absent(self.root)
        require_plugin_absent(self.shell)

        core.check_tree_bounds(self.source)
        validate_checkout_permissions(self.source)
        head = core.validate_git_checkout(
            self.source, expected_origin=core.CANONICAL_ORIGIN
        )
        validate_complete_git_checkout(self.source)
        if self.expected_commit and head != self.expected_commit:
            fail(
                f"installer checkout is {head}, not the expected commit "
                f"{self.expected_commit}"
            )
        remote = core.resolve_remote_main(self.source.parent, self.expected_commit)
        if remote != head:
            fail("installer checkout does not match canonical origin/main")
        core.validate_plugin(self.source)
        required = core.parse_required_helper_protocol(self.source / "Service.qml")
        if os.path.lexists(self.source / "helper/omaq") or ignored_records(self.source):
            fail("installer checkout is not a pristine unbuilt clone")

        require_persisted_plugin_disabled()
        self.shell.assert_same_shell(initial_shell)
        preflight_install_support(self.source, self.root)
        return head, required, initial_shell

    def preflight_and_build(self) -> tuple[core.StagedTree, core.ShellProcesses]:
        head, required, initial_shell = self.preflight()
        core.run_tree_bounded(
            [
                core.command_path("make"),
                "--no-print-directory",
                "-C",
                str(self.source),
                "helper",
            ],
            self.source,
            timeout=600,
            env=core.trusted_environment(),
        )
        core.check_tree_bounds(self.source)
        validate_checkout_permissions(self.source)
        core.validate_plugin(self.source)
        if (
            core.validate_git_checkout(
                self.source, expected_origin=core.CANONICAL_ORIGIN
            )
            != head
        ):
            fail("installer checkout changed while building the helper")
        core.validate_build_artifacts(self.source)
        validate_complete_git_checkout(self.source)
        helper_hash = core.hash_regular_executable(self.source / "helper/omaq")
        uninstalled_helper = core.helper_call(self.source, "status")
        if (
            uninstalled_helper.get("state") != "inactive"
            or uninstalled_helper.get("available_sha256") != helper_hash
        ):
            fail("a helper runtime is still present before installation")

        if core.resolve_remote_main(self.source.parent, self.expected_commit) != head:
            fail("canonical origin/main changed while the helper was built")
        target_absent(self.root)
        require_plugin_absent(self.shell)
        require_persisted_plugin_disabled()
        self.shell.assert_same_shell(initial_shell)
        preflight_install_support(self.source, self.root)
        return core.StagedTree(self.source, head, helper_hash, required), initial_shell

    def validate_pre_activation_helper(self, staged: core.StagedTree) -> dict:
        # Until the runtime doctor positively proves inactivity, malformed or
        # incomplete markers may still describe a detached target-path helper.
        self.activation_possible = True
        value = core.helper_call(self.root, "status")
        if value.get("available_sha256") != staged.helper_hash:
            fail("installed helper does not match the staged build hash")
        if value.get("state") != "inactive":
            fail("a helper runtime appeared before controlled enablement")
        self.activation_possible = False
        return value

    def assert_install_boundary(self) -> None:
        self.shell.assert_stopped()
        require_persisted_plugin_disabled()

    def move_into_place(self) -> None:
        staged_identity = core.directory_identity(self.source)
        try:
            move_tree_noreplace(
                self.source, self.root, self.assert_install_boundary
            )
        finally:
            try:
                self.moved = core.directory_identity(self.root) == staged_identity
            except core.UpdateError:
                pass

    def restore_external_checkout(self) -> None:
        staged_identity = core.directory_identity(self.root)
        try:
            move_tree_noreplace(
                self.root, self.source, self.shell.assert_stopped
            )
        finally:
            try:
                if core.directory_identity(self.source) == staged_identity:
                    self.moved = False
            except core.UpdateError:
                pass

    def rollback_before_enable(self, original_error: Exception) -> None:
        rollback_error = None
        try:
            self.stop_shell_for_transaction(require_running=False)
            self.restore_external_checkout()
            core.validate_plugin(self.source)
            if (
                core.validate_git_checkout(
                    self.source, expected_origin=core.CANONICAL_ORIGIN
                )
                != self.staged.commit
            ):
                fail("restored installer checkout has an unexpected commit")
            cursor = self.shell.journal_cursor()
            restored_shell = self.shell.start()
            self.shell_stopped = False
            require_plugin_absent(self.shell)
            self.shell.assert_same_shell(restored_shell)
            if self.shell.journal_failed(cursor):
                fail("shell reported an OmaQ loader failure after install rollback")
        except Exception as error:
            rollback_error = error
        if rollback_error is not None:
            fail(
                f"installation failed ({original_error}); checkout rollback also "
                f"failed ({rollback_error})"
            )
        fail(
            f"installation failed and the external checkout was restored: "
            f"{original_error}"
        )

    def retain_after_activation_failure(self, original_error: Exception) -> None:
        disable_error = None
        try:
            if self.shell_stopped:
                running = self.shell.start()
                self.shell_stopped = False
            else:
                running = self.shell.wait_running()
            core.run(
                [self.shell.omarchy, "plugin", "disable", core.PLUGIN_ID],
                capture=True,
                timeout=10,
                env=self.shell.ipc_env,
            )
            wait_plugin_state(
                self.shell,
                enabled=False,
                expected_shell=running,
                cursor=self.shell.journal_cursor(),
            )
            require_persisted_plugin_disabled()
            self.shell.assert_same_shell(running)
        except Exception as error:
            disable_error = error
        if disable_error is not None:
            stop_error = None
            self.shell_stopped = True
            try:
                self.shell.stop(require_running=False)
                self.shell.assert_stopped()
            except Exception as error:
                stop_error = error
            if stop_error is not None:
                fail(
                    f"installation failed after activation became possible "
                    f"({original_error}); the installed tree remains, automatic "
                    f"disable failed ({disable_error}), and the shell could not "
                    f"be stopped ({stop_error})"
                )
            fail(
                f"installation failed after activation became possible "
                f"({original_error}); the installed tree remains and automatic "
                f"disable failed ({disable_error}); the shell was stopped"
            )
        fail(
            f"installation failed after activation became possible; OmaQ was "
            f"disabled and the installed tree remains for recovery: "
            f"{original_error}"
        )

    def install(self) -> dict:
        self.staged, initial_shell = self.preflight_and_build()
        self.shell.refuse_locked_session()
        target_absent(self.root)
        require_plugin_absent(self.shell)
        require_persisted_plugin_disabled()
        self.shell.assert_same_shell(initial_shell)
        try:
            self.stop_shell_for_transaction(require_running=True)
            self.move_into_place()
            core.validate_plugin(self.root)
            if (
                core.validate_git_checkout(
                    self.root, expected_origin=core.CANONICAL_ORIGIN
                )
                != self.staged.commit
            ):
                fail("installed checkout does not match the staged commit")
            self.validate_pre_activation_helper(self.staged)
            cursor = self.shell.journal_cursor()
            started_shell = self.shell.start()
            self.shell_stopped = False
            try:
                wait_plugin_state(
                    self.shell,
                    enabled=False,
                    expected_shell=started_shell,
                    cursor=cursor,
                )
            except ActivationPossibleError:
                self.activation_possible = True
                raise

            self.enable_attempted = True
            enable_plugin(self.shell, getattr(self, "section", ""))
            helper = self.shell.consumer_ready(
                cursor,
                self.staged.helper_hash,
                started_shell,
                allowed_running={self.staged.helper_hash},
                required_protocol=self.staged.required_protocol,
            )
            require_persisted_plugin_enabled()
            self.shell.assert_same_shell(started_shell)
        except Exception as error:
            if self.enable_attempted or self.activation_possible:
                self.retain_after_activation_failure(error)
            if self.moved:
                self.rollback_before_enable(error)
            if os.path.lexists(self.root):
                fail(
                    f"installation stopped before placement ({error}); an "
                    f"unbound target occupies {self.root}, so the shell remains "
                    f"stopped"
                )
            try:
                self.restore_shell_without_move()
            except Exception as restart_error:
                fail(
                    f"installation stopped before activation ({error}); shell "
                    f"recovery failed ({restart_error})"
                )
            raise

        print(f"source: installed ({self.staged.commit})")
        print(f"available helper: {self.staged.helper_hash}")
        print(f"running helper: {helper['running_sha256']}")
        print(f"installed tree: {self.root}")
        return helper

    def close(self) -> None:
        self.lock.close()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="install-omaq.sh",
        description=(
            "Build an external canonical checkout and atomically install it "
            "while the Omarchy shell is stopped."
        ),
    )
    parser.add_argument(
        "--expect-commit",
        default=None,
        help="require this exact checkout and canonical origin/main commit",
    )
    parser.add_argument(
        "--section",
        choices=("left", "center", "right"),
        default=None,
        help="place OmaQ in this bar section (default: manifest setting)",
    )
    parser.add_argument(
        "--yes", action="store_true", help="confirm the temporary shell stop"
    )
    parser.add_argument(
        "--preflight-only", action="store_true", help=argparse.SUPPRESS
    )
    args = parser.parse_args(argv)
    if args.expect_commit is not None and not core.HEX_40.fullmatch(
        args.expect_commit
    ):
        parser.error("--expect-commit requires a lowercase 40-hex commit")
    if not args.yes:
        parser.error("refusing to stop the shell without confirmation; pass --yes")
    args.expect_commit = args.expect_commit or ""
    args.section = args.section or ""
    return args


def interrupted(_signum, _frame) -> None:
    raise core.InterruptedUpdate("installation interrupted")


def main(argv: list[str] | None = None) -> int:
    os.umask(0o077)
    args = parse_args(sys.argv[1:] if argv is None else argv)
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    installer = None
    try:
        installer = Installer(args.expect_commit, args.section)
        if args.preflight_only:
            head, _required, _initial_shell = installer.preflight()
            print(f"source: preflight ok ({head})")
        else:
            installer.install()
        return 0
    except (core.UpdateError, OSError, ValueError) as error:
        print(f"install-omaq: {error}", file=sys.stderr)
        return 1
    finally:
        if installer is not None:
            installer.close()


if __name__ == "__main__":
    raise SystemExit(main())
