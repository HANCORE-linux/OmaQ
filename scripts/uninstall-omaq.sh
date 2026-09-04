#!/bin/bash
# Remove the OmaQ plugin without silently deleting private user data.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: uninstall-omaq.sh [--yes]

Unloads and removes the OmaQ plugin. In an interactive run, each remaining OmaQ
data directory is offered separately for removal with No as the default. --yes
retains all data. Dependency cleanup remains a separate manual Pacman command.
EOF
}

assume_yes=0
while (( $# > 0 )); do
  case "$1" in
  --yes | -y)
    assume_yes=1
    ;;
  --help | -h)
    usage
    exit 0
    ;;
  *)
    printf 'uninstall-omaq: unknown option: %s\n' "$1" >&2
    usage >&2
    exit 2
    ;;
  esac
  shift
done

if (( ! assume_yes )); then
  if [[ ! -t 0 || ! -t 1 ]]; then
    echo "uninstall-omaq: refusing to continue without confirmation; pass --yes" >&2
    exit 1
  fi
  gum confirm "Remove the OmaQ plugin? Data cleanup will be offered separately." || exit 1
fi

plugin_root=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd -P)
omarchy_command=$(command -v omarchy) || {
  echo "uninstall-omaq: omarchy command is unavailable" >&2
  exit 1
}
state_dir=${OMAQ_STATE:-"$HOME/.local/state/omaq"}
state_dir=${state_dir%/}
uninstall_marker="$state_dir/omaq.uninstalling"
cleanup_marker() {
  python3 - "$state_dir" "$$" <<'PY' 2>/dev/null || true
import os, stat, sys

def process_start(pid):
    raw = open(f"/proc/{pid}/stat", "rb").read(4096).decode("ascii", "strict")
    fields = raw[raw.rfind(")") + 2:].split()
    return fields[19]

try:
    owner_pid = int(sys.argv[2])
    owner_record = f"{owner_pid} {os.geteuid()} {process_start(owner_pid)}"
    directory = os.open(sys.argv[1], os.O_RDONLY | os.O_DIRECTORY |
                        os.O_CLOEXEC | os.O_NOFOLLOW)
    directory_info = os.fstat(directory)
    if (directory_info.st_uid != os.geteuid() or directory_info.st_mode & 0o077):
        raise OSError("unsafe state directory")
    marker = os.open("omaq.uninstalling", os.O_RDONLY | os.O_CLOEXEC |
                     os.O_NOFOLLOW, dir_fd=directory)
    info = os.fstat(marker)
    content = os.read(marker, 129).decode("ascii", "strict").strip()
    os.close(marker)
    current = os.stat("omaq.uninstalling", dir_fd=directory,
                      follow_symlinks=False)
    if ((info.st_dev, info.st_ino) != (current.st_dev, current.st_ino) or
            not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or
            info.st_nlink != 1 or info.st_mode & 0o077 or
            content != owner_record):
        raise OSError("uninstall marker ownership changed")
    os.unlink("omaq.uninstalling", dir_fd=directory)
    os.close(directory)
except (FileNotFoundError, OSError, UnicodeError, ValueError, IndexError):
    pass
PY
}
trap cleanup_marker EXIT

python3 - "$plugin_root/helper/omaq" "$state_dir" "$uninstall_marker" "$$" <<'PY'
import fcntl, hashlib, json, os, socket, stat, sys, time

helper, state_dir, marker_path, owner_text = sys.argv[1:]
try:
    owner_pid = int(owner_text)
except ValueError:
    raise SystemExit("uninstall-omaq: invalid uninstall owner")
if owner_pid <= 1 or os.getppid() != owner_pid:
    raise SystemExit("uninstall-omaq: uninstall owner mismatch")
uid = os.geteuid()

def process_start(pid):
    raw = open(f"/proc/{pid}/stat", "rb").read(4096).decode("ascii", "strict")
    fields = raw[raw.rfind(")") + 2:].split()
    return fields[19]

owner_start = process_start(owner_pid)
owner_record = f"{owner_pid} {uid} {owner_start}"
if (not os.path.isabs(state_dir) or os.path.realpath(state_dir) != state_dir or
        marker_path != os.path.join(state_dir, "omaq.uninstalling")):
    raise SystemExit("uninstall-omaq: OMAQ_STATE must be absolute")
