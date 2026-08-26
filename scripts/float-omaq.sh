#!/bin/bash
# Keep OmaQ chat/demo floating from the first map and move a selected chat to
# the current workspace on request. Chat titles become unique after mapping.
set -u

MODE="${1:-float-all}"
TARGET_TITLE="${2:-}"
SCRIPT_PATH=$(realpath "$0")
LUA=0
if hyprctl dispatch 'hl.dsp' 2>&1 | grep -q 'expected a dispatcher'; then
  LUA=1
fi
INSTANCE_KEY=$(printf '%s' "${HYPRLAND_INSTANCE_SIGNATURE:-unknown}" | sha256sum | cut -d' ' -f1)
RULE_FLAG="${XDG_RUNTIME_DIR:-/tmp}/omaq-hypr-float-rules.${INSTANCE_KEY}"

if [[ "$MODE" == "watch-rules" ]]; then
  command -v flock >/dev/null 2>&1 || exit 2
  exec 8>"${RULE_FLAG}.watch.lock"
  # One watcher per surface may start on multi-monitor setups. Followers wait
  # on the compositor-scoped lock and take over when the leader exits.
  flock -x 8
  SOCKET="${XDG_RUNTIME_DIR:-/tmp}/hypr/${HYPRLAND_INSTANCE_SIGNATURE:-}/.socket2.sock"
  [[ -S "$SOCKET" ]] || exit 2
  command -v socat >/dev/null 2>&1 || exit 2
  # Subscribe first. The coprocess pipe buffers reload events while the
  # initial refresh is polling for first-map windows, so takeover has no
  # unobserved refresh gap.
  coproc RULE_EVENTS {
    exec 8>&-
    exec socat -u "UNIX-CONNECT:${SOCKET}" - 2>/dev/null
  }
  EVENT_FD=${RULE_EVENTS[0]}
  EVENT_PID=$RULE_EVENTS_PID
  cleanup_events() {
    if [[ -n "${EVENT_PID:-}" ]]; then
      kill "$EVENT_PID" 2>/dev/null || true
      wait "$EVENT_PID" 2>/dev/null || true
      EVENT_PID=""
    fi
  }
  trap 'cleanup_events; exit 0' HUP INT TERM
  exec {EVENT_READ_FD}<&"$EVENT_FD"
  "$SCRIPT_PATH" refresh-rules >/dev/null 2>&1 || true
  while IFS= read -r -u "$EVENT_READ_FD" event; do
    if [[ "$event" == configreloaded\>* ]]; then
      rm -f "$RULE_FLAG"
      "$SCRIPT_PATH" refresh-rules >/dev/null 2>&1 || true
    fi
  done
  exec {EVENT_READ_FD}<&-
  wait "$EVENT_PID" 2>/dev/null || true
  EVENT_PID=""
  trap - HUP INT TERM
  exit 0
fi

# Runtime rules are lost on a Hyprland configuration reload. Lua rules have
# stable names and can be replaced directly. Classic rules use a marker bound
# to the compositor instance and all sourced configuration generations, so
# repeated focus calls do not append duplicates while theme/config reloads
# reinstall the rules.
if (( LUA )); then
  hyprctl eval 'hl.window_rule({ name = "omaq-float-demo", match = { title = "^OmaQ demo$" }, float = true })' >/dev/null || true
  hyprctl eval 'hl.window_rule({ name = "omaq-float-chat", match = { title = "^OmaQ chat" }, float = true })' >/dev/null || true
else
  command -v flock >/dev/null 2>&1 || exit 1
  exec 9>"${RULE_FLAG}.lock"
  flock -x 9
  RULE_GENERATION=$(
    {
      printf '%s\n' "${HYPRLAND_INSTANCE_SIGNATURE:-unknown}"
      find "$HOME/.config/hypr" "$HOME/.local/state/omarchy/current/theme" \
        "$HOME/.local/state/omarchy/toggles/hypr" -maxdepth 2 -type f \
        \( -name '*.conf' -o -name '*.lua' \) -printf '%p:%i:%T@\n' 2>/dev/null | sort
    } | sha256sum | cut -d' ' -f1
  )
  PREVIOUS_GENERATION=$(cat "$RULE_FLAG" 2>/dev/null || true)
  if [[ "$PREVIOUS_GENERATION" != "$RULE_GENERATION" ]]; then
    if hyprctl keyword 'windowrulev2 = float, title:^(OmaQ demo)$' >/dev/null &&
       hyprctl keyword 'windowrulev2 = float, title:^(OmaQ chat)' >/dev/null; then
      umask 077
      RULE_TMP=$(mktemp "${RULE_FLAG}.tmp.XXXXXX") || exit 1
      if printf '%s\n' "$RULE_GENERATION" >"$RULE_TMP"; then
        mv -f "$RULE_TMP" "$RULE_FLAG"
      else
        rm -f "$RULE_TMP"
      fi
    fi
  fi
  flock -u 9
  exec 9>&-
