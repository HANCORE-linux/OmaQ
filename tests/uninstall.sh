#!/bin/sh
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
helper="$root/tests/omaq_ipc_test_helper"
[ -x "$helper" ] || {
  echo "uninstall: test helper is missing" >&2
  exit 1
}

tmp=$(mktemp -d)
trap 'jobs -p | xargs -r kill 2>/dev/null || true; rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir -m 700 "$tmp/bin"
cat >"$tmp/bin/omarchy" <<'SH'
#!/bin/sh
: >"$OMAQ_TEST_OMARCHY_CALLED"
if [ -n "${OMAQ_TEST_REMOVE_ENTERED:-}" ]; then
  : >"$OMAQ_TEST_REMOVE_ENTERED"
  tries=0
  while [ ! -f "$OMAQ_TEST_REMOVE_RELEASE" ]; do
    tries=$((tries + 1))
    [ "$tries" -lt 400 ] || exit 5
    sleep 0.025
  done
fi
[ "$*" = "plugin remove hancore.omaq --yes" ] || exit 2
[ -f "$OMAQ_STATE/omaq.uninstalling" ] || {
  echo "uninstall-test: uninstall marker missing during removal" >&2
  exit 3
}
for marker in omaq.pid omaq.protocol omaq.sock; do
  [ ! -e "$OMAQ_STATE/$marker" ] || {
    echo "uninstall-test: helper runtime marker still exists: $marker" >&2
    exit 4
  }
done
printf 'Removed hancore.omaq. Backup at: %s\n' "$OMAQ_TEST_PLUGIN_BACKUP"
SH
chmod 755 "$tmp/bin/omarchy"

run_case() {
  mode=$1
  case_root="$tmp/$mode"
  plugin="$case_root/plugin"
  home="$case_root/home"
  state="$case_root/state"
  runtime="$case_root/runtime"
  backup="$case_root/backup"
  umask 077
  mkdir -p "$plugin/scripts" "$plugin/helper" "$home" "$state" "$runtime" "$backup"
  cp "$root/scripts/uninstall-omaq.sh" "$plugin/scripts/uninstall-omaq.sh"
  cp "$helper" "$plugin/helper/omaq"
  chmod 755 "$plugin/scripts/uninstall-omaq.sh" "$plugin/helper/omaq"
  printf 'retain me\n' >"$home/private-state"
  chmod 600 "$home/private-state"

  OMAQ_HOME="$home" OMAQ_STATE="$state" \
    "$plugin/helper/omaq" </dev/null >"$case_root/helper.out" 2>"$case_root/helper.err" &
  pid=$!
  tries=0
  while [ ! -S "$state/omaq.sock" ] || [ ! -f "$state/omaq.protocol" ]; do
    tries=$((tries + 1))
    [ "$tries" -lt 200 ] || {
      cat "$case_root/helper.err" >&2
      echo "uninstall: helper did not start" >&2
      exit 1
    }
    sleep 0.025
  done
  PATH="$tmp/bin:$PATH" OMAQ_HOME="$home" OMAQ_STATE="$state" \
    XDG_RUNTIME_DIR="$runtime" OMAQ_TEST_HELPER_PID="$pid" \
    OMAQ_TEST_PLUGIN_BACKUP="$backup" \
    OMAQ_TEST_OMARCHY_CALLED="$case_root/omarchy.called" \
    "$plugin/scripts/uninstall-omaq.sh" --yes >"$case_root/uninstall.out"
  wait "$pid"
  [ -f "$case_root/omarchy.called" ] || {
    echo "uninstall: plugin remover was not called after safe shutdown" >&2
    exit 1
  }
  [ -f "$home/private-state" ] || {
    echo "uninstall: private data was deleted" >&2
    exit 1
  }
  [ ! -e "$state/omaq.uninstalling" ] || {
    echo "uninstall: uninstall marker was not cleaned" >&2
    exit 1
  }
  grep -q 'private and downloaded data was not deleted' "$case_root/uninstall.out"
}

run_case graceful

missing_state_root="$tmp/missing-state"
mkdir -p "$missing_state_root/plugin/scripts" "$missing_state_root/plugin/helper" \
  "$missing_state_root/home" "$missing_state_root/runtime" "$missing_state_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$missing_state_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$missing_state_root/plugin/helper/omaq"
chmod 755 "$missing_state_root/plugin/scripts/uninstall-omaq.sh" \
  "$missing_state_root/plugin/helper/omaq"
