#!/usr/bin/python3 -I
"""Update a live OmaQ checkout without writing while the shell watches it."""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import secrets
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import time
from typing import Callable


PLUGIN_ID = "hancore.omaq"
CANONICAL_ORIGIN = "https://github.com/HANCORE-linux/OmaQ.git"
MAX_CAPTURE = 1024 * 1024
MAX_TREE_BYTES = 512 * 1024 * 1024
MAX_TREE_ENTRIES = 50000
MAX_UPDATE_BYTES = 2 * 1024 * 1024 * 1024
MAX_UPDATE_TREES = 8
MAX_PROC_BYTES = 256 * 1024
GIT_NETWORK_CONFIG = (
    "-c",
    "core.hooksPath=/dev/null",
    "-c",
    "http.sslVerify=true",
    "-c",
    "http.followRedirects=false",
    "-c",
    "protocol.file.allow=never",
)
EXPECTED_HELPER_PROTOCOL_MIN = 9
HEX_40 = re.compile(r"^[0-9a-f]{40}$")
REQUIRED_PROTOCOL_DECLARATION = re.compile(
    r"^[ \t]*(?:readonly[ \t]+)?property[ \t]+int[ \t]+"
    r"requiredHelperProtocol[ \t]*:",
    re.MULTILINE,
)
REQUIRED_PROTOCOL_LITERAL = re.compile(
    r"[ \t]*readonly[ \t]+property[ \t]+int[ \t]+"
    r"requiredHelperProtocol[ \t]*:[ \t]*([0-9]+)[ \t]*"
)


class UpdateError(RuntimeError):
    """A fail-closed update error."""


class InterruptedUpdate(UpdateError):
    """The process received a termination request."""


def fail(message: str) -> None:
    raise UpdateError(message)


def strict_json(raw: str, source: str):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                fail(f"duplicate JSON key in {source}: {key}")
            result[key] = value
        return result

    try:
        return json.loads(raw, object_pairs_hook=pairs)
    except (json.JSONDecodeError, UnicodeError) as error:
        fail(f"invalid JSON from {source}: {error}")


def bounded_text(data: bytes, source: str, maximum: int = MAX_CAPTURE) -> str:
    if len(data) > maximum:
        fail(f"oversized output from {source}")
    try:
        return data.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        fail(f"non-UTF-8 output from {source}: {error}")


def command_path(name: str) -> str:
    path = Path("/usr/bin") / name
    try:
        info = path.stat()
    except OSError:
        fail(f"required trusted command is unavailable: {path}")
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_uid != 0
        or info.st_mode & 0o022
        or not os.access(path, os.X_OK)
    ):
        fail(f"unsafe trusted command path: {path}")
    return str(path)


def trusted_environment() -> dict[str, str]:
    allowed = {
        "DBUS_SESSION_BUS_ADDRESS",
        "DISPLAY",
        "HOME",
        "HYPRLAND_INSTANCE_SIGNATURE",
        "LANG",
        "LC_ALL",
        "LC_CTYPE",
        "LOGNAME",
        "OMARCHY_PATH",
        "USER",
        "WAYLAND_DISPLAY",
        "XDG_RUNTIME_DIR",
    }
    environment = {
        key: value for key, value in os.environ.items() if key in allowed
    }
    environment["HOME"] = str(Path.home())
    environment["PATH"] = "/usr/bin:/bin"
    return environment


def validate_secure_parent_chain(path: Path) -> None:
    current = path
    while True:
        info = current.stat()
        if (
            not stat.S_ISDIR(info.st_mode)
            or info.st_uid not in {0, os.geteuid()}
            or info.st_mode & 0o022
        ):
            fail(f"unsafe executable parent directory: {current}")
        if current.parent == current:
            return
        current = current.parent


def validate_github_cli_path(candidate: Path) -> str:
    try:
        validate_secure_parent_chain(candidate.absolute().parent)
        resolved = candidate.resolve(strict=True)
        validate_secure_parent_chain(resolved.parent)
        info = resolved.stat()
    except FileNotFoundError:
        raise
    except OSError as error:
        fail(f"cannot inspect GitHub CLI candidate {candidate}: {error}")
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_uid not in {0, os.geteuid()}
        or info.st_mode & 0o022
        or not info.st_mode & 0o111
        or info.st_size > 128 * 1024 * 1024
    ):
        fail(f"unsafe GitHub CLI path: {resolved}")
    return str(resolved)


def trusted_github_cli_path() -> str | None:
    candidates = [Path("/usr/bin/gh"), Path.home() / ".local/bin/gh"]
    candidates.extend(
        (Path.home() / ".local/share/mise/installs/gh/latest").glob("*/bin/gh")
    )
    for candidate in candidates:
        try:
            return validate_github_cli_path(candidate)
        except FileNotFoundError:
            continue
    return None


def github_auth_header() -> str | None:
    github_cli = trusted_github_cli_path()
    if github_cli is None:
        return None
    cli_fd = os.open(
        github_cli, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    )
    try:
        info = os.fstat(cli_fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid not in {0, os.geteuid()}
            or info.st_mode & 0o022
            or not info.st_mode & 0o111
            or info.st_size > 128 * 1024 * 1024
        ):
            fail("GitHub CLI changed before token acquisition")
        executable = f"/proc/self/fd/{cli_fd}"
        result = run(
            [executable, "auth", "token", "--hostname", "github.com"],
            check=False,
            capture=True,
            timeout=10,
            env=trusted_environment(),
            pass_fds=(cli_fd,),
        )
    finally:
        os.close(cli_fd)
    if result.returncode != 0:
        return None
    token = bounded_text(result.stdout, "gh auth token").strip()
    if (
        len(token) < 20
        or len(token) > 512
        or any(ord(char) < 0x21 or ord(char) > 0x7E for char in token)
    ):
        fail("GitHub CLI returned an invalid authentication token")
    credentials = base64.b64encode(
        f"x-access-token:{token}".encode("ascii")
    ).decode("ascii")
    return f"Authorization: Basic {credentials}"


def run(
    args: list[str],
    *,
    check: bool = True,
    capture: bool = False,
    timeout: float | None = None,
    env: dict[str, str] | None = None,
    quiet: bool = False,
    cwd: Path | None = None,
    preexec_fn=None,
    pass_fds: tuple[int, ...] = (),
) -> subprocess.CompletedProcess:
    stdout = subprocess.PIPE if capture else (subprocess.DEVNULL if quiet else None)
    stderr = subprocess.PIPE if capture else (subprocess.DEVNULL if quiet else None)
    if capture:
        try:
            process = subprocess.Popen(
                args,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                env=env,
                cwd=cwd,
                preexec_fn=preexec_fn,
                pass_fds=pass_fds,
            )
        except OSError as error:
            fail(f"command failed to start: {args[0]}: {error}")
        output = bytearray()
        errors = bytearray()
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ, output)
        selector.register(process.stderr, selectors.EVENT_READ, errors)
        deadline = None if timeout is None else time.monotonic() + timeout
        try:
            while selector.get_map():
                remaining = None if deadline is None else deadline - time.monotonic()
                if remaining is not None and remaining <= 0:
                    fail(f"command timed out: {args[0]}")
                events = selector.select(remaining)
                if not events:
                    fail(f"command timed out: {args[0]}")
                for key, _mask in events:
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        key.fileobj.close()
                        continue
                    key.data.extend(chunk)
                    if len(output) + len(errors) > MAX_CAPTURE:
                        fail(f"oversized output from {args[0]}")
            remaining = None if deadline is None else max(0.0, deadline - time.monotonic())
            returncode = process.wait(timeout=remaining)
        except BaseException:
            process.kill()
            process.wait()
            raise
        finally:
            selector.close()
            if process.stdout is not None and not process.stdout.closed:
                process.stdout.close()
            if process.stderr is not None and not process.stderr.closed:
                process.stderr.close()
        result = subprocess.CompletedProcess(args, returncode, bytes(output), bytes(errors))
    else:
        try:
            result = subprocess.run(
                args,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                check=False,
                timeout=timeout,
                env=env,
                cwd=cwd,
                preexec_fn=preexec_fn,
                pass_fds=pass_fds,
            )
        except subprocess.TimeoutExpired:
            fail(f"command timed out: {args[0]}")
        except OSError as error:
            fail(f"command failed to start: {args[0]}: {error}")
    if capture:
        bounded_text(result.stdout, args[0])
        bounded_text(result.stderr, args[0])
    if check and result.returncode != 0:
        detail = ""
        if capture:
            detail = bounded_text(result.stderr, args[0]).strip()
        suffix = f": {detail}" if detail else ""
        fail(f"command failed ({result.returncode}): {args[0]}{suffix}")
    return result