try:
    state_fd = os.open(state_dir, os.O_RDONLY | os.O_DIRECTORY |
                       os.O_CLOEXEC | os.O_NOFOLLOW)
except FileNotFoundError:
    parent_path, state_name = os.path.split(state_dir)
    if not parent_path or state_name in {"", ".", ".."}:
        raise SystemExit("uninstall-omaq: unsafe missing OMAQ_STATE path")
    try:
        parent_fd = os.open(parent_path, os.O_RDONLY | os.O_DIRECTORY |
                            os.O_CLOEXEC | os.O_NOFOLLOW)
        parent_info = os.fstat(parent_fd)
        if (parent_info.st_uid != uid or parent_info.st_mode & 0o022):
            raise OSError("unsafe OMAQ_STATE parent")
        try:
            os.mkdir(state_name, 0o700, dir_fd=parent_fd)
            os.fsync(parent_fd)
        except FileExistsError:
            pass
        os.close(parent_fd)
        state_fd = os.open(state_dir, os.O_RDONLY | os.O_DIRECTORY |
                           os.O_CLOEXEC | os.O_NOFOLLOW)
    except OSError as error:
        raise SystemExit(f"uninstall-omaq: cannot establish private OMAQ_STATE: {error}")
state_info = os.fstat(state_fd)
if (not stat.S_ISDIR(state_info.st_mode) or state_info.st_uid != uid or
        state_info.st_mode & 0o077):
    raise SystemExit("uninstall-omaq: refusing unsafe OMAQ_STATE directory")
marker_name = "omaq.uninstalling"
try:
    marker_fd = os.open(marker_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                       os.O_CLOEXEC | os.O_NOFOLLOW, 0o600, dir_fd=state_fd)
except FileExistsError:
    marker_info = os.stat(marker_name, dir_fd=state_fd, follow_symlinks=False)
    if (not stat.S_ISREG(marker_info.st_mode) or marker_info.st_uid != uid or
            marker_info.st_nlink != 1 or marker_info.st_mode & 0o077):
        raise SystemExit("uninstall-omaq: unsafe uninstall marker already exists")
    try:
        stale_fd = os.open(marker_name, os.O_RDONLY | os.O_CLOEXEC |
                           os.O_NOFOLLOW, dir_fd=state_fd)
        stale_info = os.fstat(stale_fd)
        if ((stale_info.st_dev, stale_info.st_ino) !=
                (marker_info.st_dev, marker_info.st_ino)):
            raise OSError("uninstall marker changed")
        stale_fields = os.read(stale_fd, 129).decode("ascii", "strict").split()
        os.close(stale_fd)
        if len(stale_fields) != 3:
            raise ValueError("malformed uninstall marker")
        stale_pid, stale_uid, stale_start = int(stale_fields[0]), int(stale_fields[1]), stale_fields[2]
        if stale_pid <= 1 or stale_uid != uid or not stale_start.isdecimal():
            raise ValueError("invalid uninstall marker owner")
        stale_process = os.stat(f"/proc/{stale_pid}")
        if stale_process.st_uid != stale_uid or process_start(stale_pid) != stale_start:
            raise ProcessLookupError("stale uninstall marker owner")
        os.kill(stale_pid, 0)
    except (FileNotFoundError, ProcessLookupError, UnicodeError, ValueError, IndexError):
        os.unlink(marker_name, dir_fd=state_fd)
        marker_fd = os.open(marker_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                            os.O_CLOEXEC | os.O_NOFOLLOW, 0o600,
                            dir_fd=state_fd)
    except PermissionError:
        raise SystemExit("uninstall-omaq: uninstall marker belongs to a live process")
    else:
        raise SystemExit("uninstall-omaq: another uninstall is still running")
os.write(marker_fd, f"{owner_record}\n".encode("ascii"))
os.fsync(marker_fd)
os.close(marker_fd)

