#!/bin/sh
# float-omaq.sh: classic rule generation is bounded and focus moves by address.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
focus_hooks=$(grep -c 'function onFocusRequestTickChanged()' "$root/ChatSurface.qml")
[ "$focus_hooks" -eq 1 ] || {
  echo "float-script: expected exactly one existing-chat focus hook" >&2
  exit 1
}
tmp=$(mktemp -d /tmp/omaq-float-test-XXXXXX)
socket_pid=""
first_watcher=""
second_watcher=""
cleanup() {
  [ -n "$first_watcher" ] && kill "$first_watcher" 2>/dev/null || true
  [ -n "$second_watcher" ] && kill "$second_watcher" 2>/dev/null || true
  [ -n "$socket_pid" ] && kill "$socket_pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT
mkdir -p "$tmp/bin" "$tmp/home/.config/hypr" "$tmp/runtime"
printf 'source test\n' >"$tmp/home/.config/hypr/hyprland.conf"
cat >"$tmp/bin/hyprctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$OMAQ_FLOAT_TEST_LOG"
if [ "$1" = "dispatch" ] && [ "${2:-}" = "hl.dsp" ]; then
  if [ "${OMAQ_FLOAT_LUA:-0}" = "1" ]; then
    echo "expected a dispatcher"
  else
    echo "classic dispatcher"
  fi
  exit 1
fi
if [ "$1" = "-j" ] && [ "${2:-}" = "clients" ]; then
  if [ "${OMAQ_FLOAT_NO_CLIENTS:-0}" = "1" ]; then
    printf '%s\n' '[]'
  else
    printf '%s\n' '[{"title":"OmaQ chat — Alice · 0","address":"0xabc"}]'
  fi
elif [ "$1" = "-j" ] && [ "${2:-}" = "activeworkspace" ]; then
  if [ "${OMAQ_FLOAT_SPECIAL:-0}" = "1" ]; then
    printf '%s\n' '{"id":-99,"name":"special:scratchpad"}'
  else
    printf '%s\n' '{"id":7,"name":"7"}'
  fi
fi
exit 0
EOF
chmod 755 "$tmp/bin/hyprctl"
cat >"$tmp/bin/socat" <<'EOF'
#!/bin/sh
[ -n "${OMAQ_FLOAT_SOCAT_LOG:-}" ] && printf '%s\n' "$$" >>"$OMAQ_FLOAT_SOCAT_LOG"
printf '%s\n' 'configreloaded>>'
if [ "${OMAQ_FLOAT_WATCH_BLOCK:-0}" = "1" ]; then
  sleep 30
elif [ "${OMAQ_FLOAT_WATCH_HOLD:-0}" = "1" ]; then
  sleep 0.5
fi
EOF
chmod 755 "$tmp/bin/socat"
export PATH="$tmp/bin:$PATH"
export HOME="$tmp/home"
export XDG_RUNTIME_DIR="$tmp/runtime"
export HYPRLAND_INSTANCE_SIGNATURE="test-instance"
export OMAQ_FLOAT_TEST_LOG="$tmp/hypr.log"

"$root/scripts/float-omaq.sh" &
first_pid=$!
"$root/scripts/float-omaq.sh" &
second_pid=$!
wait "$first_pid"
wait "$second_pid"
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 2 ] || {
  echo "float-script: duplicate classic rules" >&2
  exit 1
}
sleep 0.01
touch "$HOME/.config/hypr/hyprland.conf"
"$root/scripts/float-omaq.sh"
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 4 ] || {
  echo "float-script: changed config did not refresh rules" >&2
  exit 1
}
export HYPRLAND_INSTANCE_SIGNATURE="second-instance"
"$root/scripts/float-omaq.sh"
export HYPRLAND_INSTANCE_SIGNATURE="test-instance"
"$root/scripts/float-omaq.sh"
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 6 ] || {
  echo "float-script: instance-scoped rule marker failed" >&2
  exit 1
}
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
grep -q '^dispatch movetoworkspace current,address:0xabc$' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: current-workspace move missing" >&2
  exit 1
}
grep -q '^dispatch focuswindow address:0xabc$' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: address focus missing" >&2
  exit 1
}

