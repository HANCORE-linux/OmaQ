#!/usr/bin/env python3
"""Integration coverage for fragmented stdin and a blocked helper stdout pipe."""

from __future__ import annotations

import os
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

OLD_URGENT_LIMIT = 4096 * 1024
BATCH_EVENTS = 80
TEST_EVENT_SIZE = 65_500
COMMAND = b'{"op":"status"}\n'
EVENT = b'{"event":"snapshot","protocol":10,"unread":0,"conversations":[],"call":null}\n'
STATUS_NONCE_COMMAND = b'{"op":"status","id":"fresh-status-1"}\n'
STATUS_NONCE_EVENT = b'{"event":"snapshot","protocol":10,"unread":0,"conversations":[],"call":null,"request":"fresh-status-1"}\n'
HISTORY_A_COMMAND = b'{"op":"history","conversation":"7","limit":50,"id":"history-a"}\n'
HISTORY_A_EVENT = b'{"event":"history","conversation":"7","request":"history-a","unread":0,"items":[]}\n'
HISTORY_B_COMMAND = b'{"op":"history","conversation":"7","limit":50,"id":"history-b"}\n'
HISTORY_B_EVENT = b'{"event":"history","conversation":"7","request":"history-b","unread":0,"items":[]}\n'
ESCAPED_HISTORY_REQUEST = b'\\"' * 70
HISTORY_BAD_COMMAND = (
    b'{"op":"history","conversation":"8","limit":50,"id":"'
    + ESCAPED_HISTORY_REQUEST + b'"}\n'
)
HISTORY_BAD_EVENT = (
    b'{"event":"history.failed","conversation":"8","request":"'
    + ESCAPED_HISTORY_REQUEST + b'","code":"history_failed"}\n'
)
MESSAGE_REJECT_COMMAND = b'{"op":"msg.send","conversation":"7","text":"retry-me","id":"message-request-1"}\n'
MESSAGE_REJECT_EVENT = b'{"event":"message.failed","conversation":"7","request":"message-request-1","code":"unsupported","delivered":false}\n'
MESSAGE_MISSING_ID_COMMAND = b'{"op":"msg.send","conversation":"7","text":"missing-id"}\n'
MESSAGE_MISSING_ID_EVENT = b'{"event":"error","code":"request_required","conversation":"7"}\n'
ESCAPED_CONVERSATION = b'\\"' * 79
MESSAGE_ESCAPED_CONV_COMMAND = (
    b'{"op":"msg.send","conversation":"' + ESCAPED_CONVERSATION
    + b'","text":"escaped-conversation","id":"message-request-escaped"}\n'
)
MESSAGE_ESCAPED_CONV_EVENT = (
    b'{"event":"message.failed","conversation":"' + ESCAPED_CONVERSATION
    + b'","request":"message-request-escaped","code":"unsupported","delivered":false}\n'
)
FILE_REJECT_COMMAND = b'{"op":"file.send","conversation":"7","path":"/tmp/missing","id":"request-7"}\n'
FILE_REJECT_EVENTS = (
    b'{"event":"file.failed","id":"","conversation":"7","dir":"out","request":"request-7","code":"unsupported"}\n'
)
GROUP_INVITE_UNSUPPORTED_COMMAND = (
    b'{"op":"invite.create","kind":"group","group":"g:'
    + b'a' * 64
    + b'","id":"0","key":"'
    + b'b' * 64
    + b'","request":"gi-no-tox-1"}\n'
)
GROUP_INVITE_UNSUPPORTED_EVENT = (
    b'{"event":"group.invite.failed","group":"g:'
    + b'a' * 64
    + b'","friend":"0","request":"gi-no-tox-1","code":"unsupported"}\n'
)
NICKNAME_UNSUPPORTED_COMMAND = (
    b'{"op":"nickname.set","nickname":"Offline name","id":"nickname-no-tox"}\n'
)
NICKNAME_UNSUPPORTED_EVENT = (
    b'{"event":"error","code":"unsupported","request":"nickname-no-tox"}\n'
)
IDENTITY_UNSUPPORTED = (
    (b'{"op":"identity.unlock","passphrase":"legacy","id":"identity-unlock-1"}\n',
     b'{"event":"error","code":"unsupported","request":"identity-unlock-1"}\n'),
    (b'{"op":"identity.protect","passphrase":"long-enough","id":"identity-protect-1"}\n',
     b'{"event":"error","code":"unsupported","request":"identity-protect-1"}\n'),
    (b'{"op":"identity.unprotect","passphrase":"legacy","id":"identity-unprotect-1"}\n',
     b'{"event":"error","code":"unsupported","request":"identity-unprotect-1"}\n'),
)
INSTANCE_FIELD = re.compile(br',"instance":"([0-9a-f]{32})"')