def capture_line(args: list[str], source: str, *, env=None) -> str:
    result = run(args, capture=True, timeout=30, env=env)
    output = bounded_text(result.stdout, source).strip()
    if "\n" in output:
        fail(f"unexpected multiline output from {source}")
    return output


def lstat_directory(path: Path, *, private: bool = False) -> os.stat_result:
    try:
        info = path.lstat()
    except FileNotFoundError:
        fail(f"directory is unavailable: {path}")
    disallowed = 0o077 if private else 0o022
    if (
        not stat.S_ISDIR(info.st_mode)
        or info.st_uid != os.geteuid()
        or info.st_mode & disallowed
    ):
        fail(f"unsafe directory: {path}")
    return info


def directory_identity(path: Path) -> tuple[int, int]:
    info = lstat_directory(path)
    return info.st_dev, info.st_ino


def refuse_monitored_path(path: Path, plugins_dir: Path, label: str) -> None:
    try:
        resolved = path.resolve(strict=False)
        monitored = plugins_dir.resolve(strict=True)
    except OSError as error:
        fail(f"cannot resolve {label}: {error}")
    if resolved == monitored or monitored in resolved.parents:
        fail(f"{label} must be outside the monitored plugin directory")


def fsync_directory(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def helper_root_identity(root: Path) -> str:
    root_info = lstat_directory(root)
    helper_info = lstat_directory(root / "helper")
    return ":".join(
        str(value)
        for value in (
            root_info.st_dev,
            root_info.st_ino,
            helper_info.st_dev,
            helper_info.st_ino,
        )
    )


def copy_runtime_tool(source: Path, destination_dir: Path) -> Path:
    source_fd = os.open(
        source, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    )
    target = destination_dir / f"helper-runtime.{os.getpid()}.{secrets.token_hex(8)}.py"
    target_fd = -1
    try:
        info = os.fstat(source_fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_nlink != 1
            or info.st_mode & 0o022
            or info.st_size > 2 * 1024 * 1024
        ):
            fail("unsafe controller helper-runtime.py")
        target_fd = os.open(
            target,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o700,
        )
        copied = 0
        while True:
            chunk = os.read(source_fd, min(1024 * 1024, 2 * 1024 * 1024 + 1 - copied))
            if not chunk:
                break
            copied += len(chunk)
            if copied > 2 * 1024 * 1024:
                fail("controller helper-runtime.py exceeds 2 MiB")
            offset = 0
            while offset < len(chunk):
                written = os.write(target_fd, chunk[offset:])
                if written <= 0:
                    fail("short write while binding helper-runtime.py")
                offset += written
        os.fchmod(target_fd, 0o700)
        os.fsync(target_fd)
    except BaseException:
        try:
            target.unlink()
        except FileNotFoundError:
            pass
        raise
    finally:
        if target_fd >= 0:
            os.close(target_fd)
        os.close(source_fd)
    fsync_directory(destination_dir)
    return target


def hash_regular_executable(path: Path) -> str:
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_nlink != 1
            or info.st_mode & 0o022
            or not info.st_mode & 0o100
            or info.st_size > 16 * 1024 * 1024
        ):
            fail(f"unsafe helper executable: {path}")
        digest = hashlib.sha256()
        total = 0
        while True:
            chunk = os.read(fd, min(1024 * 1024, 16 * 1024 * 1024 + 1 - total))
            if not chunk:
                break
            total += len(chunk)
            if total > 16 * 1024 * 1024:
                fail(f"oversized helper executable: {path}")
            digest.update(chunk)
        return digest.hexdigest()
    finally:
        os.close(fd)


def strip_qml_comments_and_strings(text: str) -> str:
    output = []
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and next_char == "/":
                output.extend("  ")
                index += 2
                state = "line-comment"
                continue
            if char == "/" and next_char == "*":
                output.extend("  ")
                index += 2
                state = "block-comment"
                continue
            if char in {"\"", "'", "`"}:
                output.append(" ")
                quote = char
                state = "string"
                index += 1
                continue
            output.append(char)
            index += 1
            continue
        if state == "line-comment":
            if char == "\n":
                output.append("\n")
                state = "code"
            else:
                output.append(" ")
            index += 1
            continue
        if state == "block-comment":
            if char == "*" and next_char == "/":
                output.extend("  ")
                index += 2
                state = "code"
            else:
                output.append("\n" if char == "\n" else " ")
                index += 1
            continue
        if char == "\\":
            output.append(" ")
            index += 1
            if index < len(text):
                output.append("\n" if text[index] == "\n" else " ")
                index += 1
            continue
        if char == quote:
            output.append(" ")
            index += 1
            state = "code"
            quote = ""
            continue
        if char == "\n" and quote != "`":
            fail("staged Service.qml contains an unterminated string")
        output.append("\n" if char == "\n" else " ")
        index += 1
    if state in {"block-comment", "string"}:
        fail("staged Service.qml contains an unterminated comment or string")
    return "".join(output)


def parse_required_helper_protocol(service_path: Path) -> int:
    try:
        raw = service_path.read_bytes()
    except OSError as error:
        fail(f"cannot read staged Service.qml: {error}")
    if len(raw) > 2 * 1024 * 1024:
        fail("staged Service.qml exceeds 2 MiB")
    try:
        text = raw.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        fail(f"staged Service.qml is not UTF-8: {error}")
    stripped = strip_qml_comments_and_strings(text)
    declaration_matches = list(
        REQUIRED_PROTOCOL_DECLARATION.finditer(stripped)
    )
    if not declaration_matches:
        fail("staged Service.qml has an ambiguous requiredHelperProtocol contract")
    # Service keeps this root contract before JavaScript expressions. Reject
    # unclassified slash tokens before any declaration rather than confusing a
    # regular-expression brace with QML object depth.
    if "/" in stripped[: declaration_matches[-1].start()]:
        fail("staged Service.qml has an ambiguous requiredHelperProtocol contract")
    declarations = []
    depth = 0
    position = 0
    for match in declaration_matches:
        for char in stripped[position : match.start()]:
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
        position = match.start()
        if depth == 1:
            declarations.append(match)
    if len(declarations) != 1:
        fail("staged Service.qml has an ambiguous requiredHelperProtocol contract")
    declaration = declarations[0]
    line_end = stripped.find("\n", declaration.start())
    if line_end < 0:
        line_end = len(stripped)
    literal = REQUIRED_PROTOCOL_LITERAL.fullmatch(
        stripped[declaration.start() : line_end]
    )
    if literal is None:
        # A computed declaration needs a new fail-closed parser.
        fail("staged Service.qml has an ambiguous requiredHelperProtocol contract")
    value = int(literal.group(1))
    if value < 1 or value > 1024:
        fail("staged requiredHelperProtocol is outside the supported range")
    return value


def check_tree_bounds(root: Path, *, allow_disappeared: bool = False) -> None:
    lstat_directory(root)
    entries = 0
    total = 0
    stack = [root]
    while stack:
        current = stack.pop()
        try:
            with os.scandir(current) as iterator:
                children = list(iterator)
        except FileNotFoundError as error:
            if allow_disappeared:
                continue
            fail(f"cannot inspect staged tree: {error}")
        except OSError as error:
            fail(f"cannot inspect staged tree: {error}")
        for child in children:
            entries += 1
            if entries > MAX_TREE_ENTRIES:
                fail("staged checkout exceeds the 50000-entry limit")
            try:
                info = child.stat(follow_symlinks=False)
            except FileNotFoundError as error:
                if allow_disappeared:
                    continue
                fail(f"cannot inspect staged entry: {error}")
            except OSError as error:
                fail(f"cannot inspect staged entry: {error}")
            if stat.S_ISLNK(info.st_mode):
                fail(f"staged checkout contains a symlink: {child.path}")
            if stat.S_ISDIR(info.st_mode):
                stack.append(Path(child.path))
            elif stat.S_ISREG(info.st_mode):
                total += info.st_size
                if total > MAX_TREE_BYTES:
                    fail("staged checkout exceeds the 512 MiB limit")
            else:
                fail(f"staged checkout contains a special file: {child.path}")


