#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
source_helper=${1:-$root/tests/omaq_ipc_test_helper}
[ -x "$source_helper" ] || { echo "helper-update: test helper missing" >&2; exit 1; }
case "$source_helper" in
  "$HOME/.config/omarchy/plugins/hancore.omaq/helper/omaq")
    echo "helper-update: refusing live helper" >&2; exit 1 ;;
esac

tmp=$(mktemp -d /tmp/omaq-helper-update-XXXXXX)
plugin=$tmp/plugin
state=$tmp/state
home=$tmp/home
runtime_dir=$tmp/runtime
supervisor=""
unsupported_pid=""
cleanup() {
  if [ -f "$state/omaq.pid" ]; then
    pid=$(cat "$state/omaq.pid" 2>/dev/null || true)
    case "$pid" in ''|*[!0-9]*) ;; *) kill "$pid" 2>/dev/null || true ;; esac
  fi
  [ -z "$supervisor" ] || kill "$supervisor" 2>/dev/null || true
  [ -z "$supervisor" ] || wait "$supervisor" 2>/dev/null || true
  [ -z "$unsupported_pid" ] || kill "$unsupported_pid" 2>/dev/null || true
  [ -z "$unsupported_pid" ] || wait "$unsupported_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

umask 077
mkdir -p "$plugin/helper" "$plugin/scripts" "$state" "$home" "$runtime_dir"
cp "$root/scripts/helper-runtime.py" "$plugin/scripts/helper-runtime.py"
cp "$source_helper" "$plugin/helper/omaq"
chmod 755 "$plugin/scripts/helper-runtime.py" "$plugin/helper/omaq"
old_hash=$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)

# Failed replacement probes close every descriptor-bound runtime candidate
# before retrying, while the successful candidate is returned to its caller.
python3 - "$plugin/scripts/helper-runtime.py" <<'PY'
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location("helper_runtime", sys.argv[1])
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

class Candidate:
    def __init__(self, number):
        self.pid = 100 + number
        self.start = 200 + number
        self.digest = "a" * 64
        self.closed = False
    def close(self):
        self.closed = True

old = Candidate(0)
candidates = []
def bind_runtime(_state, _path):
    candidate = Candidate(len(candidates) + 1)
    candidates.append(candidate)
    return candidate
def probe(_state, _runtime, _client=None):
    if len(candidates) < 3:
        raise RuntimeError("malformed probe")

module.bind_runtime = bind_runtime
module.probe = probe
module.RESTART_TIMEOUT = 1.0
result = module.wait_new_runtime(pathlib.Path("/unused"), pathlib.Path("/unused"),
                                 "a" * 64, old, seconds=1.0)
assert result is candidates[-1] and not result.closed
assert all(candidate.closed for candidate in candidates[:-1])
result.close()
PY

(
  while [ ! -e "$tmp/no-restart" ]; do
    OMAQ_HOME="$home" OMAQ_STATE="$state" OMAQ_PROTOCOL_NONCE=helper-update-test \
      "$plugin/helper/omaq" </dev/null >>"$tmp/helper.out" 2>>"$tmp/helper.err" || true
    sleep 0.05
  done
) &
supervisor=$!

i=0
while [ ! -S "$state/omaq.sock" ] || [ ! -f "$state/omaq.protocol" ]; do
  i=$((i + 1)); [ "$i" -lt 200 ] || { echo "helper-update: helper start timed out" >&2; exit 1; }
  sleep 0.05
done

status=$(python3 "$plugin/scripts/helper-runtime.py" status --root "$plugin" --state "$state" --json)
python3 - "$status" "$old_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "current"
assert value["running_sha256"] == sys.argv[2] == value["available_sha256"]
PY

# A deleted available path must not prevent backup of the still-running image.
mv "$plugin/helper/omaq" "$plugin/helper/omaq.saved"
python3 "$plugin/scripts/helper-runtime.py" backup \
  --root "$plugin" --state "$state" >/dev/null
[ "$(sha256sum "$plugin/helper/omaq.prev" | cut -d' ' -f1)" = "$old_hash" ] || {
  echo "helper-update: deleted-path backup missed the running image" >&2; exit 1;
}
mv "$plugin/helper/omaq.saved" "$plugin/helper/omaq"

