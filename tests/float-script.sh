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
if [ "$1" = "dispatch" ] && [ -n "${OMAQ_FLOAT_DISPATCHED:-}" ]; then
  touch "$OMAQ_FLOAT_DISPATCHED"
  if [ -n "${OMAQ_FLOAT_FAIL_DISPATCH:-}" ]; then
    exit 1
  fi
fi
if [ "$1" = "-j" ] && [ "${2:-}" = "clients" ]; then
  if [ "${OMAQ_FLOAT_NO_CLIENTS:-0}" = "1" ]; then
    printf '%s\n' '[]'
  else
    alice_workspace="${OMAQ_FLOAT_ALICE_WORKSPACE:-7}"
    bob_title='OmaQ chat — Bob · 1'
    bob_at='512,144'
    bob_size='460,520'
    if [ "${OMAQ_FLOAT_TITLE_DELAY:-0}" = "1" ] &&
       { [ -z "${OMAQ_FLOAT_DISPATCHED:-}" ] || [ ! -e "$OMAQ_FLOAT_DISPATCHED" ]; }; then
      title_count=$(cat "$OMAQ_FLOAT_TITLE_QUERIES" 2>/dev/null || printf '0')
      title_count=$((title_count + 1))
      printf '%s\n' "$title_count" >"$OMAQ_FLOAT_TITLE_QUERIES"
      [ "$title_count" -ge "${OMAQ_FLOAT_TITLE_READY_AFTER:-3}" ] || bob_title='OmaQ chat'
    fi
    if [ -n "${OMAQ_FLOAT_DISPATCHED:-}" ] && [ -e "$OMAQ_FLOAT_DISPATCHED" ]; then
      count=$(cat "$OMAQ_FLOAT_OBSERVATIONS" 2>/dev/null || printf '0')
      count=$((count + 1))
      printf '%s\n' "$count" >"$OMAQ_FLOAT_OBSERVATIONS"
      if [ "$count" -ge "${OMAQ_FLOAT_GEOMETRY_READY_AFTER:-3}" ] &&
         [ "${OMAQ_FLOAT_STAY_OLD:-0}" != "1" ]; then
        bob_at='900,400'
        bob_size='700,650'
      fi
    fi
    printf '[{"title":"OmaQ chat — Alice · 0","address":"0xabc","floating":true,"monitor":1,"workspace":{"id":%s,"name":"%s"},"at":[40,80],"size":[420,420]},{"title":"%s","address":"0xdef","floating":true,"monitor":2,"workspace":{"id":4,"name":"4"},"at":[%s],"size":[%s]}]\n' "$alice_workspace" "$alice_workspace" "$bob_title" "$bob_at" "$bob_size"
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
: >"$OMAQ_FLOAT_TEST_LOG"
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
if grep -q '^dispatch movetoworkspace ' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: same-workspace focus changed geometry" >&2
  exit 1
fi
grep -q '^dispatch focuswindow address:0xabc$' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: same-workspace address focus missing" >&2
  exit 1
}
export OMAQ_FLOAT_ALICE_WORKSPACE=6
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
unset OMAQ_FLOAT_ALICE_WORKSPACE
grep -q '^dispatch movetoworkspace current,address:0xabc$' "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: cross-workspace move missing" >&2
  exit 1
}
export OMAQ_FLOAT_DISPATCHED="$tmp/dispatched"
export OMAQ_FLOAT_OBSERVATIONS="$tmp/observations"
export OMAQ_FLOAT_TITLE_DELAY=1
export OMAQ_FLOAT_TITLE_QUERIES="$tmp/title-queries"
export OMAQ_FLOAT_TITLE_READY_AFTER=18
export OMAQ_FLOAT_GEOMETRY_READY_AFTER=38
placement=$("$root/scripts/float-omaq.sh" place-title \
  "OmaQ chat — Bob · 1" 900 400 700 650)
printf '%s' "$placement" | jq -e '
  .title == "OmaQ chat — Bob · 1" and .monitor == "HDMI-A-1" and
  .x == 900 and .y == 400 and .width == 700 and .height == 650 and
  .floating == true' >/dev/null || {
  echo "float-script: delayed stable placement observation missing" >&2
  exit 1
}
[ "$(grep -c '^dispatch resizewindowpixel exact 700 650,address:0xdef$' \
  "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] || {
  echo "float-script: per-chat resize was not dispatched exactly once" >&2
  exit 1
}
[ "$(grep -c '^dispatch movewindowpixel exact 900 400,address:0xdef$' \
  "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] || {
  echo "float-script: per-chat move was not dispatched exactly once" >&2
  exit 1
}
if grep -E 'windowpixel .*address:0xabc' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: placing one chat changed another chat" >&2
  exit 1