def check_update_storage(root: Path) -> None:
    lstat_directory(root, private=True)
    trees = 0
    entries = 0
    total = 0
    stack = []
    with os.scandir(root) as iterator:
        for child in iterator:
            info = child.stat(follow_symlinks=False)
            if not stat.S_ISDIR(info.st_mode):
                fail(f"update storage contains an unexpected entry: {child.path}")
            trees += 1
            if trees >= MAX_UPDATE_TREES:
                fail("update storage reached its retained-tree limit")
            stack.append(Path(child.path))
    while stack:
        current = stack.pop()
        with os.scandir(current) as iterator:
            for child in iterator:
                entries += 1
                if entries > MAX_TREE_ENTRIES * MAX_UPDATE_TREES:
                    fail("update storage exceeds its aggregate entry limit")
                info = child.stat(follow_symlinks=False)
                if stat.S_ISLNK(info.st_mode):
                    fail(f"update storage contains a symlink: {child.path}")
                if stat.S_ISDIR(info.st_mode):
                    stack.append(Path(child.path))
                elif stat.S_ISREG(info.st_mode):
                    total += info.st_size
                    if total > MAX_UPDATE_BYTES - MAX_TREE_BYTES:
                        fail("update storage cannot safely retain another staged tree")
                else:
                    fail(f"update storage contains a special file: {child.path}")
    if shutil.disk_usage(root).free < MAX_TREE_BYTES * 2:
        fail("update storage has less than 1 GiB free")


def limit_staging_file_size() -> None:
    resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_TREE_BYTES, MAX_TREE_BYTES))


def terminate_process_group(process: subprocess.Popen) -> None:
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def process_group_exists(process: subprocess.Popen) -> bool:
    try:
        os.killpg(process.pid, 0)
    except ProcessLookupError:
        return False
    return True


def run_tree_bounded(
    args: list[str],
    tree: Path,
    *,
    timeout: float,
    env: dict[str, str] | None = None,
    cwd: Path | None = None,
) -> None:
    try:
        process = subprocess.Popen(
            args,
            stdin=subprocess.DEVNULL,
            env=env,
            cwd=cwd,
            preexec_fn=limit_staging_file_size,
            start_new_session=True,
        )
    except OSError as error:
        fail(f"command failed to start: {args[0]}: {error}")
    deadline = time.monotonic() + timeout
    try:
        while True:
            if tree.exists():
                check_tree_bounds(tree, allow_disappeared=True)
            returncode = process.poll()
            if returncode is not None:
                break
            if time.monotonic() >= deadline:
                fail(f"command timed out: {args[0]}")
            time.sleep(0.05)
    except BaseException:
        terminate_process_group(process)
        raise
    if returncode != 0:
        terminate_process_group(process)
        fail(f"command failed ({returncode}): {args[0]}")
    if process_group_exists(process):
        terminate_process_group(process)
        fail(f"command left a child process running: {args[0]}")


@dataclass(frozen=True)
class ProcessInfo:
    pid: int
    ppid: int
    start: int
    argv: tuple[str, ...]
    environment: dict[str, str]
    executable: str


def process_start_and_parent(raw: str) -> tuple[int, int]:
    close = raw.rfind(")")
    fields = raw[close + 2 :].split()
    if close < 0 or len(fields) <= 19:
        fail("malformed process stat")
    return int(fields[1]), int(fields[19])


def read_proc_file(path: Path, maximum: int) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    chunks = []
    total = 0
    try:
        while True:
            chunk = os.read(fd, min(65536, maximum + 1 - total))
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
            if total > maximum:
                fail(f"oversized process metadata: {path}")
    finally:
        os.close(fd)
    return b"".join(chunks)


def decode_mountinfo_path(value: str) -> Path:
    def replace(match: re.Match) -> str:
        return chr(int(match.group(1), 8))

    if re.search(r"\\(?![0-7]{3})", value):
        fail("mountinfo contains an unsupported mount escape")
    decoded = re.sub(r"\\([0-7]{3})", replace, value)
    if not decoded.startswith("/"):
        fail("mountinfo contains an unsupported mount path")
    return Path(decoded)


def mount_id_for(path: Path, mountinfo: str | None = None) -> int:
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        fail(f"cannot resolve mount identity for {path}: {error}")
    if mountinfo is None:
        raw = read_proc_file(Path("/proc/self/mountinfo"), 2 * 1024 * 1024)
        try:
            mountinfo = raw.decode("utf-8", "strict")
        except UnicodeDecodeError as error:
            fail(f"mountinfo is not UTF-8: {error}")
    matches = []
    for line in mountinfo.splitlines():
        fields = line.split()
        if len(fields) < 6 or not fields[0].isdecimal():
            fail("mountinfo contains a malformed record")
        mountpoint = decode_mountinfo_path(fields[4])
        if resolved == mountpoint or mountpoint in resolved.parents:
            matches.append((len(mountpoint.parts), int(fields[0])))
    if not matches:
        fail(f"cannot identify the mount for {path}")
    depth = max(item[0] for item in matches)
    identifiers = {identifier for item_depth, identifier in matches if item_depth == depth}
    if len(identifiers) != 1:
        fail(f"mount identity is ambiguous for {path}")
    return identifiers.pop()


class ProcessTable:
    def __init__(self, proc_root: Path = Path("/proc")):
        self.proc_root = proc_root

    def get(self, pid: int) -> ProcessInfo | None:
        base = self.proc_root / str(pid)
        try:
            status = read_proc_file(base / "status", MAX_PROC_BYTES).decode("ascii", "strict")
            uid_line = next(line for line in status.splitlines() if line.startswith("Uid:"))
            if int(uid_line.split()[1]) != os.geteuid():
                return None
            stat_raw = read_proc_file(base / "stat", MAX_PROC_BYTES).decode("ascii", "strict")
            ppid, start = process_start_and_parent(stat_raw)
            cmdline = read_proc_file(base / "cmdline", MAX_PROC_BYTES)
            argv = tuple(
                value.decode("utf-8", "surrogateescape")
                for value in cmdline.rstrip(b"\0").split(b"\0")
                if value
            )
            environ_raw = read_proc_file(base / "environ", MAX_PROC_BYTES)
            environment = {}
            for item in environ_raw.rstrip(b"\0").split(b"\0"):
                if b"=" not in item:
                    continue
                key, value = item.split(b"=", 1)
                environment[key.decode("ascii", "ignore")] = value.decode(
                    "utf-8", "surrogateescape"
                )
            executable = os.readlink(base / "exe")
            return ProcessInfo(pid, ppid, start, argv, environment, executable)
        except (
            FileNotFoundError,
            PermissionError,
            ProcessLookupError,
            StopIteration,
            UnicodeError,
            ValueError,
            OSError,
        ):
            return None

    def all(self) -> list[ProcessInfo]:
        result = []
        try:
            entries = list(self.proc_root.iterdir())
        except OSError as error:
            fail(f"cannot inspect process table: {error}")
        if len(entries) > 65536:
            fail("process table exceeds the supported bound")
        for entry in entries:
            if entry.name.isdecimal():
                value = self.get(int(entry.name))
                if value is not None:
                    result.append(value)
        return result


@dataclass(frozen=True)
class ShellProcesses:
    launchers: tuple[ProcessInfo, ...]
    shells: tuple[ProcessInfo, ...]
    watchers: tuple[ProcessInfo, ...]