# Replace the path first: backup must still copy the actual running image from
# /proc/<pid>/exe rather than the newly available path.
cp "$source_helper" "$plugin/helper/omaq.next"
printf '\0' >>"$plugin/helper/omaq.next"
chmod 755 "$plugin/helper/omaq.next"
mv -f "$plugin/helper/omaq.next" "$plugin/helper/omaq"
new_hash=$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)
[ "$new_hash" != "$old_hash" ]
printf 'do-not-touch\n' >"$tmp/external"
rm -f "$plugin/helper/omaq.prev"
ln -s "$tmp/external" "$plugin/helper/omaq.prev"
python3 "$plugin/scripts/helper-runtime.py" backup --root "$plugin" --state "$state" >/dev/null
[ "$(cat "$tmp/external")" = "do-not-touch" ] && [ ! -L "$plugin/helper/omaq.prev" ] || {
  echo "helper-update: backup followed an existing symlink" >&2; exit 1;
}
[ "$(sha256sum "$plugin/helper/omaq.prev" | cut -d' ' -f1)" = "$old_hash" ] || {
  echo "helper-update: backup did not preserve the running image" >&2; exit 1;
}

pending=$(python3 "$plugin/scripts/helper-runtime.py" status --root "$plugin" --state "$state" --json)
python3 - "$pending" "$old_hash" "$new_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "update-pending"
assert value["running_sha256"] == sys.argv[2]
assert value["available_sha256"] == sys.argv[3]
PY
pid_before_hash_check=$(cat "$state/omaq.pid")
zero_hash=$(printf '%064d' 0)
if python3 "$plugin/scripts/helper-runtime.py" activate --root "$plugin" \
    --state "$state" --expect-sha256 "$zero_hash" \
    >/dev/null 2>&1; then
  echo "helper-update: mismatched expected build hash was accepted" >&2
  exit 1
fi
[ "$(cat "$state/omaq.pid")" = "$pid_before_hash_check" ] || {
  echo "helper-update: hash mismatch stopped the running helper" >&2; exit 1;
}

prev_before_activation=$(sha256sum "$plugin/helper/omaq.prev" | cut -d' ' -f1)
activated=$(python3 "$plugin/scripts/helper-runtime.py" activate --root "$plugin" \
  --state "$state" --expect-sha256 "$new_hash" --json)
python3 - "$activated" "$new_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "activated"
assert value["running_sha256"] == sys.argv[2] == value["available_sha256"]
PY
[ "$(sha256sum "$plugin/helper/omaq.prev" | cut -d' ' -f1)" = "$prev_before_activation" ] || {
  echo "helper-update: activation changed the existing rollback image" >&2; exit 1;
}

# A registered group must defer the next available image without stopping the
# current helper. Resetting the isolated test group permits a later retry.
python3 - "$state/omaq.sock" <<'PY'
import json, socket, sys
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect(sys.argv[1])
client.sendall(b'{"op":"test.group.activate"}\n')
client.settimeout(2)
while True:
    event = json.loads(client.recv(4096).split(b"\n", 1)[0])
    if event.get("event") == "test.group.active":
        break
client.close()
PY
pid_before=$(cat "$state/omaq.pid")
cp "$plugin/helper/omaq" "$plugin/helper/omaq.next"
printf '\0' >>"$plugin/helper/omaq.next"
chmod 755 "$plugin/helper/omaq.next"
mv -f "$plugin/helper/omaq.next" "$plugin/helper/omaq"
blocked_hash=$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)
blocked=$(python3 "$plugin/scripts/helper-runtime.py" activate --root "$plugin" \
  --state "$state" --expect-sha256 "$blocked_hash" --json)
python3 - "$blocked" "$blocked_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "update-pending"
assert value["detail"] == "active_groups"
assert value["available_sha256"] == sys.argv[2]
PY
[ "$(cat "$state/omaq.pid")" = "$pid_before" ] || {
  echo "helper-update: active group did not preserve the running helper" >&2; exit 1;
}
python3 - "$state/omaq.sock" <<'PY'
import json, socket, sys
client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
client.connect(sys.argv[1])
client.sendall(b'{"op":"test.group.cache.reset"}\n')
client.settimeout(2)
while True:
    event = json.loads(client.recv(4096).split(b"\n", 1)[0])
    if event.get("event") == "test.group.active":
        break
client.close()
PY
retried=$(python3 "$plugin/scripts/helper-runtime.py" activate --root "$plugin" \
  --state "$state" --expect-sha256 "$blocked_hash" --json)
