#!/bin/bash
# Remove the OmaQ plugin without silently deleting private user data.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: uninstall-omaq.sh [--yes]

Unloads and removes the OmaQ plugin. Private data, local state, received files,
optional deployment backups, dependency packages, and any Omarchy plugin backup
are retained and can be inspected or removed manually later.
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
  gum confirm "Remove the OmaQ plugin? Private and downloaded data will be retained." || exit 1
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
import fcntl, json, os, socket, stat, sys, time

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
try:
    if start != marker_start:
        raise OSError("helper start-time mismatch")
    if process_uid(pid) != uid:
        raise OSError("helper uid mismatch")
    expected = os.path.realpath(helper)
    expected_info = os.stat(expected)
    process_info = os.stat(f"/proc/{pid}/exe")
    if (expected_info.st_dev, expected_info.st_ino) != (
            process_info.st_dev, process_info.st_ino):
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
        return (current_info.st_dev, current_info.st_ino) == (
            expected_info.st_dev, expected_info.st_ino)
    except (OSError, ValueError, IndexError, StopIteration, UnicodeError):
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
if [[ -d "$rule_dir" && ! -L "$rule_dir" && -O "$rule_dir" ]]; then
  rm -rf -- "$rule_dir"
fi

cat <<'EOF'

OmaQ was unloaded, but your private and downloaded data was not deleted.
The following paths may remain:
  ~/.local/share/omaq/                 identity, contacts, groups, avatars, history, Ratchet state
  ~/.local/state/omaq/                 preferences, unread state, receipts, surfaces, recovery state
  ~/Downloads/omaq/                    received files
  ~/.local/state/omaq-deploy-backups/  deployment backups, when present

Keep these files if you may reinstall OmaQ or need the identity or chat history.
To permanently erase selected data later, inspect it first:
  ls -la -- "$HOME/.local/share/omaq"
  ls -la -- "$HOME/.local/state/omaq"
  ls -la -- "$HOME/Downloads/omaq"
  ls -la -- "$HOME/.local/state/omaq-deploy-backups"
Then manually run only the corresponding deletion command. Each is independent
and irreversible:
  rm -rf -- "$HOME/.local/share/omaq"
  rm -rf -- "$HOME/.local/state/omaq"
  rm -rf -- "$HOME/Downloads/omaq"
  rm -rf -- "$HOME/.local/state/omaq-deploy-backups"
EOF

if [[ -n $plugin_backup ]]; then
  printf '\nOmarchy retained the plain plugin folder at:\n  %s\n' "$plugin_backup"
  printf 'Inspect it later with:\n  ls -la -- %q\n' "$plugin_backup"
  printf 'Delete it later with:\n  rm -rf -- %q\n' "$plugin_backup"
fi

cat <<'EOF'

Dependency packages are retained because other applications may use them:
  toxcore  libsignal-protocol-c  libpulse  libpng  libjpeg-turbo  libwebp
  ttf-material-symbols-variable  qrencode
The optional verification tool zbar may also remain when it was installed for testing.
Inspect ownership and dependencies first:
  pacman -Qi toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode zbar
Only after confirming that no other application needs them, they can be removed with:
  omarchy pkg drop toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode zbar
EOF

if (( remove_status != 0 )); then
  echo "uninstall-omaq: plugin removal completed, but Omarchy reported a later error" >&2
  exit "$remove_status"
fi
