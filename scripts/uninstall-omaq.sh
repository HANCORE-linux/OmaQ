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
state_dir=${OMAQ_STATE:-"$HOME/.local/state/omaq"}
state_dir=${state_dir%/}
uninstall_marker="$state_dir/omaq.uninstalling"
cleanup_marker() {
  python3 - "$state_dir" <<'PY' 2>/dev/null || true
import os, stat, sys
try:
    directory = os.open(sys.argv[1], os.O_RDONLY | os.O_DIRECTORY |
                        os.O_CLOEXEC | os.O_NOFOLLOW)
    directory_info = os.fstat(directory)
    if (directory_info.st_uid != os.geteuid() or directory_info.st_mode & 0o077):
        raise OSError("unsafe state directory")
    info = os.stat("omaq.uninstalling", dir_fd=directory, follow_symlinks=False)
    if stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and info.st_nlink == 1:
        os.unlink("omaq.uninstalling", dir_fd=directory)
    os.close(directory)
except (FileNotFoundError, OSError):
    pass
PY
}
trap cleanup_marker EXIT

python3 - "$plugin_root/helper/omaq" "$state_dir" "$uninstall_marker" <<'PY'
import json, os, signal, socket, stat, sys, time

helper, state_dir, marker_path = sys.argv[1:]
uid = os.geteuid()
if (not os.path.isabs(state_dir) or os.path.realpath(state_dir) != state_dir or
        marker_path != os.path.join(state_dir, "omaq.uninstalling")):
    raise SystemExit("uninstall-omaq: OMAQ_STATE must be absolute")
try:
    state_fd = os.open(state_dir, os.O_RDONLY | os.O_DIRECTORY |
                       os.O_CLOEXEC | os.O_NOFOLLOW)
except FileNotFoundError:
    raise SystemExit(0)
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
        stale_pid = int(os.read(stale_fd, 33).decode("ascii").strip())
        os.close(stale_fd)
        os.kill(stale_pid, 0)
    except (FileNotFoundError, ProcessLookupError, UnicodeError, ValueError):
        os.unlink(marker_name, dir_fd=state_fd)
        marker_fd = os.open(marker_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                            os.O_CLOEXEC | os.O_NOFOLLOW, 0o600,
                            dir_fd=state_fd)
    except PermissionError:
        raise SystemExit("uninstall-omaq: uninstall marker belongs to a live process")
    else:
        raise SystemExit("uninstall-omaq: another uninstall is still running")
os.write(marker_fd, f"{os.getpid()}\n".encode("ascii"))
os.fsync(marker_fd)
os.close(marker_fd)

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

def process_start(pid):
    raw = open(f"/proc/{pid}/stat", "rb").read(4096).decode("ascii", "strict")
    fields = raw[raw.rfind(")") + 2:].split()
    return fields[19]

def process_uid(pid):
    status = open(f"/proc/{pid}/status", "r", encoding="ascii").read(4096)
    line = next(value for value in status.splitlines() if value.startswith("Uid:"))
    return int(line.split()[1])

try:
    protocol = json.loads(private_read("omaq.protocol", 1024))
    pid = int(private_read("omaq.pid", 32).decode("ascii").strip())
except FileNotFoundError:
    raise SystemExit(0)
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
    if start != marker_start:
        raise OSError("helper start-time mismatch")
    if process_uid(pid) != uid:
        raise OSError("helper uid mismatch")
    expected = os.path.realpath(helper)
    expected_info = os.stat(expected)
    process_info = os.stat(f"/proc/{pid}/exe")
    process_path = os.readlink(f"/proc/{pid}/exe").removesuffix(" (deleted)")
    same_inode = (expected_info.st_dev, expected_info.st_ino) == (
        process_info.st_dev, process_info.st_ino)
    if not same_inode and os.path.realpath(process_path) != expected:
        raise OSError("helper executable mismatch")
except FileNotFoundError:
    raise SystemExit(0)
except (OSError, ValueError, IndexError, StopIteration, UnicodeError) as error:
    raise SystemExit(f"uninstall-omaq: refusing to signal an unverified process: {error}")
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
        current_path = os.readlink(f"/proc/{pid}/exe").removesuffix(" (deleted)")
        return ((current_info.st_dev, current_info.st_ino) ==
                (expected_info.st_dev, expected_info.st_ino) or
                os.path.realpath(current_path) == expected)
    except (OSError, ValueError, IndexError, StopIteration, UnicodeError):
        return False

def wait_stopped(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not same_process():
            return True
        time.sleep(0.05)
    return not same_process()

def await_correlated(client, event_name, request):
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
            if (event.get("event") == event_name and
                    str(event.get("instance", "")) == instance and
                    str(event.get("request", "")) == request):
                return
    raise RuntimeError(f"correlated {event_name} acknowledgement failed")

shutdown_sent = False
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
        await_correlated(client, "helper.probe", request)
        shutdown_request = os.urandom(16).hex()
        payload = json.dumps({"op": "helper.shutdown", "id": instance,
                              "request": shutdown_request},
                             separators=(",", ":")).encode("ascii") + b"\n"
        client.sendall(payload)
        await_correlated(client, "helper.shutdown", shutdown_request)
        shutdown_sent = True
except RuntimeError as error:
    raise SystemExit(f"uninstall-omaq: {error}")
except (OSError, ValueError, json.JSONDecodeError):
    pass
if (not shutdown_sent) or (not wait_stopped(5.0)):
    if same_process():
        os.kill(pid, signal.SIGTERM)
        if not wait_stopped(5.0):
            raise SystemExit("uninstall-omaq: verified helper did not stop")
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
remove_output=$(omarchy plugin remove hancore.omaq --yes)
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