PATH="$tmp/bin:$PATH" OMAQ_HOME="$missing_state_root/home" \
  OMAQ_STATE="$missing_state_root/state" XDG_RUNTIME_DIR="$missing_state_root/runtime" \
  OMAQ_TEST_PLUGIN_BACKUP="$missing_state_root/backup" \
  OMAQ_TEST_OMARCHY_CALLED="$missing_state_root/omarchy.called" \
  "$missing_state_root/plugin/scripts/uninstall-omaq.sh" --yes \
  >"$missing_state_root/uninstall.out" 2>"$missing_state_root/uninstall.err"
[ -f "$missing_state_root/omarchy.called" ] || {
  echo "uninstall: missing-state removal did not call plugin remover" >&2
  exit 1
}
[ -d "$missing_state_root/state" ] && [ ! -e "$missing_state_root/state/omaq.uninstalling" ] || {
  echo "uninstall: missing-state serialization failed" >&2
  exit 1
}

run_refused_case() {
  mode=$1
  case_root="$tmp/$mode"
  plugin="$case_root/plugin"
  home="$case_root/home"
  state="$case_root/state"
  runtime="$case_root/runtime"
  backup="$case_root/backup"
  umask 077
  mkdir -p "$plugin/scripts" "$plugin/helper" "$home" "$state" "$runtime" "$backup"
  cp "$root/scripts/uninstall-omaq.sh" "$plugin/scripts/uninstall-omaq.sh"
  cp "$helper" "$plugin/helper/omaq"
  chmod 755 "$plugin/scripts/uninstall-omaq.sh" "$plugin/helper/omaq"
  printf 'retain me\n' >"$home/private-state"
  chmod 600 "$home/private-state"

  safe_mode=
  case "$mode" in
  unsupported | silent | malformed | ack-fail)
    safe_mode=$(printf '%s' "$mode" | tr - _)
    ;;
  esac
  OMAQ_HOME="$home" OMAQ_STATE="$state" \
    OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE="$safe_mode" \
    "$plugin/helper/omaq" </dev/null >"$case_root/helper.out" 2>"$case_root/helper.err" &
  pid=$!
  tries=0
  while [ ! -S "$state/omaq.sock" ] || [ ! -f "$state/omaq.protocol" ]; do
    tries=$((tries + 1))
    [ "$tries" -lt 200 ] || {
      cat "$case_root/helper.err" >&2
      echo "uninstall: refused-case helper did not start" >&2
      exit 1
    }
    sleep 0.025
  done
  case "$mode" in
  active | native-unmapped | uncertain)
    case "$mode" in
    active) fixture_op=test.group.activate ;;
    native-unmapped) fixture_op=test.group.native_unmapped ;;
    uncertain) fixture_op=test.group.uncertain ;;
    esac
    python3 - "$state/omaq.sock" "$fixture_op" <<'PY'
import json, socket, sys
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
    client.settimeout(2)
    client.connect(sys.argv[1])
    client.sendall(json.dumps({"op": sys.argv[2]}, separators=(",", ":")).encode() + b"\n")
    buffered = bytearray()
    while b"\n" not in buffered:
        chunk = client.recv(4096)
        if not chunk:
            break
        buffered.extend(chunk)
    raw = bytes(buffered).split(b"\n", 1)[0]
    event = json.loads(raw)
    if event != {"event": "test.group.active", "groups": 1}:
        raise SystemExit(f"unexpected group fixture response: {event!r}")
PY
    ;;
  unavailable)
    rm -f -- "$state/omaq.sock"
    ;;
  incomplete)
    rm -f -- "$state/omaq.protocol"
    ;;
  missing-binary)
    mv -- "$plugin/helper/omaq" "$plugin/helper/omaq.removed"
    ;;
  replaced-binary)
    cp "$helper" "$plugin/helper/omaq.next"
    chmod 755 "$plugin/helper/omaq.next"
    mv -f -- "$plugin/helper/omaq.next" "$plugin/helper/omaq"
    ;;
  unsupported | silent | malformed | ack-fail)
    ;;
  esac

  set +e
  PATH="$tmp/bin:$PATH" OMAQ_HOME="$home" OMAQ_STATE="$state" \
    XDG_RUNTIME_DIR="$runtime" OMAQ_TEST_HELPER_PID="$pid" \
    OMAQ_TEST_PLUGIN_BACKUP="$backup" \
    OMAQ_TEST_OMARCHY_CALLED="$case_root/omarchy.called" \
    "$plugin/scripts/uninstall-omaq.sh" --yes \
    >"$case_root/uninstall.out" 2>"$case_root/uninstall.err"
  status=$?
  set -e
  [ "$status" -ne 0 ] || {
    echo "uninstall: $mode refusal unexpectedly succeeded" >&2
    exit 1
  }
  kill -0 "$pid" 2>/dev/null || {
    echo "uninstall: $mode refusal stopped the helper" >&2
    exit 1
  }
  if [ "$mode" = ack-fail ]; then
    python3 - "$state/omaq.protocol" "$state/omaq.sock" <<'PY'