class ShellController:
    def __init__(self, plugins_dir: Path):
        self.plugins_dir = plugins_dir
        self.processes = ProcessTable()
        self.omarchy = command_path("omarchy")
        self.quickshell = Path(command_path("quickshell")).resolve()
        self.launcher = Path(command_path("omarchy-launch-shell")).resolve()
        self.lock_check = command_path("omarchy-hyprland-session-locked")
        self.session_path = self._session_omarchy_path()
        self.shell_dir = self.session_path / "shell"
        if not (self.shell_dir / "shell.qml").is_file():
            fail(f"Omarchy shell config is unavailable: {self.shell_dir}")
        self.ipc_env = trusted_environment()
        self.ipc_env["OMARCHY_PATH"] = str(self.session_path)
        self.ipc_env["OMARCHY_SHELL_IPC_TIMEOUT"] = "0.5s"

    def _session_omarchy_path(self) -> Path:
        systemctl = command_path("systemctl")
        result = run(
            [systemctl, "--user", "show-environment"],
            capture=True,
            timeout=5,
            env=trusted_environment(),
        )
        values = []
        for line in bounded_text(result.stdout, "systemctl").splitlines():
            if line.startswith("OMARCHY_PATH="):
                values.append(line.split("=", 1)[1])
        if len(values) != 1 or not values[0].startswith("/"):
            fail("cannot bind the session OMARCHY_PATH")
        return Path(values[0])

    def _is_launcher(self, process: ProcessInfo) -> bool:
        if process.environment.get("OMARCHY_PATH") != str(self.session_path):
            return False
        for argument in process.argv:
            if not argument.startswith("/"):
                continue
            try:
                if Path(argument).resolve() == self.launcher:
                    return True
            except OSError:
                continue
        return False

    def _is_shell(self, process: ProcessInfo) -> bool:
        try:
            if Path(process.executable).resolve() != self.quickshell:
                return False
        except OSError:
            return False
        if process.environment.get("OMARCHY_PATH") != str(self.session_path):
            return False
        for index, argument in enumerate(process.argv[:-1]):
            if argument == "-p" and Path(process.argv[index + 1]) == self.shell_dir:
                return True
        return False

    def _is_watcher(self, process: ProcessInfo) -> bool:
        if not process.argv or Path(process.argv[0]).name != "inotifywait":
            return False
        return str(self.plugins_dir) in process.argv

    def scan(self) -> ShellProcesses:
        launchers = []
        shells = []
        watchers = []
        for process in self.processes.all():
            if self._is_launcher(process):
                launchers.append(process)
            if self._is_shell(process):
                shells.append(process)
            if self._is_watcher(process):
                watchers.append(process)
        key = lambda value: (value.pid, value.start)
        return ShellProcesses(
            tuple(sorted(launchers, key=key)),
            tuple(sorted(shells, key=key)),
            tuple(sorted(watchers, key=key)),
        )

    def ping(self) -> bool:
        result = run(
            [self.omarchy, "shell", "shell", "ping"],
            check=False,
            capture=True,
            timeout=2,
            env=self.ipc_env,
        )
        return result.returncode == 0 and bounded_text(result.stdout, "shell ping").strip() == "ok"

    def assert_running(self) -> ShellProcesses:
        state = self.scan()
        if len(state.launchers) != 1 or len(state.shells) != 1:
            fail("expected exactly one Omarchy shell supervisor and one Quickshell process")
        if state.shells[0].ppid != state.launchers[0].pid:
            fail("Quickshell is not owned by the bound Omarchy shell supervisor")
        if not self.ping():
            fail("Omarchy shell is not ready before update")
        return state

    def assert_stopped(self) -> None:
        state = self.scan()
        if state.launchers or state.shells or state.watchers or self.ping():
            fail("Omarchy shell or its supervisor restarted before the tree exchange")

    def refuse_locked_session(self) -> None:
        result = run(
            [self.lock_check],
            check=False,
            quiet=True,
            timeout=3,
            env=self.ipc_env,
        )
        if result.returncode == 0:
            fail("refusing to update while the Hyprland session is locked")
        if result.returncode != 1:
            fail("cannot determine whether the Hyprland session is locked")

    def _terminate_bound(self, process: ProcessInfo) -> None:
        current = self.processes.get(process.pid)
        if current is None:
            return
        if current != process:
            fail("shell supervisor identity changed before termination")
        try:
            pidfd = os.pidfd_open(process.pid, 0)
        except OSError as error:
            fail(f"cannot bind shell supervisor PID {process.pid}: {error}")
        try:
            signal.pidfd_send_signal(pidfd, signal.SIGTERM)
        finally:
            os.close(pidfd)

    def stop(self, *, require_running: bool) -> None:
        initial = self.assert_running() if require_running else self.scan()
        if not require_running and not (initial.launchers or initial.shells or initial.watchers):
            if self.ping():
                fail("shell IPC remains reachable without a bound shell process")
            return

        if initial.shells:
            run(
                [
                    str(self.quickshell),
                    "kill",
                    "-p",
                    str(self.shell_dir),
                    "--any-display",
                ],
                check=False,
                quiet=True,
                timeout=5,
                env=self.ipc_env,
            )

        # A nonzero Quickshell exit leaves the launcher in its one-second
        # backoff. Terminate that exact supervisor before it can relaunch.
        state = self.scan()
        for launcher in state.launchers:
            self._terminate_bound(launcher)

        deadline = time.monotonic() + 10
        last_kill = 0.0
        while time.monotonic() < deadline:
            state = self.scan()
            if not state.launchers and not state.shells and not state.watchers:
                if not self.ping():
                    return
            elif state.shells and not state.launchers and time.monotonic() - last_kill > 0.25:
                run(
                    [
                        str(self.quickshell),
                        "kill",
                        "-p",
                        str(self.shell_dir),
                        "--any-display",
                    ],
                    check=False,
                    quiet=True,
                    timeout=5,
                    env=self.ipc_env,
                )
                last_kill = time.monotonic()
            time.sleep(0.05)
        fail("Omarchy shell supervisor did not stop cleanly")

    def start(self) -> ShellProcesses:
        run([self.omarchy, "restart", "shell"], timeout=45, env=self.ipc_env)
        return self.assert_running()

    def assert_same_shell(self, expected: ShellProcesses) -> None:
        current = self.scan()
        if current.launchers != expected.launchers or current.shells != expected.shells:
            fail("the restarted Omarchy shell changed during consumer checks")

    def journal_cursor(self) -> str:
        result = run(
            [
                command_path("journalctl"),
                "--no-pager",
                "--quiet",
                "--lines=0",
                "--show-cursor",
                "--identifier=omarchy-shell",
            ],
            capture=True,
            timeout=5,
            env=self.ipc_env,
        )
        lines = bounded_text(result.stdout, "journalctl cursor").splitlines()
        cursors = [line[11:] for line in lines if line.startswith("-- cursor: ")]
        if len(cursors) != 1 or not cursors[0]:
            fail("cannot bind the pre-start Omarchy shell journal cursor")
        return cursors[0]

    def journal_failed(self, cursor: str) -> str:
        result = run(
            [
                command_path("journalctl"),
                "--no-pager",
                "--quiet",
                "--after-cursor",
                cursor,
                "--output=cat",
                "--identifier=omarchy-shell",
            ],
            capture=True,
            timeout=5,
            env=self.ipc_env,
        )
        text = bounded_text(result.stdout, "journalctl")
        for line in text.splitlines():
            lowered = line.lower()
            plugin_failure = "plugin widget" in lowered or "plugin service" in lowered
            if PLUGIN_ID in line and plugin_failure and "failed" in lowered:
                return line
        return ""

    def require_enabled_plugin(self) -> None:
        listed = run(
            [self.omarchy, "plugin", "list", "--json"],
            capture=True,
            timeout=3,
            env=self.ipc_env,
        )
        plugins = strict_json(
            bounded_text(listed.stdout, "plugin list"), "plugin list"
        )
        if not isinstance(plugins, list):
            fail("plugin list is not an array")
        matches = [
            item
            for item in plugins
            if isinstance(item, dict) and item.get("id") == PLUGIN_ID
        ]
        if len(matches) != 1:
            fail("plugin list does not contain exactly one OmaQ entry")
        entry = matches[0]
        if entry.get("enabled") is not True or entry.get("firstParty") is not False:
            fail("OmaQ must be an enabled third-party plugin before update")
        if "bar-widget" not in entry.get("kinds", []):
            fail("OmaQ bar-widget kind is unavailable")

    def consumer_ready(
        self,
        cursor: str,
        expected_available: str,
        expected_shell: ShellProcesses,
        *,
        runtime_path: Path | None = None,
        allowed_running: set[str] | None = None,
        expected_running_pid: int | None = None,
        required_protocol: int | None = None,
    ) -> dict:
        deadline = time.monotonic() + 20
        last_error = "plugin did not become ready"
        while time.monotonic() < deadline:
            self.assert_same_shell(expected_shell)
            failed_line = self.journal_failed(cursor)
            if failed_line:
                fail(f"new shell reported an OmaQ loader failure: {failed_line}")
            try:
                listed = run(
                    [self.omarchy, "plugin", "list", "--json"],
                    capture=True,
                    timeout=3,
                    env=self.ipc_env,
                )
                plugins = strict_json(
                    bounded_text(listed.stdout, "plugin list"), "plugin list"
                )
                if not isinstance(plugins, list):
                    fail("plugin list is not an array")
                matches = [
                    item
                    for item in plugins
                    if isinstance(item, dict) and item.get("id") == PLUGIN_ID
                ]
                if len(matches) != 1:
                    last_error = "plugin list does not contain exactly one OmaQ entry"
                    time.sleep(0.1)
                    continue
                entry = matches[0]
                if entry.get("enabled") is not True or entry.get("firstParty") is not False:
                    last_error = "OmaQ is not an enabled third-party plugin"
                    time.sleep(0.1)
                    continue
                if "bar-widget" not in entry.get("kinds", []):
                    last_error = "OmaQ bar-widget kind is unavailable"
                    time.sleep(0.1)
                    continue
                ipc = run(
                    [self.omarchy, "shell", PLUGIN_ID, "status"],
                    check=False,
                    capture=True,
                    timeout=3,
                    env=self.ipc_env,
                )
                if ipc.returncode != 0:
                    last_error = "OmaQ IPC target is unavailable"
                    time.sleep(0.1)
                    continue
                helper = helper_call(
                    self.live_root, "status", runtime_path=runtime_path
                )
                validate_consumer_helper(
                    helper,
                    expected_available,
                    allowed_running=allowed_running,
                    expected_running_pid=expected_running_pid,
                    required_protocol=required_protocol,
                )
                failed_line = self.journal_failed(cursor)
                if failed_line:
                    fail(f"new shell reported an OmaQ loader failure: {failed_line}")
                self.assert_same_shell(expected_shell)
                return helper
            except UpdateError as error:
                last_error = str(error)
                time.sleep(0.1)
        fail(last_error)

    live_root: Path


