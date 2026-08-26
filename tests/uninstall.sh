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
  if [ "$mode" = fallback ]; then
    rm -f -- "$state/omaq.sock"
  fi

  PATH="$tmp/bin:$PATH" OMAQ_HOME="$home" OMAQ_STATE="$state" \
    XDG_RUNTIME_DIR="$runtime" OMAQ_TEST_HELPER_PID="$pid" \
    OMAQ_TEST_PLUGIN_BACKUP="$backup" \
    "$plugin/scripts/uninstall-omaq.sh" --yes >"$case_root/uninstall.out"
  wait "$pid"
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
run_case fallback

unsafe_root="$tmp/unsafe"
umask 077
mkdir -p "$unsafe_root/plugin/scripts" "$unsafe_root/plugin/helper" \
  "$unsafe_root/home" "$unsafe_root/state" "$unsafe_root/runtime"
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
if PATH="$tmp/bin:$PATH" OMAQ_HOME="$unsafe_root/home" OMAQ_STATE="$unsafe_root/state" \
    XDG_RUNTIME_DIR="$unsafe_root/runtime" \
    "$unsafe_root/plugin/scripts/uninstall-omaq.sh" --yes \
    >"$unsafe_root/uninstall.out" 2>"$unsafe_root/uninstall.err"; then
  echo "uninstall: unverified process was accepted" >&2
  exit 1
fi
kill -0 "$unsafe_pid" 2>/dev/null || {
  echo "uninstall: unverified process was signaled" >&2
  exit 1
}
kill "$unsafe_pid"
wait "$unsafe_pid" 2>/dev/null || true
grep -q 'refusing to signal an unverified process' "$unsafe_root/uninstall.err"
[ ! -e "$unsafe_root/state/omaq.uninstalling" ] || {
  echo "uninstall: marker remained after a refused uninstall" >&2
  exit 1
}

echo "uninstall: ok"