fi

find_addresses() {
  command -v jq >/dev/null 2>&1 || return 0
  if [[ "$MODE" == "focus-title" && -n "$TARGET_TITLE" ]]; then
    hyprctl -j clients 2>/dev/null |
      jq -r --arg title "$TARGET_TITLE" '.[] | select((.title // "") == $title) | .address'
  else
    hyprctl -j clients 2>/dev/null |
      jq -r '.[] | select((.title // "") | test("^OmaQ (demo|chat)")) | .address'
  fi
}

found=0
i=0
while (( i < 25 )); do
  if [[ -n "$(find_addresses | head -1)" ]]; then
    found=1
    break
  fi
  if [[ "$MODE" != "focus-title" ]] &&
     hyprctl -j clients 2>/dev/null | grep -Eq '"title": "OmaQ (demo|chat)'; then
    found=1
    break
  fi
  sleep 0.04
  i=$((i + 1))
done
if (( ! found )); then
  [[ "$MODE" == "focus-title" ]] && exit 3
  exit 0
fi

float_address() {
  local address="$1"
  [[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] || return 0
  if (( LUA )); then
    hyprctl dispatch "hl.dsp.window.fullscreen({ action = \"unset\", window = \"address:${address}\" })" >/dev/null || true
    hyprctl dispatch "hl.dsp.window.float({ action = \"on\", window = \"address:${address}\" })" >/dev/null || true
  else
    hyprctl dispatch "fullscreenstate" "0 0,address:${address}" >/dev/null || true
    hyprctl dispatch "setfloating" "address:${address}" >/dev/null || true
  fi
}

move_to_current_workspace() {
  local address="$1" workspace
  [[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] || return 0
  if (( LUA )); then
    command -v jq >/dev/null 2>&1 || return 0
    workspace=$(hyprctl -j activeworkspace 2>/dev/null | jq -r \
      'if ((.name // "") | startswith("special:")) then .name else (.id // empty | tostring) end')
    [[ "$workspace" =~ ^-?[0-9]+$ || "$workspace" =~ ^special:[A-Za-z0-9._-]+$ ]] || return 1
    hyprctl dispatch "hl.dsp.window.move({ workspace = \"${workspace}\", window = \"address:${address}\", follow = true })" >/dev/null || return 1
    hyprctl dispatch "hl.dsp.window.bring_to_top({ window = \"address:${address}\" })" >/dev/null || return 1
  else
    hyprctl dispatch "movetoworkspace" "current,address:${address}" >/dev/null || return 1
    hyprctl dispatch "focuswindow" "address:${address}" >/dev/null || return 1
  fi
  return 0
}

addresses=$(find_addresses)
if [[ -n "$addresses" ]]; then
  while IFS= read -r address; do
    [[ -n "$address" ]] || continue
    float_address "$address"
    if [[ "$MODE" == "focus-title" ]]; then
      move_to_current_workspace "$address" || exit 4
      exit 0
    fi
  done <<< "$addresses"
elif [[ "$MODE" != "focus-title" ]]; then
  # Compatibility fallback for systems without jq.
  if (( LUA )); then
    hyprctl dispatch 'hl.dsp.window.float({ action = "on", window = "title:^OmaQ demo$" })' >/dev/null || true
    hyprctl dispatch 'hl.dsp.window.float({ action = "on", window = "title:^OmaQ chat" })' >/dev/null || true
  else
    hyprctl dispatch "setfloating" "title:^OmaQ demo$" >/dev/null || true
    hyprctl dispatch "setfloating" "title:^OmaQ chat" >/dev/null || true
  fi
fi
exit 0