def helper_call(
    root: Path,
    action: str,
    *,
    expected_hash: str = "",
    runtime_path: Path | None = None,
) -> dict:
    runtime = runtime_path or (root / "scripts/helper-runtime.py")
    if not runtime.is_file():
        fail(f"helper runtime tool is unavailable: {runtime}")
    command = [
        command_path("python3"),
        str(runtime),
        action,
        "--root",
        str(root),
        "--state",
        os.environ.get("OMAQ_STATE", str(Path.home() / ".local/state/omaq")),
        "--root-identity",
        helper_root_identity(root),
        "--json",
    ]
    if expected_hash:
        command.extend(["--expect-sha256", expected_hash])
    result = run(
        command, capture=True, timeout=60, env=trusted_environment()
    )
    value = strict_json(bounded_text(result.stdout, "helper runtime"), "helper runtime")
    if not isinstance(value, dict):
        fail("helper runtime result is not an object")
    return value


def require_protocol_compatible(required: int, running: int) -> None:
    if type(required) is not int or type(running) is not int:
        fail("helper protocol compatibility values must be integers")
    if required < 1 or required > 1024 or running < 1 or running > 1024:
        fail("helper protocol compatibility value is outside the supported range")
    if required > running:
        fail(
            f"staged QML requires helper protocol {required}, but the running "
            f"helper is Protocol {running}"
        )


def validate_helper_status(value: dict) -> None:
    if value.get("state") not in {"current", "update-pending"}:
        fail("a running Protocol-9-or-newer helper is required for source updates")
    protocol = value.get("running_protocol")
    running_pid = value.get("running_pid")
    running_hash = value.get("running_sha256")
    available_hash = value.get("available_sha256")
    if type(protocol) is not int or protocol < EXPECTED_HELPER_PROTOCOL_MIN or protocol > 1024:
        fail("running helper protocol is unavailable or unsupported")
    if type(running_pid) is not int or running_pid <= 1:
        fail("running helper PID is unavailable or invalid")
    for name, digest in (("running", running_hash), ("available", available_hash)):
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            fail(f"invalid {name} helper hash")


def validate_consumer_helper(
    value: dict,
    expected_available: str,
    *,
    allowed_running: set[str] | None = None,
    expected_running_pid: int | None = None,
    required_protocol: int | None = None,
) -> None:
    if value.get("available_sha256") != expected_available:
        fail("available helper changed during shell consumer checks")
    validate_helper_status(value)
    if (
        allowed_running is not None
        and value.get("running_sha256") not in allowed_running
    ):
        fail("an unexpected helper image became active during consumer checks")
    if (
        expected_running_pid is not None
        and value.get("running_pid") != expected_running_pid
    ):
        fail("the helper process changed during consumer checks")
    if required_protocol is not None:
        require_protocol_compatible(required_protocol, value["running_protocol"])


def validate_activation_result(
    value: dict,
    expected_available: str,
    allowed_pending: set[str],
    required_protocol: int,
) -> tuple[str, str]:
    state = value.get("state")
    if state not in {"activated", "current", "update-pending"}:
        fail("helper activation did not leave a running verified helper")
    available = value.get("available_sha256")
    running = value.get("running_sha256")
    protocol = value.get("running_protocol")
    pid = value.get("running_pid")
    if available != expected_available:
        fail("helper activation returned an unexpected available hash")
    if not isinstance(running, str) or not re.fullmatch(r"[0-9a-f]{64}", running):
        fail("helper activation returned an invalid running hash")
    if type(pid) is not int or pid <= 1:
        fail("helper activation returned an invalid running PID")
    if (
        type(protocol) is not int
        or protocol < EXPECTED_HELPER_PROTOCOL_MIN
        or protocol > 1024
    ):
        fail("helper activation returned an unsupported running protocol")
    require_protocol_compatible(required_protocol, protocol)
    if state == "update-pending":
        if running not in allowed_pending:
            fail("helper activation left an unexpected pending helper running")
        if value.get("detail") not in {
            "active_groups",
            "group_state_uncertain",
            "activation_unsupported",
        }:
            fail("helper activation returned an invalid pending reason")
    elif running != expected_available:
        fail("helper activation did not start the expected helper image")
    return state, running


@dataclass(frozen=True)
class StagedTree:
    path: Path
    commit: str
    helper_hash: str
    required_protocol: int


def git_environment() -> dict[str, str]:
    # Do not inherit repository redirection, object stores, TLS overrides,
    # credential helpers, proxies, or injected GIT_CONFIG_* records.
    environment = {
        "HOME": str(Path.home()),
        "PATH": "/usr/bin:/bin",
        "LANG": os.environ.get("LANG", "C.UTF-8"),
        "GIT_ATTR_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": "/dev/null",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_OPTIONAL_LOCKS": "0",
        "GIT_TERMINAL_PROMPT": "0",
    }
    return environment


def git_network_environment() -> dict[str, str]:
    environment = git_environment()
    if CANONICAL_ORIGIN != "https://github.com/HANCORE-linux/OmaQ.git":
        return environment
    header = github_auth_header()
    if header is not None:
        environment.update(
            {
                "GIT_CONFIG_COUNT": "1",
                "GIT_CONFIG_KEY_0": "http.https://github.com/.extraHeader",
                "GIT_CONFIG_VALUE_0": header,
            }
        )
    return environment


