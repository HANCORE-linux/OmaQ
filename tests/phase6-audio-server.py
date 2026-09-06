#!/usr/bin/env python3
"""Run an isolated PipeWire/PulseAudio server bound to the parent test."""

from __future__ import annotations

import ctypes
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import time

PR_SET_PDEATHSIG = 1
CONFIG_PATH = Path("/usr/share/pipewire/minimal.conf")
MAX_CONFIG_BYTES = 262_144


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"phase6-audio-server: {message}")


def configure_parent_death(expected_parent: int) -> None:
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM, 0, 0, 0) != 0:
        error = ctypes.get_errno()
        fail(f"prctl(PR_SET_PDEATHSIG) failed: {os.strerror(error)}")
    if os.getppid() != expected_parent:
        fail("parent exited before the death signal was armed")


def validate_runtime(path: Path) -> tuple[int, int]:
    prefixes = (
        "omaq-p6-pulse-",
        "omaq-p6-orphan-pulse-",
        "omaq-p6-server-test-",
    )
    if path.parent != Path("/tmp") or not any(
        path.name.startswith(prefix) and path.name[len(prefix):].isalnum()
        for prefix in prefixes
    ):
        fail("runtime path is outside the dedicated /tmp namespace")
    try:
        info = path.lstat()
    except OSError as error:
        fail(f"cannot inspect runtime directory: {error}")
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid():
        fail("runtime path is not an owned directory")
    if stat.S_IMODE(info.st_mode) != 0o700:
        fail("runtime directory mode is not 0700")
    return info.st_dev, info.st_ino


def remove_runtime(path: Path, identity: tuple[int, int]) -> None:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return
    except OSError:
        return
    if not stat.S_ISDIR(info.st_mode) or (info.st_dev, info.st_ino) != identity:
        return
    shutil.rmtree(path, ignore_errors=True)


def system_executable(name: str) -> str:
    path = Path("/usr/bin") / name
    try:
        info = path.lstat()
    except OSError as error:
        fail(f"cannot inspect {path}: {error}")
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_uid != 0
        or stat.S_IMODE(info.st_mode) & 0o022
        or not os.access(path, os.X_OK)
    ):
        fail(f"{path} is not a trusted executable")
    return str(path)