python3 - "$retried" "$blocked_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "activated"
assert value["running_sha256"] == sys.argv[2]
PY

# The public wrapper uses the normal Makefile rather than a second build path.
cp "$root/scripts/update-helper.sh" "$plugin/scripts/update-helper.sh"
chmod 755 "$plugin/scripts/update-helper.sh"
cat >"$plugin/Makefile" <<'EOF'
.PHONY: helper
helper:
	@:
EOF
wrapper=$(XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
  "$plugin/scripts/update-helper.sh")
printf '%s\n' "$wrapper" | grep -q 'OmaQ helper: backup created'
printf '%s\n' "$wrapper" | grep -q 'OmaQ helper: current'
if python3 "$plugin/scripts/helper-runtime.py" status --root "$plugin" \
    --state "$state" --root-identity 0:0:0:0 >/dev/null 2>&1; then
  echo "helper-update: replaced plugin-root identity was accepted" >&2
  exit 1
fi
(
  exec 8<"$runtime_dir/omaq-helper-update"
  flock 8
  : >"$tmp/lock-ready"
  sleep 1
) &
lock_holder=$!
while [ ! -e "$tmp/lock-ready" ]; do sleep 0.01; done
if XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
    "$plugin/scripts/update-helper.sh" --status \
    >/dev/null 2>"$tmp/lock.err"; then
  echo "helper-update: concurrent updater lock was accepted" >&2
  exit 1
fi
grep -q 'another helper update is active' "$tmp/lock.err"
wait "$lock_holder"

# The legacy wrapper delegates rollback to the shell-off source updater before
# it takes the helper lock or changes helper/omaq.
cat >"$plugin/scripts/update-omaq.sh" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >"$tmp/rollback-delegated"
exit 37
EOF
chmod 755 "$plugin/scripts/update-omaq.sh"
pid_before_delegation=$(cat "$state/omaq.pid")
available_before_delegation=$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)
set +e
XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
  "$plugin/scripts/update-helper.sh" --rollback >/dev/null 2>&1
delegation_rc=$?
set -e
[ "$delegation_rc" -eq 37 ]
[ "$(cat "$tmp/rollback-delegated")" = "--rollback-helper --yes" ]
[ "$(cat "$state/omaq.pid")" = "$pid_before_delegation" ]
[ "$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)" = \
  "$available_before_delegation" ]

# The runtime restore boundary validates .prev without following links or
# accepting special or writable files.
pid_before_bad_rollback=$(cat "$state/omaq.pid")
mv "$plugin/helper/omaq.prev" "$plugin/helper/omaq.prev.safe"
ln -s "$tmp/external" "$plugin/helper/omaq.prev"
if python3 "$plugin/scripts/helper-runtime.py" restore \
    --root "$plugin" --state "$state" >/dev/null 2>&1; then
  echo "helper-update: rollback followed a symlink" >&2; exit 1
fi
rm -f "$plugin/helper/omaq.prev"
mkfifo "$plugin/helper/omaq.prev"
if python3 "$plugin/scripts/helper-runtime.py" restore \
    --root "$plugin" --state "$state" >/dev/null 2>&1; then
  echo "helper-update: rollback accepted a FIFO" >&2; exit 1
fi
rm -f "$plugin/helper/omaq.prev"
mkdir "$plugin/helper/omaq.prev"
if python3 "$plugin/scripts/helper-runtime.py" restore \
    --root "$plugin" --state "$state" >/dev/null 2>&1; then
  echo "helper-update: rollback accepted a directory" >&2; exit 1
fi
rmdir "$plugin/helper/omaq.prev"
cp "$plugin/helper/omaq.prev.safe" "$plugin/helper/omaq.prev"
chmod 775 "$plugin/helper/omaq.prev"
if python3 "$plugin/scripts/helper-runtime.py" restore \
    --root "$plugin" --state "$state" >/dev/null 2>&1; then
  echo "helper-update: rollback accepted a writable image" >&2; exit 1
fi
[ "$(cat "$state/omaq.pid")" = "$pid_before_bad_rollback" ] || {
  echo "helper-update: rejected rollback stopped the helper" >&2; exit 1;
}
rm -f "$plugin/helper/omaq.prev" "$plugin/helper/omaq.prev.safe"
cp "$source_helper" "$plugin/helper/omaq.prev"
chmod 755 "$plugin/helper/omaq.prev"
restored=$(python3 "$plugin/scripts/helper-runtime.py" restore \
  --root "$plugin" --state "$state" --json)