def git_output(root: Path, arguments: list[str], source: str) -> str:
    git = command_path("git")
    return capture_line(
        [git, "-c", "core.hooksPath=/dev/null", "-C", str(root), *arguments],
        source,
        env=git_environment(),
    )


def validate_git_checkout(root: Path, *, expected_origin: str) -> str:
    lstat_directory(root)
    lstat_directory(root / ".git")
    branch = git_output(root, ["symbolic-ref", "--short", "HEAD"], "git branch")
    if branch != "main":
        fail(f"OmaQ checkout is not on main: {branch}")
    origin = git_output(root, ["remote", "get-url", "origin"], "git origin")
    if origin != expected_origin:
        fail(f"unexpected OmaQ origin: {origin}")
    head = git_output(root, ["rev-parse", "--verify", "HEAD"], "git HEAD")
    if not HEX_40.fullmatch(head):
        fail("Git returned an invalid HEAD commit")
    status = run(
        [
            command_path("git"),
            "-c",
            "core.hooksPath=/dev/null",
            "-c",
            "core.fsmonitor=false",
            "-C",
            str(root),
            "status",
            "--porcelain=v1",
            "--untracked-files=all",
            "-z",
        ],
        capture=True,
        timeout=30,
        env=git_environment(),
    )
    if status.stdout:
        fail(f"Git checkout has local changes: {root}")
    return head


def validate_manifest(root: Path) -> None:
    path = root / "manifest.json"
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_nlink != 1
            or info.st_mode & 0o022
            or info.st_size > 1024 * 1024
        ):
            fail("unsafe plugin manifest")
        raw = os.read(fd, 1024 * 1024 + 1)
    finally:
        os.close(fd)
    value = strict_json(bounded_text(raw, "manifest.json"), "manifest.json")
    if not isinstance(value, dict) or value.get("id") != PLUGIN_ID:
        fail("plugin manifest does not identify OmaQ")


def validate_plugin(root: Path) -> None:
    validate_manifest(root)
    run(
        [command_path("omarchy"), "plugin", "validate", str(root)],
        timeout=30,
        env=trusted_environment(),
    )


