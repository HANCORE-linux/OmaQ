#!/usr/bin/env python3
"""Inspect, back up, and optionally activate OmaQ's locally built helper."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import stat
import struct
import sys
import time

MAX_HELPER = 16 * 1024 * 1024
MAX_MARKER = 1024
RUNTIME_NAMES = ("omaq.pid", "omaq.protocol", "omaq.sock")
JSON_OUTPUT = False


def fail(message: str):
    raise RuntimeError(message)


def strict_json(raw: bytes):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                fail("duplicate JSON key")
            result[key] = value
        return result
    return json.loads(raw.decode("utf-8", "strict"), object_pairs_hook=pairs)


def hash_fd(fd: int) -> tuple[str, bytes]:
    os.lseek(fd, 0, os.SEEK_SET)
    chunks = []
    total = 0
    digest = hashlib.sha256()
    while True:
        chunk = os.read(fd, min(1024 * 1024, MAX_HELPER + 1 - total))
        if not chunk:
            break
        total += len(chunk)
        if total > MAX_HELPER:
            fail("helper exceeds the 16 MiB bound")
        digest.update(chunk)
        chunks.append(chunk)
    os.lseek(fd, 0, os.SEEK_SET)
    return digest.hexdigest(), b"".join(chunks)


def process_start(pid: int) -> int:
    raw = Path(f"/proc/{pid}/stat").read_text(encoding="ascii")
    close = raw.rfind(")")
    fields = raw[close + 2:].split()
    if close < 0 or len(fields) <= 19:
        fail("malformed helper process stat")
    return int(fields[19])


def process_uid(pid: int) -> int:
    raw = Path(f"/proc/{pid}/status").read_text(encoding="ascii")
    line = next(value for value in raw.splitlines() if value.startswith("Uid:"))
    return int(line.split()[1])


def helper_pids(helper_path: Path) -> list[int]:
    wanted = str(helper_path).encode() + b"\0"
    result = []
    for entry in Path("/proc").iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            fd = os.open(entry / "cmdline", os.O_RDONLY | os.O_CLOEXEC |
                         os.O_NONBLOCK | os.O_NOFOLLOW)
            try:
                command = os.read(fd, len(wanted) + 1)
            finally:
                os.close(fd)
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
        if command == wanted:
            result.append(int(entry.name))
    return sorted(result)


def open_directory(path: Path, private: bool) -> int:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW)
    info = os.fstat(fd)
    disallowed = 0o077 if private else 0o022
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or
            info.st_mode & disallowed):
        os.close(fd)
        fail(f"unsafe directory: {path}")
    return fd


def read_private(state_fd: int, name: str, maximum: int) -> bytes:
    fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW |
                 os.O_NONBLOCK, dir_fd=state_fd)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or
                info.st_nlink != 1 or info.st_mode & 0o077 or
                info.st_size < 0 or info.st_size > maximum):
            fail(f"unsafe runtime marker: {name}")
        data = os.read(fd, maximum + 1)
        if len(data) > maximum or len(data) != info.st_size:
            fail(f"runtime marker changed while reading: {name}")
        return data
    finally:
        os.close(fd)


def runtime_absent(state_dir: Path, helper_path: Path) -> bool:
    return (not any(os.path.lexists(state_dir / name) for name in RUNTIME_NAMES) and
            not helper_pids(helper_path))


def open_available(helper_dir_fd: int) -> tuple[int, os.stat_result, str]:
    fd = os.open("omaq", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW |
                 os.O_NONBLOCK, dir_fd=helper_dir_fd)
    info = os.fstat(fd)
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or
            info.st_nlink != 1 or info.st_mode & 0o022 or
            not info.st_mode & 0o100 or info.st_size > MAX_HELPER):
        os.close(fd)
        fail("available helper is not a safe executable regular file")
    digest, _ = hash_fd(fd)
    return fd, info, digest


@dataclass
class Runtime:
    pid: int
    start: int
    protocol: int
    instance: str
    socket_device: int
    socket_inode: int
    executable_device: int
    executable_inode: int
    digest: str
    executable_fd: int

    def close(self):
        if self.executable_fd >= 0:
            os.close(self.executable_fd)
            self.executable_fd = -1

    def same_process(self) -> bool:
        try:
            if process_start(self.pid) != self.start or process_uid(self.pid) != os.geteuid():
                return False
            current = Path(f"/proc/{self.pid}/exe").stat()
            return (current.st_dev, current.st_ino) == (
                self.executable_device, self.executable_inode)
        except (OSError, ValueError, IndexError, StopIteration, UnicodeError):
            return False


def bind_runtime(state_dir: Path, helper_path: Path) -> Runtime:
    state_fd = open_directory(state_dir, True)
    try:
        protocol = strict_json(read_private(state_fd, "omaq.protocol", MAX_MARKER))
        pid = int(read_private(state_fd, "omaq.pid", 32).decode("ascii").strip())
        sock = os.stat("omaq.sock", dir_fd=state_fd, follow_symlinks=False)
    finally:
        os.close(state_fd)
    if (not isinstance(protocol, dict) or
            set(protocol) != {"pid", "start", "version", "instance", "nonce"}):
        fail("helper protocol marker has an unexpected schema")
    instance = str(protocol.get("instance", ""))
    start_text = str(protocol.get("start", ""))
    version = protocol.get("version")
    if (pid <= 1 or int(protocol.get("pid", -1)) != pid or
            not start_text.isdecimal() or int(start_text) <= 0 or
            type(version) is not int or version < 9 or version > 1024 or
            len(instance) != 32 or
            any(char not in "0123456789abcdef" for char in instance)):
        fail("helper pid/protocol marker mismatch")
    if (not stat.S_ISSOCK(sock.st_mode) or sock.st_uid != os.geteuid() or
            sock.st_mode & 0o077):
        fail("unsafe helper socket")
    if helper_pids(helper_path) != [pid]:
        fail("helper process set does not match the runtime marker")
    start = int(start_text)
    if process_start(pid) != start or process_uid(pid) != os.geteuid():
        fail("helper process identity mismatch")
    executable_fd = os.open(f"/proc/{pid}/exe", os.O_RDONLY | os.O_CLOEXEC)
    try:
        executable = os.fstat(executable_fd)
        if (not stat.S_ISREG(executable.st_mode) or
                executable.st_uid != os.geteuid() or
                executable.st_size > MAX_HELPER):
            fail("running helper image is not a bounded regular file")
        digest, _ = hash_fd(executable_fd)
        current = Path(f"/proc/{pid}/exe").stat()
        if ((current.st_dev, current.st_ino) != (executable.st_dev, executable.st_ino) or
                process_start(pid) != start):
            fail("helper changed while binding its executable")
        return Runtime(pid, start, version, instance, sock.st_dev, sock.st_ino,
                       executable.st_dev, executable.st_ino, digest, executable_fd)
    except BaseException:
        os.close(executable_fd)
        raise


def open_socket(state_dir: Path, runtime: Runtime) -> socket.socket:
    path = state_dir / "omaq.sock"
    before = path.lstat()
    if ((before.st_dev, before.st_ino) !=
            (runtime.socket_device, runtime.socket_inode)):
        fail("helper socket changed before connect")
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        client.settimeout(3.0)
        client.connect(str(path))
        after = path.lstat()
        if (after.st_dev, after.st_ino) != (runtime.socket_device, runtime.socket_inode):
            fail("helper socket changed during connect")
        peer_pid, peer_uid, _ = struct.unpack(
            "3i", client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED,
                                    struct.calcsize("3i")))
        if (peer_pid != runtime.pid or peer_uid != os.geteuid() or
                process_start(peer_pid) != runtime.start or not runtime.same_process()):
            fail("helper socket peer mismatch")
        return client
    except BaseException:
        client.close()
        raise


def await_event(client: socket.socket, runtime: Runtime, request: str,
                names: set[str]):
    deadline = time.monotonic() + 3.0
    buffered = bytearray()
    total = 0
    lines = 0
    while time.monotonic() < deadline and total <= 65536 and lines <= 128:
        client.settimeout(max(0.05, deadline - time.monotonic()))
        chunk = client.recv(4096)
        if not chunk:
            break
        total += len(chunk)
        buffered.extend(chunk)
        while b"\n" in buffered:
            raw, remainder = bytes(buffered).split(b"\n", 1)
            buffered = bytearray(remainder)
            lines += 1
            if len(raw) > 4096:
                fail("oversized helper event")
            event = strict_json(raw)
            if not isinstance(event, dict):
                fail("helper emitted a non-object event")
            if (event.get("event") == "error" and
                    event.get("code") == "unsupported" and
                    names == {"helper.shutdown", "helper.shutdown_blocked"}):
                if set(event) != {"event", "code"}:
                    fail("unexpected unsupported-event schema")
                return event
            if (event.get("event") in names and
                    event.get("instance") == runtime.instance and
                    event.get("request") == request):
                expected = {"event", "instance", "request"}
                if event.get("event") == "helper.shutdown_blocked":
                    expected |= {"reason", "groups"}
                if set(event) != expected:
                    fail("unexpected correlated helper event schema")
                return event
    fail("correlated helper acknowledgement timed out")


def probe(state_dir: Path, runtime: Runtime, client=None):
    owned = client is None
    if owned:
        client = open_socket(state_dir, runtime)
    try:
        request = secrets.token_hex(16)
        client.sendall(json.dumps({"op": "helper.probe", "id": runtime.instance,
                                  "request": request},
                                 separators=(",", ":")).encode("ascii") + b"\n")
        event = await_event(client, runtime, request, {"helper.probe"})
        if not runtime.same_process():
            fail("helper changed after probe")
        return event
    finally:
        if owned:
            client.close()


def atomic_helper_write(helper_dir_fd: int, target: str, data: bytes,
                        expected: str):
    if hashlib.sha256(data).hexdigest() != expected:
        fail("helper write source hash mismatch")
    temporary = f".{target}.{os.getpid()}.{secrets.token_hex(8)}"
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 os.O_CLOEXEC | os.O_NOFOLLOW, 0o600, dir_fd=helper_dir_fd)
    try:
        offset = 0
        while offset < len(data):
            written = os.write(fd, data[offset:])
            if written <= 0:
                fail("short helper write")
            offset += written
        os.fchmod(fd, 0o755)
        os.fsync(fd)
    finally:
        os.close(fd)
    try:
        check = os.open(temporary, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW |
                        os.O_NONBLOCK, dir_fd=helper_dir_fd)
        try:
            if hash_fd(check)[0] != expected:
                fail("temporary helper hash mismatch")
        finally:
            os.close(check)
        os.rename(temporary, target, src_dir_fd=helper_dir_fd,
                  dst_dir_fd=helper_dir_fd)
        os.fsync(helper_dir_fd)
    finally:
        try:
            os.unlink(temporary, dir_fd=helper_dir_fd)
        except FileNotFoundError:
            pass


def atomic_backup(helper_dir_fd: int, runtime: Runtime):
    if not runtime.same_process():
        fail("running helper changed before backup")
    digest, data = hash_fd(runtime.executable_fd)
    if digest != runtime.digest or not runtime.same_process():
        fail("running helper changed while creating backup")
    atomic_helper_write(helper_dir_fd, "omaq.prev", data, runtime.digest)


def restore_available(helper_dir_fd: int):
    fd = os.open("omaq.prev", os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW |
                 os.O_NONBLOCK, dir_fd=helper_dir_fd)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or
                info.st_nlink != 1 or info.st_mode & 0o022 or
                not info.st_mode & 0o100 or info.st_size > MAX_HELPER):
            fail("rollback helper is not a safe executable regular file")
        digest, data = hash_fd(fd)
    finally:
        os.close(fd)
    atomic_helper_write(helper_dir_fd, "omaq", data, digest)
    return digest


def describe(state: str, available: str, runtime=None, detail=None):
    value = {"state": state, "available_sha256": available}
    if runtime:
        value.update({"running_sha256": runtime.digest,
                      "running_protocol": runtime.protocol,
                      "running_pid": runtime.pid})
    if detail:
        value["detail"] = detail
    if JSON_OUTPUT:
        print(json.dumps(value, sort_keys=True))
        return
    print(f"OmaQ helper: {state.replace('-', ' ')}")
    if runtime:
        print(f"  Running:   {runtime.digest} (Protocol {runtime.protocol}, PID {runtime.pid})")
    if available:
        print(f"  Available: {available}")
    if detail:
        print(f"  Detail:    {detail}")


def wait_new_runtime(state_dir: Path, helper_path: Path, expected: str,
                     old: Runtime, seconds=None) -> Runtime:
    if seconds is None:
        seconds = float(os.environ.get("OMAQ_HELPER_RESTART_TIMEOUT", "45"))
        if seconds < 0.5 or seconds > 120:
            fail("invalid helper restart timeout")
    deadline = time.monotonic() + seconds
    last_error = "no runtime marker"
    while time.monotonic() < deadline:
        current = None
        try:
            current = bind_runtime(state_dir, helper_path)
            if current.pid == old.pid and current.start == old.start:
                last_error = "old helper still present"
            elif current.digest != expected:
                last_error = "restarted helper hash differs from available binary"
            else:
                probe(state_dir, current)
                result = current
                current = None
                return result
        except (FileNotFoundError, ConnectionRefusedError, socket.timeout,
                json.JSONDecodeError, OSError, RuntimeError) as error:
            last_error = str(error)
        finally:
            if current is not None:
                current.close()
        time.sleep(0.1)
    fail(f"degraded: helper did not restart with the available binary ({last_error}); "
         "inspect .prev, then run update-helper.sh --rollback")


def open_tree(root: Path) -> tuple[int, int, tuple[int, int, int, int]]:
    root_fd = open_directory(root, False)
    try:
        helper_fd = os.open("helper", os.O_RDONLY | os.O_DIRECTORY |
                            os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=root_fd)
        helper_info = os.fstat(helper_fd)
        if (not stat.S_ISDIR(helper_info.st_mode) or
                helper_info.st_uid != os.geteuid() or
                helper_info.st_mode & 0o022):
            os.close(helper_fd)
            fail("unsafe helper directory")
        root_info = os.fstat(root_fd)
        identity = (root_info.st_dev, root_info.st_ino,
                    helper_info.st_dev, helper_info.st_ino)
        return root_fd, helper_fd, identity
    except BaseException:
        os.close(root_fd)
        raise


def tree_identity(root: Path) -> tuple[int, int, int, int]:
    root_fd, helper_fd, identity = open_tree(root)
    os.close(helper_fd)
    os.close(root_fd)
    return identity


def command(args):
    root = Path(args.root).absolute()
    helper_path = root / "helper/omaq"
    state_dir = Path(args.state).absolute()
    root_fd, helper_dir_fd, identity = open_tree(root)
    available_fd = -1
    if args.root_identity:
        try:
            expected_identity = tuple(int(value) for value in
                                      args.root_identity.split(":"))
        except ValueError:
            fail("invalid expected plugin-root identity")
        if len(expected_identity) != 4 or expected_identity != identity:
            fail("plugin root or helper directory changed during update")
    try:
        if args.action == "restore":
            digest = restore_available(helper_dir_fd)
            describe("available-restored", digest,
                     detail="restored helper/omaq from helper/omaq.prev")
            return 0

        if args.action == "backup":
            if runtime_absent(state_dir, helper_path):
                fail("no running helper image to back up; use the normal first-install build")
            runtime = bind_runtime(state_dir, helper_path)
            try:
                probe(state_dir, runtime)
                try:
                    backup_available_fd, _, available_hash = open_available(helper_dir_fd)
                    os.close(backup_available_fd)
                except FileNotFoundError:
                    available_hash = ""
                atomic_backup(helper_dir_fd, runtime)
                describe("backup-created", available_hash, runtime)
                return 0
            finally:
                runtime.close()

        available_fd, available_info, available_hash = open_available(helper_dir_fd)
        if args.expect_sha256:
            if (len(args.expect_sha256) != 64 or
                    any(char not in "0123456789abcdef" for char in args.expect_sha256) or
                    available_hash != args.expect_sha256):
                fail("available helper does not match the expected build hash")
        if runtime_absent(state_dir, helper_path):
            describe("inactive", available_hash,
                     detail="no running helper; the next start will use the available binary")
            return 0
        runtime = bind_runtime(state_dir, helper_path)
        try:
            probe(state_dir, runtime)
            if runtime.digest == available_hash:
                describe("current", available_hash, runtime)
                return 0
            if args.action == "status":
                describe("update-pending", available_hash, runtime)
                return 0
            if tree_identity(root) != identity:
                fail("plugin root changed before helper activation")
            client = open_socket(state_dir, runtime)
            try:
                probe(state_dir, runtime, client)
                check_fd, check_info, check_hash = open_available(helper_dir_fd)
                try:
                    current_path = os.stat("omaq", dir_fd=helper_dir_fd,
                                           follow_symlinks=False)
                    if (tree_identity(root) != identity or
                            check_hash != available_hash or
                            (check_info.st_dev, check_info.st_ino) !=
                            (available_info.st_dev, available_info.st_ino) or
                            (current_path.st_dev, current_path.st_ino) !=
                            (check_info.st_dev, check_info.st_ino)):
                        fail("available helper changed before shutdown")
                finally:
                    os.close(check_fd)
                request = secrets.token_hex(16)
                client.sendall(json.dumps({
                    "op": "helper.shutdown_if_no_groups",
                    "id": runtime.instance,
                    "request": request,
                }, separators=(",", ":")).encode("ascii") + b"\n")
                result = await_event(client, runtime, request, {
                    "helper.shutdown", "helper.shutdown_blocked"})
            finally:
                client.close()
            if result.get("event") == "error":
                if not runtime.same_process():
                    fail("helper changed after unsupported activation")
                describe("update-pending", available_hash, runtime,
                         "activation_unsupported")
                return 0
            if result.get("event") == "helper.shutdown_blocked":
                groups = result.get("groups")
                reason = result.get("reason")
                if (type(groups) is not int or groups < 0 or groups > 1024 or
                        reason not in {"active_groups", "group_state_uncertain"} or
                        (reason == "active_groups" and groups == 0)):
                    fail("malformed helper shutdown rejection")
                if not runtime.same_process():
                    fail("helper changed after blocking activation")
                describe("update-pending", available_hash, runtime, str(reason))
                return 0
            deadline = time.monotonic() + 7.0
            while time.monotonic() < deadline and runtime.same_process():
                time.sleep(0.05)
            if runtime.same_process():
                fail("helper acknowledged shutdown but did not exit")
            replacement = wait_new_runtime(
                state_dir, helper_path, available_hash, runtime)
            try:
                current_fd, current_info, current_hash = open_available(helper_dir_fd)
                try:
                    current_path = os.stat("omaq", dir_fd=helper_dir_fd,
                                           follow_symlinks=False)
                    if (tree_identity(root) != identity or
                            current_hash != available_hash or
                            (current_info.st_dev, current_info.st_ino) !=
                            (replacement.executable_device, replacement.executable_inode) or
                            (current_path.st_dev, current_path.st_ino) !=
                            (current_info.st_dev, current_info.st_ino)):
                        fail("degraded: available helper changed during activation; "
                             "inspect .prev, then run update-helper.sh --rollback")
                    describe("activated", available_hash, replacement)
                finally:
                    os.close(current_fd)
            finally:
                replacement.close()
            return 0
        finally:
            runtime.close()
    finally:
        if available_fd >= 0:
            os.close(available_fd)
        os.close(helper_dir_fd)
        os.close(root_fd)


def main():
    global JSON_OUTPUT
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("status", "backup", "activate", "restore"))
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    parser.add_argument("--state", default=os.environ.get(
        "OMAQ_STATE", str(Path.home() / ".local/state/omaq")))
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--root-identity", default="")
    parser.add_argument("--expect-sha256", default="")
    args = parser.parse_args()
    JSON_OUTPUT = args.json
    return command(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, UnicodeError, RuntimeError,
            json.JSONDecodeError) as error:
        print(f"omaq-helper-runtime: {error}", file=sys.stderr)
        raise SystemExit(1)