import json, socket, sys
instance = str(json.load(open(sys.argv[1], encoding="utf-8"))["instance"])
request = "ack-failure-resume-probe"
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
    client.settimeout(2)
    client.connect(sys.argv[2])
    client.sendall(json.dumps({"op": "helper.probe", "id": instance,
                               "request": request},
                              separators=(",", ":")).encode("ascii") + b"\n")
    buffered = bytearray()
    events = []
    while len(buffered) <= 65536:
        while b"\n" in buffered:
            raw, remainder = bytes(buffered).split(b"\n", 1)
            buffered = bytearray(remainder)
            event = json.loads(raw)
            events.append(event)
            if (event.get("event") == "helper.probe" and
                    event.get("instance") == instance and
                    event.get("request") == request):
                raise SystemExit(0)
        chunk = client.recv(4096)
        if not chunk:
            break
        buffered.extend(chunk)
raise SystemExit(f"resumed helper probe missing: {events!r}")
PY
  fi
  [ ! -e "$case_root/omarchy.called" ] || {
    echo "uninstall: $mode refusal reached plugin removal" >&2
    exit 1
  }
  [ -f "$home/private-state" ] || {
    echo "uninstall: $mode refusal deleted private data" >&2
    exit 1
  }
  [ ! -e "$state/omaq.uninstalling" ] || {
    echo "uninstall: $mode refusal retained uninstall marker" >&2
    exit 1
  }
  case "$mode" in
  active | native-unmapped)
    grep -q 'active private group' "$case_root/uninstall.err"
    ;;
  uncertain)
    grep -q 'helper group state is uncertain' "$case_root/uninstall.err"
    ;;
  unavailable)
    grep -q 'incomplete helper runtime state' "$case_root/uninstall.err"
    ;;
  incomplete)
    grep -q 'incomplete helper runtime state' "$case_root/uninstall.err"
    ;;
  missing-binary | replaced-binary)
    grep -q 'refusing to signal an unverified process' "$case_root/uninstall.err"
    ;;
  unsupported | silent | ack-fail)
    grep -q 'safe helper shutdown unavailable; no signal sent' \
      "$case_root/uninstall.err"
    ;;
  malformed)
    grep -q 'malformed helper shutdown rejection' "$case_root/uninstall.err"
    ;;
  esac
  kill "$pid"
  wait "$pid" 2>/dev/null || true
}

run_refused_case active
run_refused_case native-unmapped
run_refused_case uncertain
run_refused_case unavailable
run_refused_case incomplete
run_refused_case missing-binary
run_refused_case replaced-binary
run_refused_case unsupported
run_refused_case silent
run_refused_case malformed
run_refused_case ack-fail

signal_root="$tmp/ack-fail-signal"
mkdir -p "$signal_root/plugin/scripts" "$signal_root/plugin/helper" \
  "$signal_root/home" "$signal_root/state" "$signal_root/runtime" \
  "$signal_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$signal_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$signal_root/plugin/helper/omaq"
chmod 755 "$signal_root/plugin/scripts/uninstall-omaq.sh" \
  "$signal_root/plugin/helper/omaq"
OMAQ_HOME="$signal_root/home" OMAQ_STATE="$signal_root/state" \
  OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE=ack_fail_signal \
  "$signal_root/plugin/helper/omaq" </dev/null \
  >"$signal_root/helper.out" 2>"$signal_root/helper.err" &