def validate_build_artifacts(root: Path) -> None:
    result = run(
        [
            command_path("git"),
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
        env=git_environment(),
    )
    records = [record for record in result.stdout.split(b"\0") if record]
    if records != [b"!! helper/omaq"]:
        fail("staged build created an unexpected or modified tree entry")


def resolve_remote_main(update_base: Path, expected_commit: str) -> str:
    git = command_path("git")
    remote = run(
        [
            git,
            *GIT_NETWORK_CONFIG,
            "ls-remote",
            "--exit-code",
            CANONICAL_ORIGIN,
            "refs/heads/main",
        ],
        capture=True,
        timeout=60,
        env=git_network_environment(),
        cwd=update_base,
        preexec_fn=limit_staging_file_size,
    )
    fields = bounded_text(remote.stdout, "git ls-remote").strip().split()
    if (
        len(fields) != 2
        or fields[1] != "refs/heads/main"
        or not HEX_40.fullmatch(fields[0])
    ):
        fail("canonical origin returned an ambiguous main ref")
    target = fields[0]
    if expected_commit and target != expected_commit:
        fail(f"origin/main is {target}, not the expected commit {expected_commit}")
    return target


def stage_update(
    update_base: Path,
    live_head: str,
    expected_commit: str,
    target_commit: str = "",
) -> StagedTree:
    git = command_path("git")
    check_update_storage(update_base)
    target = target_commit or resolve_remote_main(update_base, expected_commit)
    if not HEX_40.fullmatch(target):
        fail("target commit is not a lowercase 40-hex object name")
    if expected_commit and target != expected_commit:
        fail(f"origin/main is {target}, not the expected commit {expected_commit}")

    timestamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    stage = update_base / f"tree-{timestamp}-{secrets.token_hex(6)}"
    run_tree_bounded(
        [
            git,
            *GIT_NETWORK_CONFIG,
            "clone",
            "--no-hardlinks",
            "--branch",
            "main",
            "--single-branch",
            "--",
            CANONICAL_ORIGIN,
            str(stage),
        ],
        stage,
        timeout=300,
        env=git_network_environment(),
        cwd=update_base,
    )
    stage_head = validate_git_checkout(stage, expected_origin=CANONICAL_ORIGIN)
    if stage_head != target:
        fail("origin/main changed while the staging clone was created")
    ancestry = run(
        [
            git,
            "-c",
            "core.hooksPath=/dev/null",
            "-C",
            str(stage),
            "merge-base",
            "--is-ancestor",
            live_head,
            stage_head,
        ],
        check=False,
        quiet=True,
        env=git_environment(),
    )
    if ancestry.returncode != 0:
        fail("staged source is not a fast-forward of the installed checkout")
    check_tree_bounds(stage)
    validate_plugin(stage)
    required = parse_required_helper_protocol(stage / "Service.qml")
    run_tree_bounded(
        [command_path("make"), "--no-print-directory", "-C", str(stage), "helper"],
        stage,
        timeout=600,
        env=trusted_environment(),
    )
    check_tree_bounds(stage)
    validate_plugin(stage)
    if validate_git_checkout(stage, expected_origin=CANONICAL_ORIGIN) != stage_head:
        fail("staged checkout changed while building the helper")
    validate_build_artifacts(stage)
    helper_hash = hash_regular_executable(stage / "helper/omaq")
    return StagedTree(stage, stage_head, helper_hash, required)


def preflight_exchange_support(
    staging: Path,
    live: Path,
    *,
    mv_path: str | None = None,
    mount_resolver: Callable[[Path], int] = mount_id_for,
) -> None:
    staging_identity = directory_identity(staging)
    live_identity = directory_identity(live)
    if staging_identity[0] != live_identity[0]:
        fail("staging and live trees are on different filesystems")
    if mount_resolver(staging) != mount_resolver(live):
        fail("staging and live trees cross a mount boundary")

    parent = staging.parent
    lstat_directory(parent, private=True)
    token = f"exchange-probe-{os.getpid()}-{secrets.token_hex(8)}"
    first = parent / f"{token}-a"
    second = parent / f"{token}-b"
    created = []
    try:
        first.mkdir(mode=0o700)
        created.append(first)
        second.mkdir(mode=0o700)
        created.append(second)
        first_identity = directory_identity(first)
        second_identity = directory_identity(second)
        command = mv_path or command_path("mv")
        result = run(
            [
                command,
                "-T",
                "--exchange",
                "--no-copy",
                "--",
                str(first),
                str(second),
            ],
            check=False,
            quiet=True,
            env=trusted_environment(),
        )
        if result.returncode != 0:
            fail("atomic no-copy directory exchange is unavailable")
        if (
            directory_identity(first) != second_identity
            or directory_identity(second) != first_identity
        ):
            fail("atomic exchange probe returned unexpected directory identities")
    finally:
        for path in created:
            try:
                path.rmdir()
            except FileNotFoundError:
                pass
        fsync_directory(parent)


def exchange_trees(
    staging: Path,
    live: Path,
    assert_shell_stopped: Callable[[], None],
    *,
    mv_path: str = "/usr/bin/mv",
) -> None:
    staging_before = directory_identity(staging)
    live_before = directory_identity(live)
    if staging_before[0] != live_before[0]:
        fail("staging and live trees are on different filesystems")
    if mount_id_for(staging) != mount_id_for(live):
        fail("staging and live trees cross a mount boundary")
    if staging == live or live in staging.parents or staging in live.parents:
        fail("staging and live tree paths overlap")

    # This is the last cooperative race check before renameat2. The documented
    # same-user trust boundary excludes an uncooperative concurrent launcher.
    assert_shell_stopped()
    result = run(
        [mv_path, "-T", "--exchange", "--no-copy", "--", str(staging), str(live)],
        check=False,
        env=trusted_environment(),
    )
    if result.returncode != 0:
        if directory_identity(staging) != staging_before or directory_identity(live) != live_before:
            fail("tree exchange failed after changing a directory identity")
        fail("atomic tree exchange failed without a copy fallback")

    if directory_identity(live) != staging_before or directory_identity(staging) != live_before:
        fail("atomic tree exchange returned with unexpected directory identities")
    fsync_directory(live.parent)
    if staging.parent != live.parent:
        fsync_directory(staging.parent)


class UpdateLock:
    def __init__(self, plugins_dir: Path | None = None):
        runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.geteuid()}"))
        if plugins_dir is not None:
            refuse_monitored_path(runtime, plugins_dir, "XDG_RUNTIME_DIR")
        lstat_directory(runtime, private=True)
        self.fds: list[int] = []

        source_dir = runtime / "omaq-source-update"
        try:
            source_dir.mkdir(mode=0o700)
        except FileExistsError:
            pass
        lstat_directory(source_dir, private=True)
        self.source_dir = source_dir
        source_fd = os.open(
            source_dir / "lock",
            os.O_RDONLY | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
            0o600,
        )
        info = os.fstat(source_fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
            os.close(source_fd)
            fail("unsafe source-update lock")
        try:
            fcntl.flock(source_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            os.close(source_fd)
            fail("another OmaQ source update is active")
        self.fds.append(source_fd)

        # Serialize with update-helper.sh, which flocks this exact private
        # directory descriptor rather than a separate lock file.
        helper_dir = runtime / "omaq-helper-update"
        try:
            helper_dir.mkdir(mode=0o700)
        except FileExistsError:
            pass
        lstat_directory(helper_dir, private=True)
        helper_fd = os.open(
            helper_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
        )
        try:
            fcntl.flock(helper_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            os.close(helper_fd)
            self.close()
            fail("a helper update is already active")
        self.fds.append(helper_fd)

    def close(self):
        while self.fds:
            os.close(self.fds.pop())


class Updater:
    def __init__(self, expected_commit: str):
        # The controller may run from an external canonical bootstrap clone so
        # installations predating this script can enter the safe workflow
        # without first modifying their monitored live checkout.
        self.program_root = Path(__file__).absolute().parent.parent
        lstat_directory(self.program_root)
        self.root = Path.home() / ".config/omarchy/plugins" / PLUGIN_ID
        if self.root.is_symlink():
            fail(f"live plugin root must be a plain Git checkout: {self.root}")
        self.plugins_dir = self.root.parent
        lstat_directory(self.plugins_dir)
        lstat_directory(self.root)
        self.expected_commit = expected_commit
        self.lock = UpdateLock(self.plugins_dir)
        self.shell = ShellController(self.plugins_dir)
        self.shell.live_root = self.root
        state_home = Path(
            os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))
        )
        if not state_home.is_absolute():
            fail("XDG_STATE_HOME must be absolute")
        refuse_monitored_path(state_home, self.plugins_dir, "XDG_STATE_HOME")
        try:
            state_home.mkdir(mode=0o700, parents=True, exist_ok=True)
        except OSError as error:
            fail(f"cannot create state directory: {error}")
        lstat_directory(state_home)
        self.update_base = state_home / "omaq-source-updates"
        try:
            self.update_base.mkdir(mode=0o700)
        except FileExistsError:
            pass
        lstat_directory(self.update_base, private=True)
        self.runtime_tool = copy_runtime_tool(
            self.program_root / "scripts/helper-runtime.py", self.lock.source_dir
        )
        self.staged: StagedTree | None = None
        self.original_helper: dict | None = None
        self.exchanged = False
        self.shell_stopped = False

    def preflight(self) -> tuple[str, dict, ShellProcesses]:
        if os.geteuid() == 0:
            fail("refusing to update OmaQ as root")
        self.shell.refuse_locked_session()
        initial_shell = self.shell.assert_running()
        self.shell.require_enabled_plugin()
        live_head = validate_git_checkout(self.root, expected_origin=CANONICAL_ORIGIN)
        validate_plugin(self.root)
        helper = helper_call(
            self.root, "status", runtime_path=self.runtime_tool
        )
        validate_helper_status(helper)
        return live_head, helper, initial_shell

    def restore_shell_without_exchange(self) -> None:
        if not self.shell_stopped:
            return
        try:
            self.shell.start()
        finally:
            self.shell_stopped = False

    def stop_shell_for_transaction(self, *, require_running: bool) -> None:
        self.shell_stopped = True
        try:
            self.shell.stop(require_running=require_running)
        except Exception as stop_error:
            try:
                self.restore_shell_without_exchange()
            except Exception as restart_error:
                fail(
                    f"shell stop failed ({stop_error}); shell recovery failed "
                    f"({restart_error})"
                )
            raise

    def exchange_staged_tree(self) -> None:
        assert self.staged is not None
        staged_identity = directory_identity(self.staged.path)
        live_identity = directory_identity(self.root)
        try:
            exchange_trees(self.staged.path, self.root, self.shell.assert_stopped)
        finally:
            # Track a completed rename even if a later identity or fsync check
            # fails, so the exception path restores the previous tree.
            try:
                self.exchanged = (
                    directory_identity(self.root) == staged_identity
                    and directory_identity(self.staged.path) == live_identity
                )
            except UpdateError:
                pass

    def rollback_tree(self, original_error: Exception) -> None:
        assert self.staged is not None
        rollback_error = None
        try:
            self.stop_shell_for_transaction(require_running=False)
            exchange_trees(self.staged.path, self.root, self.shell.assert_stopped)
            self.exchanged = False
            validate_plugin(self.root)
            cursor = self.shell.journal_cursor()
            started_shell = self.shell.start()
            self.shell_stopped = False
            restored_hash = hash_regular_executable(self.root / "helper/omaq")
            allowed_running = {restored_hash, self.staged.helper_hash}
            if self.original_helper is not None:
                allowed_running.add(self.original_helper["running_sha256"])
            self.shell.consumer_ready(
                cursor,
                restored_hash,
                started_shell,
                runtime_path=self.runtime_tool,
                allowed_running=allowed_running,
                required_protocol=parse_required_helper_protocol(
                    self.root / "Service.qml"
                ),
            )
        except Exception as error:  # Preserve both failures in the final report.
            rollback_error = error
        if rollback_error:
            fail(
                f"update failed ({original_error}); automatic tree rollback also "
                f"failed ({rollback_error})"
            )
        fail(f"update failed and the previous tree was restored: {original_error}")

    def finish_noop_update(
        self,
        live_head: str,
        live_identity: tuple[int, int],
        expected_shell: ShellProcesses,
    ) -> dict:
        if directory_identity(self.root) != live_identity:
            fail("live plugin root changed during the no-op preflight")
        if (
            validate_git_checkout(self.root, expected_origin=CANONICAL_ORIGIN)
            != live_head
        ):
            fail("live checkout changed during the no-op preflight")
        self.shell.assert_same_shell(expected_shell)
        helper = helper_call(
            self.root, "status", runtime_path=self.runtime_tool
        )
        validate_helper_status(helper)
        required_protocol = parse_required_helper_protocol(
            self.root / "Service.qml"
        )
        require_protocol_compatible(
            required_protocol, helper["running_protocol"]
        )
        result = helper
        if helper["state"] == "update-pending":
            self.shell.refuse_locked_session()
            cursor = self.shell.journal_cursor()
            result = helper_call(
                self.root,
                "activate",
                expected_hash=helper["available_sha256"],
                runtime_path=self.runtime_tool,
            )
            state, activated_hash = validate_activation_result(
                result,
                helper["available_sha256"],
                {helper["running_sha256"]},
                required_protocol,
            )
            self.shell.consumer_ready(
                cursor,
                helper["available_sha256"],
                expected_shell,
                runtime_path=self.runtime_tool,
                allowed_running={activated_hash},
                expected_running_pid=result["running_pid"],
                required_protocol=required_protocol,
            )
            if state == "update-pending":
                print("update-pending: old helper, new tree")
                print(f"  Detail: {result['detail']}")
            else:
                print(f"helper: {state}")
        else:
            print("helper: current")
        print(f"source: current ({live_head})")
        print(f"available helper: {helper['available_sha256']}")
        return result

    def update(self) -> dict:
        live_head, old_helper, initial_shell = self.preflight()
        self.original_helper = old_helper
        live_identity = directory_identity(self.root)
        target_commit = resolve_remote_main(
            self.update_base, self.expected_commit
        )
        if target_commit == live_head:
            return self.finish_noop_update(
                live_head, live_identity, initial_shell
            )
        self.staged = stage_update(
            self.update_base,
            live_head,
            self.expected_commit,
            target_commit,
        )
        if directory_identity(self.root) != live_identity:
            fail("live plugin root changed while the replacement was staged")
        if (
            validate_git_checkout(self.root, expected_origin=CANONICAL_ORIGIN)
            != live_head
        ):
            fail("live checkout changed while the replacement was staged")
        refreshed_helper = helper_call(
            self.root, "status", runtime_path=self.runtime_tool
        )
        validate_helper_status(refreshed_helper)
        for key in ("running_pid", "running_protocol", "running_sha256"):
            if refreshed_helper.get(key) != old_helper.get(key):
                fail("running helper changed while the replacement was staged")
        require_protocol_compatible(
            self.staged.required_protocol, old_helper["running_protocol"]
        )
        preflight_exchange_support(self.staged.path, self.root)

        self.shell.refuse_locked_session()
        try:
            self.stop_shell_for_transaction(require_running=True)
            before_backup = helper_call(
                self.root, "backup", runtime_path=self.runtime_tool
            )
            for key in ("running_pid", "running_protocol", "running_sha256"):
                if before_backup.get(key) != old_helper[key]:
                    fail("running helper changed before the source exchange")
            self.exchange_staged_tree()
            validate_plugin(self.root)
            if (
                validate_git_checkout(self.root, expected_origin=CANONICAL_ORIGIN)
                != self.staged.commit
            ):
                fail("live checkout does not match the staged commit after exchange")
            after_backup = helper_call(
                self.root, "backup", runtime_path=self.runtime_tool
            )
            if (
                any(
                    after_backup.get(key) != old_helper[key]
                    for key in ("running_pid", "running_protocol", "running_sha256")
                )
                or after_backup.get("available_sha256") != self.staged.helper_hash
            ):
                fail("post-exchange helper backup is not bound to the expected images")
            if (
                hash_regular_executable(self.root / "helper/omaq.prev")
                != old_helper["running_sha256"]
            ):
                fail("post-exchange .prev does not contain the old running helper")
            cursor = self.shell.journal_cursor()
            started_shell = self.shell.start()
            self.shell_stopped = False
            consumer_helper = self.shell.consumer_ready(
                cursor,
                self.staged.helper_hash,
                started_shell,
                runtime_path=self.runtime_tool,
                allowed_running={
                    old_helper["running_sha256"],
                    self.staged.helper_hash,
                },
                required_protocol=self.staged.required_protocol,
            )
        except Exception as error:
            if self.exchanged:
                self.rollback_tree(error)
            try:
                self.restore_shell_without_exchange()
            except Exception as restart_error:
                fail(
                    f"update stopped before exchange ({error}); shell recovery "
                    f"failed ({restart_error})"
                )
            raise

        running_hash = consumer_helper.get("running_sha256")
        if running_hash not in {old_helper["running_sha256"], self.staged.helper_hash}:
            fail("an unexpected helper image became active during consumer checks")
        activation = helper_call(
            self.root,
            "activate",
            expected_hash=self.staged.helper_hash,
            runtime_path=self.runtime_tool,
        )
        state, activated_hash = validate_activation_result(
            activation,
            self.staged.helper_hash,
            {old_helper["running_sha256"]},
            self.staged.required_protocol,
        )
        self.shell.consumer_ready(
            cursor,
            self.staged.helper_hash,
            started_shell,
            runtime_path=self.runtime_tool,
            allowed_running={activated_hash},
            expected_running_pid=activation["running_pid"],
            required_protocol=self.staged.required_protocol,
        )
        if state == "update-pending":
            print("update-pending: old helper, new tree")
            print(f"  Detail: {activation['detail']}")
        else:
            print(f"helper: {state}")
        print(f"source: {live_head} -> {self.staged.commit}")
        print(f"available helper: {self.staged.helper_hash}")
        print(f"previous tree: {self.staged.path}")
        return activation

    def rollback_helper(self) -> dict:
        if os.geteuid() == 0:
            fail("refusing to update OmaQ as root")
        self.shell.refuse_locked_session()
        self.shell.assert_running()
        self.shell.require_enabled_plugin()
        validate_git_checkout(self.root, expected_origin=CANONICAL_ORIGIN)
        validate_plugin(self.root)
        required_protocol = parse_required_helper_protocol(self.root / "Service.qml")
        self.shell.refuse_locked_session()
        try:
            self.stop_shell_for_transaction(require_running=True)
            restored = helper_call(
                self.root, "restore", runtime_path=self.runtime_tool
            )
            restored_hash = restored.get("available_sha256")
            if not isinstance(restored_hash, str) or not re.fullmatch(
                r"[0-9a-f]{64}", restored_hash
            ):
                fail("helper rollback returned an invalid hash")
            cursor = self.shell.journal_cursor()
            started_shell = self.shell.start()
            self.shell_stopped = False
            before_activation = self.shell.consumer_ready(
                cursor,
                restored_hash,
                started_shell,
                runtime_path=self.runtime_tool,
                required_protocol=required_protocol,
            )
        except Exception as error:
            try:
                self.restore_shell_without_exchange()
            except Exception as restart_error:
                fail(f"helper rollback failed ({error}); shell recovery failed ({restart_error})")
            raise
        activation = helper_call(
            self.root,
            "activate",
            expected_hash=restored_hash,
            runtime_path=self.runtime_tool,
        )
        state, activated_hash = validate_activation_result(
            activation,
            restored_hash,
            {before_activation["running_sha256"]},
            required_protocol,
        )
        self.shell.consumer_ready(
            cursor,
            restored_hash,
            started_shell,
            runtime_path=self.runtime_tool,
            allowed_running={activated_hash},
            expected_running_pid=activation["running_pid"],
            required_protocol=required_protocol,
        )
        print(f"helper rollback: {state}")
        print(f"available helper: {restored_hash}")
        return activation

    def close(self):
        runtime_tool = getattr(self, "runtime_tool", None)
        if runtime_tool is not None:
            try:
                runtime_tool.unlink()
                fsync_directory(runtime_tool.parent)
            except FileNotFoundError:
                pass
        self.lock.close()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="update-omaq.sh",
        description="Stage, build, and atomically activate an OmaQ source update.",
    )
    parser.add_argument(
        "--expect-commit", default="", help="require this exact origin/main commit"
    )
    parser.add_argument(
        "--rollback-helper",
        action="store_true",
        help="restore helper/omaq.prev under the shell-off boundary",
    )
    parser.add_argument(
        "--yes", action="store_true", help="confirm the temporary shell stop"
    )
    args = parser.parse_args(argv)
    if args.expect_commit and not HEX_40.fullmatch(args.expect_commit):
        parser.error("--expect-commit requires a lowercase 40-hex commit")
    if args.rollback_helper and args.expect_commit:
        parser.error("--rollback-helper cannot be combined with --expect-commit")
    if not args.yes:
        parser.error("refusing to stop the shell without confirmation; pass --yes")
    return args


def interrupted(_signum, _frame):
    raise InterruptedUpdate("update interrupted")


def main(argv: list[str] | None = None) -> int:
    os.umask(0o077)
    args = parse_args(sys.argv[1:] if argv is None else argv)
    signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGHUP, interrupted)
    updater = None
    try:
        updater = Updater(args.expect_commit)
        if args.rollback_helper:
            updater.rollback_helper()
        else:
            updater.update()
        return 0
    except (UpdateError, OSError, ValueError) as error:
        print(f"update-omaq: {error}", file=sys.stderr)
        return 1
    finally:
        if updater is not None:
            updater.close()


if __name__ == "__main__":
    raise SystemExit(main())