def normalize_instances(data: bytes) -> bytes:
    return INSTANCE_FIELD.sub(b"", data)


def test_command(index: int) -> bytes:
    return f'{{"op":"test.emit","id":"{index:06d}"}}\n'.encode()


def test_event(index: int) -> bytes:
    prefix = f'{{"event":"test","id":"{index:06d}","padding":"'.encode()
    return prefix + b"x" * (TEST_EVENT_SIZE - len(prefix) - 2) + b'"}\n'


def wait_for(predicate, timeout: float, message: str) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError(message)


def stop(process: subprocess.Popen[bytes]) -> tuple[bytes, bytes]:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
    stdout = process.stdout.read() if process.stdout else b""
    stderr = process.stderr.read() if process.stderr else b""
    return stdout, stderr


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: ipc-regression.py HELPER", file=sys.stderr)
        return 2
    helper = str(Path(sys.argv[1]).resolve())
    home = tempfile.mkdtemp(prefix="omaq-ipc-home-")
    other_home = tempfile.mkdtemp(prefix="omaq-ipc-other-home-")
    state = tempfile.mkdtemp(prefix="omaq-ipc-state-")
    replay_path = Path(tempfile.mkstemp(prefix="omaq-ipc-replay-")[1])
    env = os.environ.copy()
    env.update(OMAQ_HOME=home, OMAQ_STATE=state)
    first: subprocess.Popen[bytes] | None = None
    second: subprocess.Popen[bytes] | None = None

    try:
        first = subprocess.Popen(
            [helper],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        socket_path = Path(state) / "omaq.sock"
        spool_path = Path(state) / "stdout-critical.spool"
        wait_for(socket_path.exists, 5, "helper socket was not created")
        assert first.stdin is not None

        # A second home must not become a concurrent writer for the same state/spool.
        second_env = env.copy()
        second_env["OMAQ_HOME"] = other_home
        contender = subprocess.run(
            [helper],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            env=second_env,
            timeout=5,
            check=False,
        )
        if contender.returncode != 2:
            raise RuntimeError(
                f"same-state contender returned {contender.returncode}, expected 2: "
                f"{contender.stderr.decode(errors='replace')}"
            )

        # Socket and stdin use the same fail-closed framing for oversize and NUL lines.
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as framing_client:
            framing_client.settimeout(0.25)
            framing_client.connect(str(socket_path))
            framing_client.sendall(b"x" * 4096 + COMMAND)
            framing_client.sendall(COMMAND[:-1] + b"\0garbage\n")
            try:
                unexpected = framing_client.recv(4096)
            except socket.timeout:
                unexpected = b""
            if unexpected:
                raise RuntimeError(f"malformed socket line produced output: {unexpected!r}")
            framing_client.settimeout(5)
            framing_client.sendall(COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if normalize_instances(response) != EVENT or not INSTANCE_FIELD.search(response):
                raise RuntimeError(f"socket framing recovery mismatch: {response!r}")
            framing_client.sendall(FILE_REJECT_COMMAND)
            response = b""
            while response.count(b"\n") < 1:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != FILE_REJECT_EVENTS:
                raise RuntimeError(f"file rejection correlation mismatch: {response!r}")
            for command, expected in IDENTITY_UNSUPPORTED:
                framing_client.sendall(command)
                response = b""
                while b"\n" not in response:
                    chunk = framing_client.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                if response != expected:
                    raise RuntimeError(
                        f"identity rejection correlation mismatch: {response!r}"
                    )
            framing_client.sendall(GROUP_INVITE_UNSUPPORTED_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != GROUP_INVITE_UNSUPPORTED_EVENT:
                raise RuntimeError(
                    f"group invite rejection correlation mismatch: {response!r}"
                )
            framing_client.sendall(NICKNAME_UNSUPPORTED_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != NICKNAME_UNSUPPORTED_EVENT:
                raise RuntimeError(
                    f"nickname rejection correlation mismatch: {response!r}"
                )
            framing_client.sendall(STATUS_NONCE_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if normalize_instances(response) != STATUS_NONCE_EVENT or not INSTANCE_FIELD.search(response):
                raise RuntimeError(f"status nonce correlation mismatch: {response!r}")
            stage_path = Path(home) / "attachments" / ".staging-stage-ipc-1"
            framing_client.sendall(
                b'{"op":"attachment.stage.create","id":"stage-ipc-1"}\n'
            )
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            expected_stage = (
                '{"event":"attachment.stage","request":"stage-ipc-1","path":"'
                + str(stage_path) + '"}\n'
            ).encode()
            if response != expected_stage or not stage_path.is_file():
                raise RuntimeError(f"attachment stage creation mismatch: {response!r}")
            stage_info = stage_path.stat()
            if stat.S_IMODE(stage_info.st_mode) != 0o600 or stage_info.st_uid != os.geteuid():
                raise RuntimeError("attachment stage permissions are unsafe")
            framing_client.sendall(
                ('{"op":"attachment.stage.discard","id":"stage-ipc-1","path":"'
                 + str(stage_path) + '"}\n').encode()
            )
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            expected_stage_discard = (
                b'{"event":"attachment.discarded","request":"stage-ipc-1"}\n'
            )
            if response != expected_stage_discard:
                raise RuntimeError(f"attachment stage discard mismatch: {response!r}")
            wait_for(lambda: not stage_path.exists(), 2, "attachment stage was not discarded")
            image_source = Path(home) / "inspection-source.png"
            image_source.write_bytes(bytes.fromhex(
                "89504e470d0a1a0a0000000d4948445200000001000000010804000000b51c0c02"
                "0000000b4944415478da6364f80f00010501012718e3660000000049454e44"
                "ae426082") + b"TRAILING-PAYLOAD")
            image_source.chmod(0o600)
            inspected_path = Path(home) / "attachments" / "inspect-ipc-1.png"
            pending_path = Path(home) / "attachments" / ".pending-inspect-ipc-1"
            framing_client.sendall(
                ('{"op":"attachment.inspect","id":"inspect-ipc-1","path":"'
                 + str(image_source) + '"}\n').encode()
            )
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            expected_inspection = (
                '{"event":"attachment.inspected","request":"inspect-ipc-1",'
                '"path":"' + str(inspected_path) + '","kind":"image"}\n'
            ).encode()
            if response != expected_inspection or not inspected_path.is_file() or not pending_path.is_file():
                raise RuntimeError(f"attachment inspection mismatch: {response!r}")
            if b"TRAILING-PAYLOAD" in inspected_path.read_bytes():
                raise RuntimeError("attachment inspection retained trailing polyglot bytes")
            framing_client.sendall(
                ('{"op":"attachment.stage.discard","id":"inspect-ipc-1","path":"'
                 + str(inspected_path) + '"}\n').encode()
            )
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            expected_inspection_discard = (
                b'{"event":"attachment.discarded","request":"inspect-ipc-1"}\n'
            )
            if response != expected_inspection_discard:
                raise RuntimeError(f"inspected attachment discard mismatch: {response!r}")
            wait_for(lambda: not inspected_path.exists() and not pending_path.exists(),
                     2, "inspected attachment was not discarded")
            framing_client.sendall(HISTORY_A_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != HISTORY_A_EVENT:
                raise RuntimeError(f"history-a correlation mismatch: {response!r}")
            broken_history = Path(home) / "history" / "8" / "messages.jsonl"
            broken_history.parent.mkdir(parents=True, exist_ok=True)
            broken_history.write_bytes(b'{"id":"valid-prefix"}\n{bad}\n')
            framing_client.sendall(HISTORY_BAD_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != HISTORY_BAD_EVENT:
                raise RuntimeError(f"malformed history did not fail closed: {response!r}")
            framing_client.sendall(MESSAGE_REJECT_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != MESSAGE_REJECT_EVENT:
                raise RuntimeError(f"message rejection correlation mismatch: {response!r}")
            framing_client.sendall(MESSAGE_MISSING_ID_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != MESSAGE_MISSING_ID_EVENT:
                raise RuntimeError(f"missing message id was not rejected: {response!r}")
            framing_client.sendall(MESSAGE_ESCAPED_CONV_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = framing_client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != MESSAGE_ESCAPED_CONV_EVENT:
                raise RuntimeError(f"escaped conversation correlation mismatch: {response!r}")

        # The first command deliberately crosses separate stdin reads.
        first.stdin.write(b'{"op":"sta')
        first.stdin.flush()
        time.sleep(0.05)
        if first.poll() is not None:
            raise RuntimeError("helper exited on a fragmented stdin prefix")
        first.stdin.write(b'tus"}\r\n')
        first.stdin.flush()

        writer_error: list[BaseException] = []

        def produce() -> None:
            try:
                assert first is not None and first.stdin is not None
                first.stdin.write(b"".join(test_command(i) for i in range(BATCH_EVENTS)))
                first.stdin.flush()
            except BaseException as error:  # propagated to the test thread
                writer_error.append(error)

        writer = threading.Thread(target=produce, daemon=True)
        writer.start()
        writer.join(timeout=45)
        if writer.is_alive():
            raise RuntimeError("stdin producer blocked behind the stalled stdout client")
        if writer_error:
            raise RuntimeError(f"stdin producer failed: {writer_error[0]}")
        if first.poll() is not None:
            raise RuntimeError("helper exited while stdout was blocked")
        wait_for(
            lambda: spool_path.exists() and spool_path.stat().st_size > OLD_URGENT_LIMIT,
            10,
            "critical stdout spool did not exceed the former 4 MiB limit",
        )
        if spool_path.stat().st_mode & 0o777 != 0o600:
            raise RuntimeError("critical stdout spool mode is not 0600")

        # A separate IPC client must still receive responses while stdout is stalled.
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(5)
            client.connect(str(socket_path))
            client.sendall(COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if normalize_instances(response) != EVENT or not INSTANCE_FIELD.search(response):
                raise RuntimeError(f"socket client response mismatch: {response!r}")
            client.sendall(HISTORY_B_COMMAND)
            response = b""
            while b"\n" not in response:
                chunk = client.recv(4096)
                if not chunk:
                    break
                response += chunk
            if response != HISTORY_B_EVENT:
                raise RuntimeError(f"history-b correlation mismatch: {response!r}")

        first_stdout, first_stderr = stop(first)
        first = None
        if first_stderr:
            raise RuntimeError(f"first helper diagnostics: {first_stderr.decode(errors='replace')}")
        attachments = Path(home) / "attachments"
        pending_orphan = attachments / ".pending-cleanup-1"
        final_orphan = attachments / "cleanup-1.png"
        staging_orphan = attachments / ".staging-cleanup-2"
        pending_orphan.write_bytes(b"")
        final_orphan.write_bytes(b"orphan")
        staging_orphan.write_bytes(b"partial")
        for orphan in (pending_orphan, final_orphan, staging_orphan):
            orphan.chmod(0o600)

        # Restart with a writable stdout and require replay of the complete FIFO.
        with replay_path.open("wb") as replay:
            second = subprocess.Popen(
                [helper],
                stdin=subprocess.PIPE,
                stdout=replay,
                stderr=subprocess.PIPE,
                env=env,
            )
            # Queue a command immediately; replay mode must defer it until old FIFO drain.
            replay_client: socket.socket | None = None
            deadline = time.monotonic() + 5
            while replay_client is None:
                candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                candidate.settimeout(30)
                try:
                    candidate.connect(str(socket_path))
                    replay_client = candidate
                except (ConnectionRefusedError, FileNotFoundError):
                    candidate.close()
                    if time.monotonic() >= deadline:
                        raise RuntimeError("could not connect during helper replay startup")
                    time.sleep(0.02)
            with replay_client:
                replay_client.sendall(COMMAND)
                response = b""
                while b"\n" not in response:
                    chunk = replay_client.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                if normalize_instances(response) != EVENT or not INSTANCE_FIELD.search(response):
                    raise RuntimeError(f"post-replay socket response mismatch: {response!r}")
            wait_for(
                lambda: spool_path.exists() and spool_path.stat().st_size == 0,
                30,
                "restarted helper did not drain the critical stdout spool",
            )
            if any(path.exists() for path in
                   (pending_orphan, final_orphan, staging_orphan)):
                raise RuntimeError("restart did not clean orphan attachment staging")
            second_stdout, second_stderr = stop(second)
            second = None
            if second_stdout:
                raise RuntimeError("unexpected captured stdout for file-backed replay")
            if second_stderr:
                raise RuntimeError(
                    f"restarted helper diagnostics: {second_stderr.decode(errors='replace')}"
                )

        replayed = replay_path.read_bytes()
        expected = (
            EVENT
            + FILE_REJECT_EVENTS
            + b"".join(expected for _, expected in IDENTITY_UNSUPPORTED)
            + GROUP_INVITE_UNSUPPORTED_EVENT
            + NICKNAME_UNSUPPORTED_EVENT
            + STATUS_NONCE_EVENT
            + expected_stage
            + expected_stage_discard
            + expected_inspection
            + expected_inspection_discard
            + HISTORY_A_EVENT
            + HISTORY_BAD_EVENT
            + MESSAGE_REJECT_EVENT
            + MESSAGE_MISSING_ID_EVENT
            + MESSAGE_ESCAPED_CONV_EVENT
            + EVENT
            + b"".join(test_event(i) for i in range(BATCH_EVENTS))
            + EVENT
            + HISTORY_B_EVENT
            + EVENT
        )
        boundary = first_stdout.rfind(b"\n") + 1
        first_complete = first_stdout[:boundary]
        partial = first_stdout[boundary:]
        normalized_complete = normalize_instances(first_complete)
        normalized_partial = normalize_instances(partial)
        if partial and not expected[len(normalized_complete) :].startswith(normalized_partial):
            raise RuntimeError("old stdout ended with an invalid partial critical record")
        # A fresh helper stdout stream discards the old stream's incomplete JSONL tail.
        combined = first_complete + replayed
        instances = INSTANCE_FIELD.findall(combined)
        if len(instances) != 5 or len(set(instances)) < 2:
            raise RuntimeError(f"helper instance ids missing or not rotated: {instances!r}")
        if normalize_instances(combined) != expected:
            raise RuntimeError(
                f"critical FIFO mismatch: got {len(combined)} bytes, expected {len(expected)}"
            )
        print("ipc-regression: ok")
        return 0
    except Exception as error:
        print(f"ipc-regression: FAIL: {error}", file=sys.stderr)
        return 1
    finally:
        if first is not None:
            stop(first)
        if second is not None:
            stop(second)
        replay_path.unlink(missing_ok=True)
        shutil.rmtree(home, ignore_errors=True)
        shutil.rmtree(other_home, ignore_errors=True)
        shutil.rmtree(state, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