def acquire_exclusive_state_lock():
    try:
        lock = os.open("omaq-state.lock", os.O_RDWR | os.O_CREAT |
                       os.O_CLOEXEC | os.O_NOFOLLOW, 0o600,
                       dir_fd=state_fd)
        lock_info = os.fstat(lock)
        if (not stat.S_ISREG(lock_info.st_mode) or lock_info.st_uid != uid or
                lock_info.st_nlink != 1 or lock_info.st_mode & 0o077):
            raise OSError("unsafe helper state lock")
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return lock
    except BlockingIOError:
        raise SystemExit("uninstall-omaq: helper startup is in progress")
    except OSError as error:
        raise SystemExit(f"uninstall-omaq: unsafe helper state lock: {error}")

runtime_names = ("omaq.pid", "omaq.protocol", "omaq.sock")
runtime_present = set()
for runtime_name in runtime_names:
    try:
        os.stat(runtime_name, dir_fd=state_fd, follow_symlinks=False)
    except FileNotFoundError:
        continue
    runtime_present.add(runtime_name)
if not runtime_present:
    state_lock = acquire_exclusive_state_lock()
    for runtime_name in runtime_names:
        try:
            os.stat(runtime_name, dir_fd=state_fd, follow_symlinks=False)
        except FileNotFoundError:
            continue
        raise SystemExit("uninstall-omaq: helper runtime appeared during uninstall")
    raise SystemExit(0)
if runtime_present != set(runtime_names):
    raise SystemExit("uninstall-omaq: incomplete helper runtime state")

def private_read(name, limit):
    fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW,
                 dir_fd=state_fd)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != uid or
                info.st_nlink != 1 or info.st_mode & 0o077):
            raise OSError("unsafe helper marker")
        data = os.read(fd, limit + 1)
        if len(data) > limit:
            raise OSError("oversized helper marker")
        return data
    finally:
        os.close(fd)

def process_uid(pid):
    status = open(f"/proc/{pid}/status", "r", encoding="ascii").read(4096)
    line = next(value for value in status.splitlines() if value.startswith("Uid:"))
    return int(line.split()[1])

def clean_stale_runtime():
    state_lock = acquire_exclusive_state_lock()
    try:
        for runtime_name in runtime_names:
            try:
                runtime_info = os.stat(runtime_name, dir_fd=state_fd,
                                       follow_symlinks=False)
            except FileNotFoundError:
                continue
            if runtime_name == "omaq.sock":
                valid_type = stat.S_ISSOCK(runtime_info.st_mode)
            else:
                valid_type = stat.S_ISREG(runtime_info.st_mode)
            if (not valid_type or runtime_info.st_uid != uid or
                    runtime_info.st_nlink != 1 or runtime_info.st_mode & 0o077):
                raise SystemExit("uninstall-omaq: unsafe stale helper runtime state")
            os.unlink(runtime_name, dir_fd=state_fd)
        os.fsync(state_fd)
    finally:
        os.close(state_lock)

try:
    protocol = json.loads(private_read("omaq.protocol", 1024))
    pid = int(private_read("omaq.pid", 32).decode("ascii").strip())
except FileNotFoundError:
    raise SystemExit("uninstall-omaq: incomplete helper runtime state")
except (OSError, ValueError, json.JSONDecodeError) as error:
    raise SystemExit(f"uninstall-omaq: unsafe helper state: {error}")
instance = str(protocol.get("instance", ""))
marker_start = str(protocol.get("start", ""))
if (pid <= 1 or int(protocol.get("pid", -1)) != pid or
        not marker_start.isdecimal() or int(marker_start) <= 0 or
        len(instance) != 32 or
        any(char not in "0123456789abcdef" for char in instance)):
    raise SystemExit("uninstall-omaq: helper pid/protocol marker mismatch")
try:
    start = process_start(pid)
except (FileNotFoundError, OSError, ValueError, IndexError, UnicodeError) as error:
    try:
        clean_stale_runtime()
    except SystemExit:
        raise SystemExit(f"uninstall-omaq: unsafe helper process state: {error}")
    raise SystemExit(0)


def executable_identity(info):
    return (info.st_dev, info.st_ino, stat.S_IMODE(info.st_mode),
            info.st_uid, info.st_nlink, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns)


def validate_executable(info):
    if (not stat.S_ISREG(info.st_mode) or info.st_uid != uid or
            info.st_nlink != 1 or info.st_mode & 0o022 or
            not info.st_mode & stat.S_IXUSR or info.st_size <= 0 or
            info.st_size > 64 * 1024 * 1024):
        raise OSError("unsafe helper executable")