python3 - "$restored" "$old_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "available-restored"
assert value["available_sha256"] == sys.argv[2]
PY
rollback=$(python3 "$plugin/scripts/helper-runtime.py" activate \
  --root "$plugin" --state "$state" --expect-sha256 "$old_hash" --json)
python3 - "$rollback" "$old_hash" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "activated"
assert value["running_sha256"] == sys.argv[2] == value["available_sha256"]
PY
[ "$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)" = "$old_hash" ]

# Root replacement during make cannot redirect the already descriptor-bound
# runtime doctor or Makefile to code in the replacement tree.
plugin_bound=$tmp/plugin-bound
cat >"$plugin/Makefile" <<EOF
.PHONY: helper
helper:
	@mv -- "$plugin" "$plugin_bound"; mkdir -p "$plugin/helper" "$plugin/scripts"; printf '%s\n' '#!/bin/sh' 'touch "$tmp/replacement-runtime-ran"' 'exit 1' >"$plugin/scripts/helper-runtime.py"; chmod 755 "$plugin/scripts/helper-runtime.py"; printf '%s\n' '.RECIPEPREFIX := >' '.PHONY: helper' 'helper:' '>@touch "$tmp/replacement-make-ran"' >"$plugin/Makefile"; touch "$tmp/bound-make-ran"
EOF
set +e
XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
  "$plugin/scripts/update-helper.sh" >"$tmp/root-swap.out" 2>"$tmp/root-swap.err"
root_swap_rc=$?
set -e
[ "$root_swap_rc" -ne 0 ]
[ -e "$tmp/bound-make-ran" ]
[ ! -e "$tmp/replacement-runtime-ran" ]
[ ! -e "$tmp/replacement-make-ran" ]
rm -rf -- "$plugin"
mv -- "$plugin_bound" "$plugin"

# If the normal build changes helper/omaq and then fails, the wrapper restores
# the descriptor-backed .prev image while leaving the running PID untouched.
pid_before_make_failure=$(cat "$state/omaq.pid")
running_before_make_failure=$(python3 "$plugin/scripts/helper-runtime.py" status \
  --root "$plugin" --state "$state" --json)
running_before_make_failure=$(python3 - "$running_before_make_failure" <<'PY'
import json, sys
print(json.loads(sys.argv[1])["running_sha256"])
PY
)
cat >"$plugin/Makefile" <<'EOF'
.PHONY: helper
helper:
	@cp helper/omaq helper/omaq.failed; printf '\0' >>helper/omaq.failed; chmod 755 helper/omaq.failed; mv -f helper/omaq.failed helper/omaq; false
EOF
set +e
XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
  "$plugin/scripts/update-helper.sh" \
  >"$tmp/make-failed.out" 2>"$tmp/make-failed.err"
make_failed_rc=$?
set -e
[ "$make_failed_rc" -ne 0 ]
grep -q 'build validation failed' "$tmp/make-failed.err"
[ "$(cat "$state/omaq.pid")" = "$pid_before_make_failure" ]
[ "$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)" = "$running_before_make_failure" ] || {
  echo "helper-update: failed build remained available" >&2; exit 1;
}

# A successful make whose output fails the updater's descriptor validation is
# covered by the same synchronous restoration path.
cat >"$plugin/Makefile" <<'EOF'
.PHONY: helper
helper:
	@dd if=/dev/zero of=helper/omaq bs=1M count=17 status=none; chmod 755 helper/omaq
EOF
set +e
XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
  "$plugin/scripts/update-helper.sh" \
  >"$tmp/invalid-build.out" 2>"$tmp/invalid-build.err"
invalid_build_rc=$?
set -e
[ "$invalid_build_rc" -ne 0 ]
grep -q 'build validation failed' "$tmp/invalid-build.err"
[ "$(cat "$state/omaq.pid")" = "$pid_before_make_failure" ]
[ "$(sha256sum "$plugin/helper/omaq" | cut -d' ' -f1)" = "$running_before_make_failure" ] || {
  echo "helper-update: invalid successful build remained available" >&2; exit 1;
}