fi
: >"$OMAQ_FLOAT_TEST_LOG"
observed=$("$root/scripts/float-omaq.sh" observe-title "OmaQ chat — Bob · 1")
if grep -Eq '^(eval|keyword|dispatch) ' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: read-only observation mutated compositor state" >&2
  exit 1
fi
printf '%s' "$observed" | jq -e '
  .x == 900 and .y == 400 and .width == 700 and .height == 650 and
  .floating == true and .monitor == "HDMI-A-1"' >/dev/null || {
  echo "float-script: read-only title observation missing" >&2
  exit 1
}
geometry=$("$root/scripts/float-omaq.sh" list-geometry)
printf '%s' "$geometry" | jq -e '
  length == 2 and .[0].title == "OmaQ chat — Alice · 0" and
  .[0].x == 40 and .[0].width == 420 and .[0].monitor == "DP-1" and
  .[0].floating == true and
  .[1].title == "OmaQ chat — Bob · 1" and .[1].y == 400 and
  .[1].height == 650 and .[1].monitor == "HDMI-A-1" and
  .[1].floating == true' >/dev/null || {
  echo "float-script: bounded geometry snapshot missing" >&2
  exit 1
}

rm -f "$OMAQ_FLOAT_DISPATCHED" "$OMAQ_FLOAT_OBSERVATIONS"
: >"$OMAQ_FLOAT_TEST_LOG"
export OMAQ_FLOAT_STAY_OLD=1
set +e
timeout_output=$("$root/scripts/float-omaq.sh" place-title \
  "OmaQ chat — Bob · 1" 900 400 700 650)
timeout_status=$?
set -e
unset OMAQ_FLOAT_STAY_OLD
[ "$timeout_status" -eq 5 ] && [ -z "$timeout_output" ] || {
  echo "float-script: unstable placement did not fail closed" >&2
  exit 1
}
[ "$(grep -c '^dispatch resizewindowpixel ' "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] &&
  [ "$(grep -c '^dispatch movewindowpixel ' "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] || {
  echo "float-script: observation timeout repeated a native mutation" >&2
  exit 1
}
unset OMAQ_FLOAT_DISPATCHED OMAQ_FLOAT_OBSERVATIONS
unset OMAQ_FLOAT_TITLE_DELAY OMAQ_FLOAT_TITLE_QUERIES
unset OMAQ_FLOAT_TITLE_READY_AFTER OMAQ_FLOAT_GEOMETRY_READY_AFTER

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
if grep -q 'hl.dsp.window.move({ workspace = "7"' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: same-workspace Lua focus changed geometry" >&2
  exit 1
fi
grep -q 'hl.dsp.window.bring_to_top({ window = "address:0xabc" })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua focus missing" >&2
  exit 1
}
if grep -Eq 'window\.float|setfloating|fullscreenstate' "$OMAQ_FLOAT_TEST_LOG"; then
  echo "float-script: focusing an existing chat changed its tiling state" >&2
  exit 1
fi
export OMAQ_FLOAT_ALICE_WORKSPACE=6
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
unset OMAQ_FLOAT_ALICE_WORKSPACE
grep -q 'hl.dsp.window.move({ workspace = "7", window = "address:0xabc", follow = true })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: cross-workspace Lua move missing" >&2
  exit 1
}
export OMAQ_FLOAT_DISPATCHED="$tmp/lua-dispatched"
export OMAQ_FLOAT_OBSERVATIONS="$tmp/lua-observations"
lua_placement=$("$root/scripts/float-omaq.sh" place-title \
  "OmaQ chat — Bob · 1" 900 400 700 650)
printf '%s' "$lua_placement" | jq -e '
  .x == 900 and .y == 400 and .width == 700 and .height == 650 and
  .floating == true' >/dev/null || {
  echo "float-script: Lua stable placement observation missing" >&2
  exit 1
}
[ "$(grep -Fc 'hl.dsp.window.resize({ x = 700, y = 650, relative = false, window = "address:0xdef" })' "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] || {
  echo "float-script: Lua resize was not dispatched exactly once" >&2
  exit 1
}
[ "$(grep -Fc 'hl.dsp.window.move({ x = 900, y = 400, relative = false, window = "address:0xdef" })' "$OMAQ_FLOAT_TEST_LOG")" -eq 1 ] || {
  echo "float-script: Lua move was not dispatched exactly once" >&2
  exit 1
}
unset OMAQ_FLOAT_DISPATCHED OMAQ_FLOAT_OBSERVATIONS
export OMAQ_FLOAT_SPECIAL=1
"$root/scripts/float-omaq.sh" focus-title "OmaQ chat — Alice · 0"
grep -q 'hl.dsp.window.move({ workspace = "special:scratchpad", window = "address:0xabc", follow = true })' \
  "$OMAQ_FLOAT_TEST_LOG" || {
  echo "float-script: Lua special-workspace move missing" >&2
  exit 1
}

echo "float-script: ok"