def executable_digest(path, *, nofollow):
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NONBLOCK
    if nofollow:
        flags |= os.O_NOFOLLOW
    descriptor = os.open(path, flags)
    try:
        before = os.fstat(descriptor)
        validate_executable(before)
        digest = hashlib.sha256()
        remaining = before.st_size
        while remaining:
            chunk = os.read(descriptor, min(remaining, 1024 * 1024))
            if not chunk:
                raise OSError("helper executable was truncated while hashing")
            remaining -= len(chunk)
            digest.update(chunk)
        if os.read(descriptor, 1):
            raise OSError("helper executable grew while hashing")
        after = os.fstat(descriptor)
        if executable_identity(after) != executable_identity(before):
            raise OSError("helper executable changed while hashing")
        if nofollow:
            current = os.stat(path, follow_symlinks=False)
            if executable_identity(current) != executable_identity(after):
                raise OSError("helper executable path changed while hashing")
        return after, digest.digest()
    finally:
        os.close(descriptor)


try:
    if start != marker_start:
        raise OSError("helper start-time mismatch")
    if process_uid(pid) != uid:
        raise OSError("helper uid mismatch")
    if (not os.path.isabs(helper) or os.path.normpath(helper) != helper or
            os.path.realpath(helper) != helper):
        raise OSError("unsafe helper executable path")
    expected_info = os.stat(helper, follow_symlinks=False)
    validate_executable(expected_info)
    process_path = f"/proc/{pid}/exe"
    process_info = os.stat(process_path)
    validate_executable(process_info)
    if (expected_info.st_dev, expected_info.st_ino) != (
            process_info.st_dev, process_info.st_ino):
        # A source-only directory exchange can retain the byte-identical helper
        # process in the previous tree. Bind both files before allowing that
        # expected inode split; changed bytes remain fail-closed.
        expected_info, expected_digest = executable_digest(helper, nofollow=True)
        process_info, process_digest = executable_digest(
            process_path, nofollow=False)
        if expected_digest != process_digest:
            raise OSError("helper executable mismatch")
except (OSError, ValueError, IndexError, StopIteration, UnicodeError) as error:
    try:
        clean_stale_runtime()
    except SystemExit:
        raise SystemExit(
            f"uninstall-omaq: refusing to signal an unverified process: {error}")
    raise SystemExit(0)
socket_path = os.path.join(state_dir, "omaq.sock")
try:
    socket_info = os.lstat(socket_path)
except FileNotFoundError:
    socket_available = False
else:
    if not stat.S_ISSOCK(socket_info.st_mode) or socket_info.st_uid != uid:
        raise SystemExit("uninstall-omaq: refusing unsafe helper socket")
    socket_available = True

def same_process():
    try:
        if process_start(pid) != start or process_uid(pid) != uid:
            return False
        current_info = os.stat(f"/proc/{pid}/exe")
        return executable_identity(current_info) == executable_identity(process_info)
    except (OSError, ValueError, IndexError, StopIteration, UnicodeError):
        return False


def same_available_helper():
    try:
        current = os.stat(helper, follow_symlinks=False)
        return executable_identity(current) == executable_identity(expected_info)
    except OSError:
        return False