socket_dir="$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE"
mkdir -p "$socket_dir"
python3 - "$socket_dir/.socket2.sock" <<'PY' &
import socket, sys, time
s = socket.socket(socket.AF_UNIX)
s.bind(sys.argv[1])
time.sleep(5)
PY
socket_pid=$!
i=0
while [ "$i" -lt 20 ] && [ ! -S "$socket_dir/.socket2.sock" ]; do
  i=$((i + 1))
  sleep 0.05
done
export OMAQ_FLOAT_SOCAT_LOG="$tmp/socat.log"
export OMAQ_FLOAT_WATCH_BLOCK=1
: >"$OMAQ_FLOAT_SOCAT_LOG"
"$root/scripts/float-omaq.sh" watch-rules &
first_watcher=$!
i=0
while [ "$i" -lt 40 ] && [ "$(wc -l <"$OMAQ_FLOAT_SOCAT_LOG")" -lt 1 ]; do
  i=$((i + 1))
  sleep 0.05
done
"$root/scripts/float-omaq.sh" watch-rules &
second_watcher=$!
sleep 0.1
[ "$(wc -l <"$OMAQ_FLOAT_SOCAT_LOG")" -eq 1 ] || {
  echo "float-script: follower started before leadership" >&2
  exit 1
}
kill "$first_watcher"
wait "$first_watcher" 2>/dev/null || true
i=0
while [ "$i" -lt 40 ] && [ "$(wc -l <"$OMAQ_FLOAT_SOCAT_LOG")" -lt 2 ]; do
  i=$((i + 1))
  sleep 0.05
done
[ "$(wc -l <"$OMAQ_FLOAT_SOCAT_LOG")" -eq 2 ] || {
  echo "float-script: follower did not take over terminated leader" >&2
  exit 1
}
kill "$second_watcher"
wait "$second_watcher" 2>/dev/null || true
first_watcher=""
second_watcher=""
unset OMAQ_FLOAT_WATCH_BLOCK
: >"$OMAQ_FLOAT_TEST_LOG"
rm -f "$XDG_RUNTIME_DIR"/omaq-hypr-float-rules.*
export OMAQ_FLOAT_WATCH_HOLD=1
export OMAQ_FLOAT_NO_CLIENTS=1
"$root/scripts/float-omaq.sh" watch-rules &
first_watcher=$!
sleep 0.05
"$root/scripts/float-omaq.sh" watch-rules &
second_watcher=$!
sleep 0.05
kill -0 "$second_watcher" 2>/dev/null || {
  echo "float-script: follower did not wait for watcher leadership" >&2
  exit 1
}
wait "$first_watcher"
wait "$second_watcher"
first_watcher=""
second_watcher=""
unset OMAQ_FLOAT_WATCH_HOLD
unset OMAQ_FLOAT_NO_CLIENTS
kill "$socket_pid" 2>/dev/null || true
wait "$socket_pid" 2>/dev/null || true
socket_pid=""
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 6 ] || {
  echo "float-script: leader and follower did not refresh rules" >&2
  exit 1
}
"$root/scripts/float-omaq.sh" watch-rules
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 8 ] || {
  echo "float-script: later watcher did not take over after leader exit" >&2
  exit 1
}

export OMAQ_FLOAT_LUA=1
: >"$OMAQ_FLOAT_TEST_LOG"
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
grep -q 'hl.dsp.window.move({ workspace = "7", window = "address:0xabc", follow = true })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua current-workspace move missing" >&2
  exit 1
}
grep -q 'hl.dsp.window.bring_to_top({ window = "address:0xabc" })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua focus missing" >&2
  exit 1
}
export OMAQ_FLOAT_SPECIAL=1
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
grep -q 'hl.dsp.window.move({ workspace = "special:scratchpad", window = "address:0xabc", follow = true })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua special-workspace move missing" >&2
  exit 1
}

echo "float-script: ok"
