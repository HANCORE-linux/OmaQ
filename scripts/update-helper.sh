#!/bin/bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: update-helper.sh [--activate] [--status] [--rollback]

Build OmaQ's helper with the normal Makefile and report whether the detached
runtime still uses an older binary. --activate requests a group-free safe stop;
Service.qml then starts the newly built helper automatically. --status only
reports versions. --rollback delegates to update-omaq.sh so helper/omaq.prev is
restored only while the Omarchy shell watcher is stopped.
EOF
}

activate=0
status_only=0
rollback=0
while (($#)); do
  case $1 in
    --activate) activate=1 ;;
    --status) status_only=1 ;;
    --rollback) rollback=1 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'update-helper: unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if ((activate + status_only + rollback > 1)); then
  echo "update-helper: --activate, --status, and --rollback are mutually exclusive" >&2
  exit 2
fi

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
runtime="$root/scripts/helper-runtime.py"
[[ -x $runtime ]] || { echo "update-helper: helper runtime tool is not executable" >&2; exit 1; }
if ((rollback)); then
  updater="$root/scripts/update-omaq.sh"
  [[ -x $updater ]] || { echo "update-helper: shell-off updater is unavailable" >&2; exit 1; }
  exec "$updater" --rollback-helper --yes
fi
[[ -r $root/Makefile ]] || { echo "update-helper: Makefile is unavailable" >&2; exit 1; }
command -v flock >/dev/null || { echo "update-helper: flock is unavailable" >&2; exit 1; }

# Serialize cooperative runs outside the replaceable plugin tree. The Python
# boundary additionally rechecks root/helper identities before every phase and
# immediately before any shutdown request.
lock_base=${XDG_RUNTIME_DIR:-/run/user/$UID}
lock_dir="$lock_base/omaq-helper-update"
umask 077
mkdir -p -- "$lock_dir"
python3 - "$lock_dir" <<'PY'
import os, stat, sys
info = os.lstat(sys.argv[1])
if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or
        info.st_mode & 0o077):
    raise SystemExit("update-helper: unsafe updater lock directory")
PY
exec 9<"$lock_dir"
flock -n 9 || { echo "update-helper: another helper update is active" >&2; exit 1; }

# Keep the updater source, Makefile, root, and helper directory that were
# selected under the lock. Root renames cannot redirect these descriptors to a
# replacement tree before its identity checks run.
exec 8<"$root"
exec 7<"$root/helper"
exec 6<"$root/scripts/helper-runtime.py"
exec 5<"$root/Makefile"
root_bound="/proc/$$/fd/8"
runtime_bound="/proc/$$/fd/6"
makefile_bound="/proc/$$/fd/5"
root_identity="$(stat -Lc '%d:%i' -- "/proc/$$/fd/8"):$(stat -Lc '%d:%i' -- "/proc/$$/fd/7")"
common=(--root "$root" --root-identity "$root_identity")

if ((status_only)); then
  exec python3 "$runtime_bound" status "${common[@]}"
fi

# Back up the image that is actually executing. /proc/<pid>/exe remains bound
# to that image even when an earlier build already replaced helper/omaq.
python3 "$runtime_bound" backup "${common[@]}"
current_identity="$(stat -c '%d:%i' -- "$root"):$(stat -c '%d:%i' -- "$root/helper")"
[[ $current_identity == "$root_identity" ]] || {
  echo "update-helper: plugin root changed before build" >&2
  exit 1
}
restore_failed_build() {
  echo "update-helper: build validation failed; restoring the available path from .prev" >&2
  if ! python3 "$runtime_bound" restore "${common[@]}"; then
    echo "update-helper: degraded — automatic path restoration failed; use --rollback after inspecting .prev" >&2
  fi
}

set +e
make --no-print-directory -C "$root_bound" -f "$makefile_bound" helper
make_status=$?
set -e
if ((make_status != 0)); then
  restore_failed_build
  exit "$make_status"
fi

set +e
status_json=$(python3 "$runtime_bound" status "${common[@]}" --json)
status_result=$?
set -e
if ((status_result != 0)); then
  restore_failed_build
  exit "$status_result"
fi
if ! python3 "$runtime_bound" status "${common[@]}"; then
  restore_failed_build
  exit 1
fi
set +e
available_hash=$(python3 - "$status_json" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
hash_value = value.get("available_sha256")
if not isinstance(hash_value, str) or len(hash_value) != 64 or any(
        char not in "0123456789abcdef" for char in hash_value):
    raise SystemExit("update-helper: invalid available helper hash")
print(hash_value)
PY
)
hash_result=$?
set -e
if ((hash_result != 0)); then
  restore_failed_build
  exit "$hash_result"
fi

if ((activate)); then
  cat >&2 <<'EOF'
update-helper: activation briefly takes OmaQ offline. In-flight messages become
delivery_unknown, and active transfers, calls, or invitations fail visibly.
EOF
  python3 "$runtime_bound" activate "${common[@]}" \
    --expect-sha256 "$available_hash"
fi