# A linked plugin root is never traversed by the runtime doctor.
ln -s "$plugin" "$tmp/plugin-link"
if python3 "$plugin/scripts/helper-runtime.py" status \
    --root "$tmp/plugin-link" --state "$state" >/dev/null 2>&1; then
  echo "helper-update: linked plugin root was accepted" >&2
  exit 1
fi

# If Service does not restart the new image, activation must report degraded
# and preserve the manual rollback image instead of claiming success.
cp "$plugin/helper/omaq" "$plugin/helper/omaq.next"
printf '\0' >>"$plugin/helper/omaq.next"
chmod 755 "$plugin/helper/omaq.next"
mv -f "$plugin/helper/omaq.next" "$plugin/helper/omaq"
touch "$tmp/no-restart"
set +e
OMAQ_HELPER_RESTART_TIMEOUT=0.5 \
  python3 "$plugin/scripts/helper-runtime.py" activate \
    --root "$plugin" --state "$state" >"$tmp/degraded.out" 2>"$tmp/degraded.err"
rc=$?
set -e
[ "$rc" -ne 0 ] || { echo "helper-update: missing restart claimed success" >&2; exit 1; }
grep -q 'degraded: helper did not restart' "$tmp/degraded.err"
[ -x "$plugin/helper/omaq.prev" ] || {
  echo "helper-update: degraded activation lost rollback image" >&2; exit 1;
}

# The update wrapper is not a first-install path and never starts make when no
# helper runtime can be bound.
cat >"$plugin/Makefile" <<EOF
.PHONY: helper
helper:
	@touch "$tmp/unexpected-no-runtime-build"
EOF
if XDG_RUNTIME_DIR="$runtime_dir" OMAQ_STATE="$state" \
    "$plugin/scripts/update-helper.sh" >"$tmp/no-runtime.out" 2>"$tmp/no-runtime.err"; then
  echo "helper-update: no-runtime update was accepted" >&2; exit 1
fi
grep -q 'no running helper image' "$tmp/no-runtime.err"
[ ! -e "$tmp/unexpected-no-runtime-build" ] || {
  echo "helper-update: no-runtime update started make" >&2; exit 1;
}

# A probe-capable older helper without the group-safe shutdown operation stays
# running and reports an explicit pending state instead of timing out.
unsupported_plugin=$tmp/unsupported-plugin
unsupported_state=$tmp/unsupported-state
unsupported_home=$tmp/unsupported-home
mkdir -p "$unsupported_plugin/helper" "$unsupported_plugin/scripts" \
  "$unsupported_state" "$unsupported_home"
cp "$root/scripts/helper-runtime.py" "$unsupported_plugin/scripts/helper-runtime.py"
cp "$source_helper" "$unsupported_plugin/helper/omaq"
chmod 755 "$unsupported_plugin/scripts/helper-runtime.py" \
  "$unsupported_plugin/helper/omaq"
OMAQ_HOME="$unsupported_home" OMAQ_STATE="$unsupported_state" \
  OMAQ_PROTOCOL_NONCE=helper-update-unsupported \
  OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE=unsupported \
  "$unsupported_plugin/helper/omaq" </dev/null \
  >"$tmp/unsupported.out" 2>"$tmp/unsupported.err" &
unsupported_pid=$!
i=0
while [ ! -S "$unsupported_state/omaq.sock" ] || \
      [ ! -f "$unsupported_state/omaq.protocol" ]; do
  i=$((i + 1)); [ "$i" -lt 200 ] || {
    echo "helper-update: unsupported helper start timed out" >&2; exit 1;
  }
  sleep 0.05
done
cp "$unsupported_plugin/helper/omaq" "$unsupported_plugin/helper/omaq.next"
printf '\0' >>"$unsupported_plugin/helper/omaq.next"
chmod 755 "$unsupported_plugin/helper/omaq.next"
mv -f "$unsupported_plugin/helper/omaq.next" "$unsupported_plugin/helper/omaq"
unsupported=$(python3 "$unsupported_plugin/scripts/helper-runtime.py" activate \
  --root "$unsupported_plugin" --state "$unsupported_state" --json)
python3 - "$unsupported" "$unsupported_pid" <<'PY'
import json, sys
value = json.loads(sys.argv[1])
assert value["state"] == "update-pending"
assert value["detail"] == "activation_unsupported"
assert value["running_pid"] == int(sys.argv[2])
PY
kill "$unsupported_pid"
wait "$unsupported_pid" 2>/dev/null || true
unsupported_pid=""

echo "helper-update: ok"