def wait_stopped(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not same_process():
            return True
        time.sleep(0.05)
    return not same_process()

def await_correlated(client, event_names, request):
    deadline = time.monotonic() + 2.0
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
                raise RuntimeError("oversized helper event during shutdown")
            event = json.loads(raw)
            if not isinstance(event, dict):
                raise RuntimeError("malformed helper event during shutdown")
            if (event.get("event") in event_names and
                    str(event.get("instance", "")) == instance and
                    str(event.get("request", "")) == request):
                return event
    expected = " or ".join(sorted(event_names))
    raise RuntimeError(f"correlated {expected} acknowledgement failed")

try:
    if not socket_available:
        raise OSError("helper socket is unavailable")
    if not same_process() or not same_available_helper():
        raise OSError("helper executable changed before shutdown")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(socket_path)
        request = os.urandom(16).hex()
        payload = json.dumps({"op": "helper.probe", "id": instance,
                              "request": request},
                             separators=(",", ":")).encode("ascii") + b"\n"
        client.sendall(payload)
        await_correlated(client, {"helper.probe"}, request)
        shutdown_request = os.urandom(16).hex()
        payload = json.dumps({"op": "helper.shutdown_if_no_groups",
                              "id": instance, "request": shutdown_request},
                             separators=(",", ":")).encode("ascii") + b"\n"
        client.sendall(payload)
        result = await_correlated(
            client, {"helper.shutdown", "helper.shutdown_blocked"},
            shutdown_request)
        if result.get("event") == "helper.shutdown_blocked":
            groups = result.get("groups")
            reason = result.get("reason")
            if (type(groups) is not int or groups < 0 or groups > 1024 or
                    (reason == "active_groups" and groups == 0) or
                    reason not in {"active_groups", "group_state_uncertain"}):
                raise RuntimeError("malformed helper shutdown rejection")
            if reason == "active_groups":
                raise SystemExit(
                    f"uninstall-omaq: helper owns {groups} active private group(s); "
                    "leave them before uninstalling")
            raise SystemExit(
                "uninstall-omaq: helper group state is uncertain; "
                "finish group cleanup before uninstalling")
except SystemExit:
    raise
except (OSError, ValueError, json.JSONDecodeError, RuntimeError) as error:
    raise SystemExit(
        f"uninstall-omaq: safe helper shutdown unavailable; no signal sent: {error}")
if not wait_stopped(5.0):
    raise SystemExit("uninstall-omaq: helper did not stop; no signal sent")
deadline = time.monotonic() + 2.0
runtime_names = ("omaq.pid", "omaq.protocol", "omaq.sock")
while time.monotonic() < deadline:
    if not any(os.path.lexists(os.path.join(state_dir, name)) for name in runtime_names):
        break
    time.sleep(0.05)
else:
    raise SystemExit("uninstall-omaq: helper runtime markers were not cleaned")
PY

set +e
remove_output=$(python3 - "$state_dir" "$omarchy_command" <<'PY'
import fcntl, os, stat, subprocess, sys

state_dir, omarchy = sys.argv[1:]
try:
    state_fd = os.open(state_dir, os.O_RDONLY | os.O_DIRECTORY |
                       os.O_CLOEXEC | os.O_NOFOLLOW)
    state_info = os.fstat(state_fd)
    if (state_info.st_uid != os.geteuid() or state_info.st_mode & 0o077):
        raise OSError("unsafe state directory")
    lock_fd = os.open("omaq-state.lock", os.O_RDWR | os.O_CLOEXEC |
                      os.O_NOFOLLOW, dir_fd=state_fd)
    lock_info = os.fstat(lock_fd)
    if (not stat.S_ISREG(lock_info.st_mode) or
            lock_info.st_uid != os.geteuid() or lock_info.st_nlink != 1 or
            lock_info.st_mode & 0o077):
        raise OSError("unsafe helper state lock")
    fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
except (BlockingIOError, OSError) as error:
    raise SystemExit(f"uninstall-omaq: cannot lock helper startup during removal: {error}")
result = subprocess.run(
    [omarchy, "plugin", "remove", "hancore.omaq", "--yes"],
    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, check=False)
sys.stdout.buffer.write(result.stdout)
sys.stdout.buffer.flush()
raise SystemExit(result.returncode)
PY
)
remove_status=$?
set -e
printf '%s\n' "$remove_output"
plugin_backup=$(printf '%s\n' "$remove_output" |
  sed -n 's/^Removed hancore\.omaq\. Backup at: \(.*\)$/\1/p' | tail -n 1)
if ! printf '%s\n' "$remove_output" |
     grep -Eq '^(Removed|Unlinked) hancore\.omaq(\.|$)'; then
  if (( remove_status != 0 )); then
    echo "uninstall-omaq: plugin removal failed before completion" >&2
    exit "$remove_status"
  fi
  echo "uninstall-omaq: remover returned success without a completion marker" >&2
  exit 1
fi

cleanup_marker
trap - EXIT
runtime_root=${XDG_RUNTIME_DIR:-"/tmp/omaq-runtime-${UID}"}
rule_dir="$runtime_root/omaq-hypr"
if ! python3 - "$rule_dir" <<'PY'
import os
import re
import stat
import sys

rule_dir = sys.argv[1]
if (not os.path.isabs(rule_dir) or os.path.normpath(rule_dir) != rule_dir or
        os.path.realpath(rule_dir) != rule_dir):
    raise SystemExit("uninstall-omaq: unsafe runtime rule path")
parent, leaf = os.path.split(rule_dir)
allowed = re.compile(
    r"rules(?:-v2)?\.[0-9a-f]{64}(?:\.lock|\.tmp\.[A-Za-z0-9]{6})?"
    r"|rules\.[0-9a-f]{64}\.watch\.lock"
)
flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW


def remove_rule_dir():
    try:
        parent_fd = os.open(parent, flags)
    except FileNotFoundError:
        return
    try:
        parent_info = os.fstat(parent_fd)
        if (parent_info.st_uid != os.geteuid() or
                stat.S_IMODE(parent_info.st_mode) & 0o077):
            raise OSError("unsafe runtime root ownership or mode")
        try:
            rule_fd = os.open(leaf, flags, dir_fd=parent_fd)
        except FileNotFoundError:
            return
        try:
            rule_info = os.fstat(rule_fd)
            if (rule_info.st_uid != os.geteuid() or
                    stat.S_IMODE(rule_info.st_mode) & 0o077):
                raise OSError("unsafe rule directory ownership or mode")
            entries = os.listdir(rule_fd)
            for name in entries:
                if not allowed.fullmatch(name):
                    raise OSError(f"unexpected rule entry: {name}")
                entry = os.stat(name, dir_fd=rule_fd, follow_symlinks=False)
                if (not stat.S_ISREG(entry.st_mode) or
                        entry.st_uid != os.geteuid() or entry.st_nlink != 1):
                    raise OSError(f"unsafe rule entry: {name}")
            for name in entries:
                os.unlink(name, dir_fd=rule_fd)
            os.fsync(rule_fd)
            if os.listdir(rule_fd):
                raise OSError("rule directory changed during cleanup")
            try:
                current = os.stat(leaf, dir_fd=parent_fd,
                                  follow_symlinks=False)
            except FileNotFoundError as error:
                raise OSError("rule directory changed during cleanup") from error
            if ((current.st_dev, current.st_ino) !=
                    (rule_info.st_dev, rule_info.st_ino)):
                raise OSError("rule directory changed during cleanup")
            os.rmdir(leaf, dir_fd=parent_fd)
            os.fsync(parent_fd)
        finally:
            os.close(rule_fd)
    finally:
        os.close(parent_fd)


try:
    remove_rule_dir()
except OSError as error:
    raise SystemExit(f"uninstall-omaq: unsafe runtime rule layout: {error}")
PY
then
  echo "uninstall-omaq: runtime rule cleanup refused; inspect $rule_dir manually" >&2
fi

safe_delete_directory() {
  PATH=/usr/bin:/bin /usr/bin/python3 - "$1" "$HOME" <<'PY'
import os
import shutil
import stat
import sys

path, home = sys.argv[1:]
uid = os.geteuid()
if (not os.path.isabs(path) or os.path.normpath(path) != path or
        os.path.realpath(path) != path or os.path.realpath(home) != home or
        path in {"/", home}):
    raise SystemExit("uninstall-omaq: refusing unsafe data path")
parent, leaf = os.path.split(path)
if not leaf or leaf in {".", ".."}:
    raise SystemExit("uninstall-omaq: refusing unsafe data path")
flags = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW
parent_fd = os.open(parent, flags)
try:
    parent_info = os.fstat(parent_fd)
    if parent_info.st_uid != uid or stat.S_IMODE(parent_info.st_mode) & 0o022:
        raise OSError("unsafe data parent")
    entry = os.stat(leaf, dir_fd=parent_fd, follow_symlinks=False)
    if not stat.S_ISDIR(entry.st_mode) or entry.st_uid != uid:
        raise OSError("data path is not an owner-controlled directory")
    quarantine = f".{leaf}.omaq-delete.{os.getpid()}.{os.urandom(8).hex()}"
    os.rename(leaf, quarantine, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
    moved = os.stat(quarantine, dir_fd=parent_fd, follow_symlinks=False)
    if (moved.st_dev, moved.st_ino) != (entry.st_dev, entry.st_ino):
        try:
            os.rename(quarantine, leaf, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        finally:
            raise OSError("data path changed before deletion")
    quarantine_path = os.path.join(parent, quarantine)
    try:
        if not shutil.rmtree.avoids_symlink_attacks:
            raise OSError("safe recursive deletion is unavailable")
        def mount_id(descriptor):
            metadata = os.open(
                f"/proc/self/fdinfo/{descriptor}",
                os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW,
            )
            try:
                raw = os.read(metadata, 4097)
                if len(raw) > 4096:
                    raise OSError("oversized descriptor metadata")
            finally:
                os.close(metadata)
            values = []
            for line in raw.splitlines():
                if line.startswith(b"mnt_id:\t"):
                    value = line.removeprefix(b"mnt_id:\t")
                    if not value.isdigit():
                        raise OSError("malformed mount identity")
                    values.append(int(value))
            if len(values) != 1:
                raise OSError("missing or ambiguous mount identity")
            return values[0]

        entries = 0
        root_mount = None
        for _root, directories, files, directory_fd in os.fwalk(
                quarantine_path, topdown=True, follow_symlinks=False):
            directory = os.fstat(directory_fd)
            current_mount = mount_id(directory_fd)
            if root_mount is None:
                root_mount = current_mount
            if (not stat.S_ISDIR(directory.st_mode) or directory.st_uid != uid or
                    directory.st_dev != entry.st_dev or current_mount != root_mount or
                    stat.S_IMODE(directory.st_mode) & 0o022 or
                    (stat.S_IMODE(directory.st_mode) & 0o700) != 0o700):
                raise OSError("unsafe, mounted, or non-removable data subdirectory")
            for name in directories + files:
                child = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
                child_fd = os.open(
                    name, os.O_PATH | os.O_CLOEXEC | os.O_NOFOLLOW,
                    dir_fd=directory_fd,
                )
                try:
                    child_mount = mount_id(child_fd)
                finally:
                    os.close(child_fd)
                if child.st_uid != uid:
                    raise OSError("data tree contains an entry owned by another user")
                if child.st_dev != entry.st_dev or child_mount != root_mount:
                    raise OSError("data tree crosses a filesystem or mount boundary")
                entries += 1
                if entries > 1000000:
                    raise OSError("data tree exceeds the cleanup entry limit")
        shutil.rmtree(quarantine_path)
        os.fsync(parent_fd)
    except Exception:
        if os.path.lexists(quarantine_path) and not os.path.lexists(path):
            os.rename(quarantine, leaf, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
            os.fsync(parent_fd)
        raise
finally:
    os.close(parent_fd)
PY
}

cleanup_paths=()
cleanup_labels=()
protected_paths=()
declare -A cleanup_seen=()
record_protected_path() {
  local protected=$1
  [[ -n $protected ]] || return 0
  [[ -z ${cleanup_seen["protected:$protected"]+set} ]] || return 0
  cleanup_seen["protected:$protected"]=1
  protected_paths+=("$protected")
}
protect_cleanup_candidate() {
  local candidate=$1 parent leaf physical physical_parent
  while [[ $candidate == */ && $candidate != / ]]; do
    candidate=${candidate%/}
  done
  [[ -e $candidate || -L $candidate ]] || return 0
  parent=${candidate%/*}
  leaf=${candidate##*/}
  [[ -n $parent && -n $leaf ]] || return 0
  physical_parent=$(unset CDPATH; cd -- "$parent" 2>/dev/null && pwd -P) || return 0
  record_protected_path "$physical_parent/$leaf"
  if physical=$(unset CDPATH; cd -- "$candidate" 2>/dev/null && pwd -P); then
    record_protected_path "$physical"
  fi
}
add_cleanup_directory() {
  local path=${1%/} label=$2 physical
  [[ -n $path && $path == /* && $path != / && $path != "$HOME" &&
      $path != *$'\n'* ]] || {
    protect_cleanup_candidate "$path"
    [[ ! -e $path && ! -L $path ]] ||
      printf 'Retained unsafe or unsupported path: %q\n' "$path" >&2
    return
  }
  [[ -d $path && ! -L $path ]] || {
    protect_cleanup_candidate "$path"
    [[ ! -e $path && ! -L $path ]] ||
      printf 'Retained non-directory or symlink path: %q\n' "$path" >&2
    return
  }
  physical=$(unset CDPATH; cd -- "$path" 2>/dev/null && pwd -P) || {
    protect_cleanup_candidate "$path"
    printf 'Retained inaccessible path: %q\n' "$path" >&2
    return
  }
  [[ $physical == "$path" ]] || {
    protect_cleanup_candidate "$path"
    printf 'Retained non-canonical path: %q\n' "$path" >&2
    return
  }
  [[ -z ${cleanup_seen[$path]+set} ]] || return 0
  cleanup_seen[$path]=1
  cleanup_paths+=("$path")
  cleanup_labels+=("$label")
}

state_home=${XDG_STATE_HOME:-"$HOME/.local/state"}
download_dir=${OMAQ_DOWNLOAD_DIR:-}
[[ $download_dir == /* ]] || download_dir=${XDG_DOWNLOAD_DIR:-}
[[ $download_dir == /* ]] || download_dir="$HOME/Downloads"
if (( ! assume_yes && remove_status == 0 )); then
  add_cleanup_directory "${OMAQ_HOME:-"$HOME/.local/share/omaq"}" \
    "private identity, contacts, groups, avatars, history, and Ratchet state"
  add_cleanup_directory "$download_dir/omaq" "received files"
  add_cleanup_directory "$state_home/omaq-deploy-backups" "deployment backups"
  add_cleanup_directory "$state_home/omaq-source-updates" "retained source-update trees"
  add_cleanup_directory "$HOME/.omaq-source-install" "retained source checkout"
  [[ -z $plugin_backup ]] ||
    add_cleanup_directory "$plugin_backup" "Omarchy plugin backup"
  add_cleanup_directory "$state_dir" \
    "preferences, unread state, receipts, surfaces, and recovery state"
fi

if (( remove_status != 0 )); then
  echo "uninstall-omaq: plugin removal completed, but Omarchy reported a later error" >&2
  echo "uninstall-omaq: all data was retained and dependency packages were untouched" >&2
  exit "$remove_status"
fi

selected_paths=()
declined_paths=()
if (( ! assume_yes )); then
  for index in "${!cleanup_paths[@]}"; do
    path=${cleanup_paths[$index]}
    label=${cleanup_labels[$index]}
    printf -v display_path '%q' "$path"
    if gum confirm --default=false \
        "Permanently delete $display_path ($label)?"; then
      selected_paths+=("$path")
    else
      declined_paths+=("$path")
      printf 'Retained: %q\n' "$path"
    fi
  done
else
  echo "uninstall-omaq: --yes retains all private data, downloads, and backups"
fi

# A confirmed parent must never override No for a nested data directory.
deletable_paths=()
for path in "${selected_paths[@]}"; do
  blocked=0
  for retained in "${declined_paths[@]}" "${protected_paths[@]}"; do
    if [[ $retained == "$path" || $retained == "$path"/* ]]; then
      blocked=1
      break
    fi
  done
  if (( blocked )); then
    printf 'Retained: %q (contains a declined or protected data directory)\n' "$path"
  else
    deletable_paths+=("$path")
  fi
done
selected_paths=()
for path in "${deletable_paths[@]}"; do
  covered=0
  for parent in "${deletable_paths[@]}"; do
    if [[ $path != "$parent" && $path == "$parent"/* ]]; then
      covered=1
      break
    fi
  done
  (( covered )) || selected_paths+=("$path")
done

cleanup_failed=0
for path in "${selected_paths[@]}"; do
  if safe_delete_directory "$path"; then
    printf 'Deleted: %q\n' "$path"
  else
    parent=${path%/*}
    leaf=${path##*/}
    printf 'uninstall-omaq: deletion failed for %q; cleanup may be partial; inspect that path and %q for a hidden .%s.omaq-delete.* remainder\n' \
      "$path" "$parent" "$leaf" >&2
    cleanup_failed=1
    break
  fi
done

cat <<'EOF'

OmaQ was unloaded. Unselected data was retained.
Dependency packages are never removed automatically. If you have verified that
none were installed before OmaQ and no other application needs them, the
non-recursive removal command is:

  sudo pacman -R toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode
EOF
(( cleanup_failed == 0 )) || exit 1
