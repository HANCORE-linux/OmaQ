#!/bin/sh
# float-omaq.sh: first-map rules are bounded and focus never re-floats a tiled window.
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
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
if [ "$1" = "keyword" ] && [ -n "${OMAQ_FLOAT_FAIL_MARKER:-}" ]; then
  count=$(cat "$OMAQ_FLOAT_FAIL_MARKER" 2>/dev/null || printf '0')
  count=$((count + 1))
  printf '%s\n' "$count" >"$OMAQ_FLOAT_FAIL_MARKER"
  [ "$count" -eq 5 ] && exit 1
fi
if [ "$1" = "eval" ]; then
  [ "${OMAQ_FLOAT_LUA:-0}" = "1" ] && exit 0
  exit 1
fi
if [ "$1" = "-j" ] && [ "${2:-}" = "clients" ]; then
  if [ "${OMAQ_FLOAT_NO_CLIENTS:-0}" = "1" ]; then
    printf '%s\n' '[]'
  else
    printf '%s\n' '[{"title":"OmaQ chat — Alice · 0","address":"0xabc","floating":true,"monitor":1,"at":[40,80],"size":[420,420]},{"title":"OmaQ chat — Bob · 1","address":"0xdef","floating":true,"monitor":2,"at":[512,144],"size":[460,520]}]'
  fi
elif [ "$1" = "-j" ] && [ "${2:-}" = "monitors" ]; then
  printf '%s\n' '[{"id":1,"name":"DP-1"},{"id":2,"name":"HDMI-A-1"}]'
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
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 4 ] || {
  echo "float-script: duplicate classic rules" >&2
  exit 1
}
sleep 0.01
touch "$HOME/.config/hypr/hyprland.conf"
"$root/scripts/float-omaq.sh"
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 8 ] || {
  echo "float-script: changed config did not refresh rules" >&2
  exit 1
}
export HYPRLAND_INSTANCE_SIGNATURE="second-instance"
"$root/scripts/float-omaq.sh"
export HYPRLAND_INSTANCE_SIGNATURE="test-instance"
"$root/scripts/float-omaq.sh"
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 12 ] || {
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
"$root/scripts/float-omaq.sh" place-title "OmaQ chat — Bob · 1" 512 144 460 520
grep -q '^dispatch resizewindowpixel exact 460 520,address:0xdef$' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: per-chat resize missing" >&2
  exit 1
}
grep -q '^dispatch movewindowpixel exact 512 144,address:0xdef$' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: per-chat placement missing" >&2
  exit 1
}
if grep -E 'windowpixel .*address:0xabc' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: placing one chat changed another chat" >&2
  exit 1
fi
geometry=$($root/scripts/float-omaq.sh list-geometry)
printf '%s' "$geometry" | jq -e '
  length == 2 and .[0].title == "OmaQ chat — Alice · 0" and
  .[0].x == 40 and .[0].width == 420 and .[0].monitor == "DP-1" and
  .[1].title == "OmaQ chat — Bob · 1" and .[1].y == 144 and
  .[1].height == 520 and .[1].monitor == "HDMI-A-1"' >/dev/null || {
  echo "float-script: bounded geometry snapshot missing" >&2
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
rm -rf "$XDG_RUNTIME_DIR/omaq-hypr"
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
rm -rf "$XDG_RUNTIME_DIR/omaq-hypr"
export OMAQ_FLOAT_FAIL_MARKER="$tmp/fail-once"
set +e
"$root/scripts/float-omaq.sh" watch-rules
reload_status=$?
set -e
[ "$reload_status" -eq 5 ] || {
  echo "float-script: failed reload install did not trigger recovery" >&2
  exit 1
}
unset OMAQ_FLOAT_FAIL_MARKER
kill "$socket_pid" 2>/dev/null || true
wait "$socket_pid" 2>/dev/null || true
socket_pid=""
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 17 ] || {
  echo "float-script: leader, follower, and failed reload counts differ" >&2
  exit 1
}
"$root/scripts/float-omaq.sh" watch-rules
[ "$(grep -c '^keyword windowrulev2' "$OMAQ_FLOAT_TEST_LOG")" -eq 25 ] || {
  echo "float-script: later watcher did not take over after leader exit" >&2
  exit 1
}

export OMAQ_FLOAT_LUA=1
: >"$OMAQ_FLOAT_TEST_LOG"
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
if grep -q 'dispatch hl.dsp$' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: invalid Lua capability probe returned" >&2
  exit 1
fi
grep -q 'omaq_window_rules.chat_v5 = hl.window_rule' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua rule handle is not retained" >&2
  exit 1
}
grep -Fq 'match = { initial_title = "^OmaQ chat.*$" }' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: static Lua rule does not match the initial title" >&2
  exit 1
}
grep -q 'no_anim = true' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: OmaQ window animations remain enabled" >&2
  exit 1
}
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
if grep -Eq 'window\.float|setfloating|fullscreenstate' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: focusing an existing chat changed its tiling state" >&2
  exit 1
fi
export OMAQ_FLOAT_SPECIAL=1
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
grep -q 'hl.dsp.window.move({ workspace = "special:scratchpad", window = "address:0xabc", follow = true })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua special-workspace move missing" >&2
  exit 1
}

echo "float-script: ok"