signal_pid=$!
tries=0
while [ ! -S "$signal_root/state/omaq.sock" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || {
    echo "uninstall: signal-race helper did not start" >&2
    exit 1
  }
  sleep 0.025
done
set +e
PATH="$tmp/bin:$PATH" OMAQ_HOME="$signal_root/home" \
  OMAQ_STATE="$signal_root/state" XDG_RUNTIME_DIR="$signal_root/runtime" \
  OMAQ_TEST_PLUGIN_BACKUP="$signal_root/backup" \
  OMAQ_TEST_OMARCHY_CALLED="$signal_root/omarchy.called" \
  "$signal_root/plugin/scripts/uninstall-omaq.sh" --yes \
  >"$signal_root/uninstall.out" 2>"$signal_root/uninstall.err"
signal_status=$?
set -e
[ "$signal_status" -ne 0 ] || {
  echo "uninstall: acknowledgement/signal race unexpectedly removed plugin" >&2
  exit 1
}
wait "$signal_pid"
[ ! -e "$signal_root/omarchy.called" ] || {
  echo "uninstall: acknowledgement/signal race reached plugin removal" >&2
  exit 1
}
[ ! -e "$signal_root/state/omaq.uninstalling" ] || {
  echo "uninstall: acknowledgement/signal race retained uninstall marker" >&2
  exit 1
}
grep -q 'safe helper shutdown unavailable; no signal sent' \
  "$signal_root/uninstall.err"

marker_root="$tmp/live-marker"
mkdir -p "$marker_root/plugin/scripts" "$marker_root/plugin/helper" \
  "$marker_root/home" "$marker_root/state" "$marker_root/runtime" "$marker_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$marker_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$marker_root/plugin/helper/omaq"
chmod 755 "$marker_root/plugin/scripts/uninstall-omaq.sh" "$marker_root/plugin/helper/omaq"
python3 - "$$" "$marker_root/state/omaq.uninstalling" <<'PY'
import os, sys
pid = int(sys.argv[1])
raw = open(f"/proc/{pid}/stat", encoding="ascii").read()
start = raw[raw.rfind(")") + 2:].split()[19]
with open(sys.argv[2], "w", encoding="ascii") as marker:
    marker.write(f"{pid} {os.geteuid()} {start}\n")
PY
chmod 600 "$marker_root/state/omaq.uninstalling"
if PATH="$tmp/bin:$PATH" OMAQ_HOME="$marker_root/home" \
    OMAQ_STATE="$marker_root/state" XDG_RUNTIME_DIR="$marker_root/runtime" \
    OMAQ_TEST_PLUGIN_BACKUP="$marker_root/backup" \
    OMAQ_TEST_OMARCHY_CALLED="$marker_root/omarchy.called" \
    "$marker_root/plugin/scripts/uninstall-omaq.sh" --yes \
    >"$marker_root/uninstall.out" 2>"$marker_root/uninstall.err"; then
  echo "uninstall: concurrent live marker was accepted" >&2
  exit 1
fi
[ -f "$marker_root/state/omaq.uninstalling" ] || {
  echo "uninstall: concurrent live marker was deleted" >&2
  exit 1
}
[ ! -e "$marker_root/omarchy.called" ] || {
  echo "uninstall: concurrent live marker reached plugin removal" >&2
  exit 1
}
grep -q 'another uninstall is still running' "$marker_root/uninstall.err"
printf '%s %s 1\n' "$$" "$(id -u)" >"$marker_root/state/omaq.uninstalling"
PATH="$tmp/bin:$PATH" OMAQ_HOME="$marker_root/home" \
  OMAQ_STATE="$marker_root/state" XDG_RUNTIME_DIR="$marker_root/runtime" \
  OMAQ_TEST_PLUGIN_BACKUP="$marker_root/backup" \
  OMAQ_TEST_OMARCHY_CALLED="$marker_root/stale-omarchy.called" \
  "$marker_root/plugin/scripts/uninstall-omaq.sh" --yes \
  >"$marker_root/stale-uninstall.out" 2>"$marker_root/stale-uninstall.err"
[ -f "$marker_root/stale-omarchy.called" ] || {
  echo "uninstall: stale reused-PID marker blocked removal" >&2
  exit 1
}
[ ! -e "$marker_root/state/omaq.uninstalling" ] || {
  echo "uninstall: stale reused-PID marker was not cleaned" >&2
  exit 1
}

startup_root="$tmp/startup-race"
mkdir -p "$startup_root/plugin/scripts" "$startup_root/plugin/helper" \
  "$startup_root/home" "$startup_root/state" "$startup_root/runtime" \
  "$startup_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$startup_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$startup_root/plugin/helper/omaq"
chmod 755 "$startup_root/plugin/scripts/uninstall-omaq.sh" \
  "$startup_root/plugin/helper/omaq"
printf 'retain me\n' >"$startup_root/home/private-state"
OMAQ_HOME="$startup_root/home" OMAQ_STATE="$startup_root/state" \
  OMAQ_IPC_TEST_IGNORE_UNINSTALL_MARKER=1 \
  OMAQ_IPC_TEST_STARTUP_PHASE=before-lock \
  OMAQ_IPC_TEST_STARTUP_READY="$startup_root/startup.ready" \
  OMAQ_IPC_TEST_STARTUP_RELEASE="$startup_root/startup.release" \
  "$startup_root/plugin/helper/omaq" </dev/null \
  >"$startup_root/helper.out" 2>"$startup_root/helper.err" &
startup_pid=$!
tries=0
while [ ! -f "$startup_root/startup.ready" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || {
    echo "uninstall: helper did not reach startup barrier" >&2
    exit 1
  }
  sleep 0.025
done
PATH="$tmp/bin:$PATH" OMAQ_HOME="$startup_root/home" \
  OMAQ_STATE="$startup_root/state" XDG_RUNTIME_DIR="$startup_root/runtime" \
  OMAQ_TEST_PLUGIN_BACKUP="$startup_root/backup" \
  OMAQ_TEST_OMARCHY_CALLED="$startup_root/omarchy.called" \
  OMAQ_TEST_REMOVE_ENTERED="$startup_root/remove.entered" \
  OMAQ_TEST_REMOVE_RELEASE="$startup_root/remove.release" \
  "$startup_root/plugin/scripts/uninstall-omaq.sh" --yes \
  >"$startup_root/uninstall.out" 2>"$startup_root/uninstall.err" &
uninstall_pid=$!
tries=0
while [ ! -f "$startup_root/remove.entered" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || {
    echo "uninstall: remover did not reach startup-race barrier" >&2
    exit 1
  }
  sleep 0.025
done
[ -f "$startup_root/state/omaq.uninstalling" ] || {
  echo "uninstall: marker missing during startup race" >&2
  exit 1
}
printf 'release\n' >"$startup_root/startup.release"
chmod 600 "$startup_root/startup.release"
set +e
wait "$startup_pid"
startup_status=$?
set -e
[ "$startup_status" -eq 2 ] || {
  echo "uninstall: legacy startup was not rejected by the removal lock" >&2
  exit 1
}
printf 'release\n' >"$startup_root/remove.release"
chmod 600 "$startup_root/remove.release"
wait "$uninstall_pid"
[ -f "$startup_root/omarchy.called" ] || {
  echo "uninstall: startup race did not reach verified plugin removal" >&2
  exit 1
}
[ ! -e "$startup_root/state/omaq.uninstalling" ] || {
  echo "uninstall: startup race retained uninstall marker" >&2
  exit 1
}
[ -f "$startup_root/home/private-state" ] || {
  echo "uninstall: startup race deleted private data" >&2
  exit 1
}

locked_root="$tmp/startup-locked"
mkdir -p "$locked_root/plugin/scripts" "$locked_root/plugin/helper" \
  "$locked_root/home" "$locked_root/state" "$locked_root/runtime" \
  "$locked_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$locked_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$locked_root/plugin/helper/omaq"
chmod 755 "$locked_root/plugin/scripts/uninstall-omaq.sh" \
  "$locked_root/plugin/helper/omaq"
printf 'retain me\n' >"$locked_root/home/private-state"
OMAQ_HOME="$locked_root/home" OMAQ_STATE="$locked_root/state" \
  OMAQ_IPC_TEST_STARTUP_PHASE=after-lock \
  OMAQ_IPC_TEST_STARTUP_READY="$locked_root/startup.ready" \
  OMAQ_IPC_TEST_STARTUP_RELEASE="$locked_root/startup.release" \
  "$locked_root/plugin/helper/omaq" </dev/null \
  >"$locked_root/helper.out" 2>"$locked_root/helper.err" &
locked_pid=$!
tries=0
while [ ! -f "$locked_root/startup.ready" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || {
    echo "uninstall: helper did not reach locked startup barrier" >&2
    exit 1
  }
  sleep 0.025
done
if PATH="$tmp/bin:$PATH" OMAQ_HOME="$locked_root/home" \
    OMAQ_STATE="$locked_root/state" XDG_RUNTIME_DIR="$locked_root/runtime" \
    OMAQ_TEST_PLUGIN_BACKUP="$locked_root/backup" \
    OMAQ_TEST_OMARCHY_CALLED="$locked_root/omarchy.called" \
    "$locked_root/plugin/scripts/uninstall-omaq.sh" --yes \
    >"$locked_root/uninstall.out" 2>"$locked_root/uninstall.err"; then
  echo "uninstall: locked helper startup was accepted" >&2
  exit 1
fi
kill -0 "$locked_pid" 2>/dev/null || {
  echo "uninstall: locked startup refusal stopped helper" >&2
  exit 1
}
[ ! -e "$locked_root/omarchy.called" ] || {
  echo "uninstall: locked startup reached plugin removal" >&2
  exit 1
}
[ ! -e "$locked_root/state/omaq.uninstalling" ] || {
  echo "uninstall: locked startup retained uninstall marker" >&2
  exit 1
}
grep -q 'helper startup is in progress' "$locked_root/uninstall.err"
printf 'release\n' >"$locked_root/startup.release"
chmod 600 "$locked_root/startup.release"
tries=0
while [ ! -S "$locked_root/state/omaq.sock" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || {
    echo "uninstall: helper did not resume after locked refusal" >&2
    exit 1
  }
  sleep 0.025
done
kill "$locked_pid"
wait "$locked_pid" 2>/dev/null || true
[ -f "$locked_root/home/private-state" ] || {
  echo "uninstall: locked startup deleted private data" >&2
  exit 1
}

unsafe_root="$tmp/unsafe"
umask 077
mkdir -p "$unsafe_root/plugin/scripts" "$unsafe_root/plugin/helper" \
  "$unsafe_root/home" "$unsafe_root/state" "$unsafe_root/runtime" \
  "$unsafe_root/backup"
cp "$root/scripts/uninstall-omaq.sh" "$unsafe_root/plugin/scripts/uninstall-omaq.sh"
cp "$helper" "$unsafe_root/plugin/helper/omaq"
chmod 755 "$unsafe_root/plugin/scripts/uninstall-omaq.sh" "$unsafe_root/plugin/helper/omaq"
sleep 60 &
unsafe_pid=$!
unsafe_start=$(python3 - "$unsafe_pid" <<'PY'
import sys
raw = open(f"/proc/{sys.argv[1]}/stat", encoding="ascii").read()
print(raw[raw.rfind(")") + 2:].split()[19])
PY
)
printf '%s\n' "$unsafe_pid" >"$unsafe_root/state/omaq.pid"
printf '{"pid":%s,"start":%s,"version":9,"instance":"00000000000000000000000000000000","nonce":""}\n' \
  "$unsafe_pid" "$unsafe_start" >"$unsafe_root/state/omaq.protocol"
chmod 600 "$unsafe_root/state/omaq.pid" "$unsafe_root/state/omaq.protocol"
python3 - "$unsafe_root/state/omaq.sock" <<'PY' &
import socket, sys, time
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
    server.bind(sys.argv[1])
    server.listen(1)
    time.sleep(60)
PY
unsafe_socket_pid=$!
tries=0
while [ ! -S "$unsafe_root/state/omaq.sock" ]; do
  tries=$((tries + 1))
  [ "$tries" -lt 200 ] || exit 1
  sleep 0.025
done
chmod 600 "$unsafe_root/state/omaq.sock"
PATH="$tmp/bin:$PATH" OMAQ_HOME="$unsafe_root/home" OMAQ_STATE="$unsafe_root/state" \
  XDG_RUNTIME_DIR="$unsafe_root/runtime" \
  OMAQ_TEST_PLUGIN_BACKUP="$unsafe_root/backup" \
  OMAQ_TEST_OMARCHY_CALLED="$unsafe_root/omarchy.called" \
  "$unsafe_root/plugin/scripts/uninstall-omaq.sh" --yes \
  >"$unsafe_root/uninstall.out" 2>"$unsafe_root/uninstall.err"
kill -0 "$unsafe_pid" 2>/dev/null || {
  echo "uninstall: stale runtime cleanup signaled a foreign process" >&2
  exit 1
}
[ -f "$unsafe_root/omarchy.called" ] || {
  echo "uninstall: stale reused-PID runtime blocked removal" >&2
  exit 1
}
kill "$unsafe_pid" "$unsafe_socket_pid"
wait "$unsafe_pid" 2>/dev/null || true
wait "$unsafe_socket_pid" 2>/dev/null || true
[ ! -e "$unsafe_root/state/omaq.uninstalling" ] || {
  echo "uninstall: marker remained after stale runtime cleanup" >&2
  exit 1
}

echo "uninstall: ok"