def private_config(owner: int) -> bytes:
    try:
        info = CONFIG_PATH.lstat()
    except OSError as error:
        fail(f"cannot inspect {CONFIG_PATH}: {error}")
    if (
        not stat.S_ISREG(info.st_mode)
        or info.st_uid != 0
        or stat.S_IMODE(info.st_mode) & 0o022
    ):
        fail("PipeWire minimal config is not a trusted regular file")
    if info.st_size <= 0 or info.st_size > MAX_CONFIG_BYTES:
        fail("PipeWire minimal config size is outside the accepted bound")
    try:
        source = CONFIG_PATH.read_text(encoding="utf-8", errors="strict")
    except (OSError, UnicodeError) as error:
        fail(f"cannot read PipeWire minimal config: {error}")

    jack = "minimal.use-jack-tunnel = true"
    if source.count(jack) != 2:
        fail("PipeWire minimal config has an unexpected jack-tunnel shape")
    source = source.replace(jack, "minimal.use-jack-tunnel = false", 1)

    objects_start = source.find("context.objects = [")
    exec_start = source.find("context.exec = [", objects_start)
    if objects_start < 0 or exec_start < 0:
        fail("PipeWire minimal config has no bounded context.objects section")
    private_objects = """context.objects = [
    { factory = spa-node-factory
        args = {
            factory.name = support.node.driver
            node.name = OmaQ-Test-Dummy-Driver
            node.group = pipewire.omaq-test
            priority.driver = 20000
        }
    }
    { factory = spa-node-factory
        args = {
            factory.name = support.node.driver
            node.name = OmaQ-Test-Freewheel-Driver
            priority.driver = 19000
            node.group = pipewire.omaq-test-freewheel
            node.freewheel = true
        }
    }
]

"""
    source = source[:objects_start] + private_objects + source[exec_start:]

    pulse_marker = '    server.address = [\n        "unix:native"\n    ]\n}'
    if source.count(pulse_marker) != 1:
        fail("PipeWire minimal config has an unexpected PulseAudio address")
    pulse_config = (
        '    server.address = [\n        "unix:native"\n    ]\n'
        f'    server.dbus-name = "org.pulseaudio.Server.OmaQTest{owner}"\n'
        "}"
    )
    return source.replace(pulse_marker, pulse_config, 1).encode("utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        fail("usage: phase6-audio-server.py OWNER_PID RUNTIME_DIR")
    try:
        owner = int(sys.argv[1], 10)
    except ValueError:
        fail("owner PID is invalid")
    if owner <= 1:
        fail("owner PID is outside the accepted range")
    runtime = Path(sys.argv[2])
    if not runtime.is_absolute():
        fail("runtime path must be absolute")

    configure_parent_death(owner)
    runtime_identity = validate_runtime(runtime)
    config_path = runtime / "pipewire.conf"
    config_path.write_bytes(private_config(owner))
    config_path.chmod(0o600)

    pipewire = system_executable("pipewire")
    wireplumber = system_executable("wireplumber")
    environment = os.environ.copy()
    private_directories = {
        "HOME": runtime / "home",
        "XDG_CACHE_HOME": runtime / "cache",
        "XDG_CONFIG_HOME": runtime / "config",
        "XDG_STATE_HOME": runtime / "state",
    }
    for directory in private_directories.values():
        directory.mkdir(mode=0o700)
    environment.update({key: str(path) for key, path in private_directories.items()})
    environment["PIPEWIRE_RUNTIME_DIR"] = str(runtime)
    environment["XDG_RUNTIME_DIR"] = str(runtime)
    stderr_path = runtime / "pipewire.err"
    stderr_stream = stderr_path.open("w+b", buffering=0)
    stderr_path.chmod(0o600)

    supervisor_pid = os.getpid()

    def arm_child_parent_death() -> None:
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(PR_SET_PDEATHSIG, signal.SIGTERM, 0, 0, 0) != 0:
            os._exit(125)
        if os.getppid() != supervisor_pid:
            os._exit(125)

    stopping = False

    def stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    def terminate(process: subprocess.Popen[bytes] | None) -> None:
        if process is None or process.poll() is not None:
            return
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    def error_tail() -> str:
        size = os.fstat(stderr_stream.fileno()).st_size
        stderr_stream.seek(max(0, size - 2048))
        return stderr_stream.read(2048).decode("utf-8", errors="replace").strip()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGHUP, stop)
    pipewire_process: subprocess.Popen[bytes] | None = None
    policy_process: subprocess.Popen[bytes] | None = None

    try:
        pipewire_process = subprocess.Popen(
            [pipewire, "-c", str(config_path)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=stderr_stream,
            env=environment,
            preexec_fn=arm_child_parent_death,
        )
        deadline = time.monotonic() + 5
        native_socket = runtime / "pipewire-0"
        pulse_socket = runtime / "pulse" / "native"
        while not stopping and time.monotonic() < deadline:
            if pipewire_process.poll() is not None:
                fail(f"private PipeWire exited early: {error_tail()}")
            if native_socket.is_socket() and pulse_socket.is_socket():
                break
            time.sleep(0.05)
        else:
            if not stopping:
                fail("private PipeWire sockets did not become ready")
        if stopping:
            return 0

        policy_process = subprocess.Popen(
            [wireplumber, "--profile", "policy"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=stderr_stream,
            env=environment,
            preexec_fn=arm_child_parent_death,
        )
        time.sleep(0.2)
        if policy_process.poll() is not None:
            fail(f"private WirePlumber policy exited early: {error_tail()}")
        ready_path = runtime / "ready"
        ready_path.write_text("ready\n", encoding="ascii")
        ready_path.chmod(0o600)

        while not stopping:
            if pipewire_process.poll() is not None:
                fail(f"private PipeWire exited early: {error_tail()}")
            if policy_process.poll() is not None:
                fail(f"private WirePlumber policy exited early: {error_tail()}")
            time.sleep(0.05)
    finally:
        terminate(policy_process)
        terminate(pipewire_process)
        stderr_stream.close()
        remove_runtime(runtime, runtime_identity)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
