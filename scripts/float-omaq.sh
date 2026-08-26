#!/bin/bash
# Install first-map rules for OmaQ windows and focus one exact chat on request.
# Existing windows are never forced back to floating after the user tiles them.
set -u

MODE="${1:-install-rules}"
TARGET_TITLE="${2:-}"

runtime_root="${XDG_RUNTIME_DIR:-/tmp/omaq-runtime-${UID}}"
if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
  mkdir -p -- "$runtime_root" || exit 1
  chmod 700 -- "$runtime_root" 2>/dev/null || exit 1
fi
[[ -d "$runtime_root" && ! -L "$runtime_root" && -O "$runtime_root" ]] || exit 1
RULE_DIR="$runtime_root/omaq-hypr"
mkdir -p -- "$RULE_DIR" || exit 1
[[ -d "$RULE_DIR" && ! -L "$RULE_DIR" && -O "$RULE_DIR" ]] || exit 1
chmod 700 -- "$RULE_DIR" 2>/dev/null || exit 1

LUA=0
if hyprctl eval 'return true' >/dev/null 2>&1; then
  LUA=1
fi
INSTANCE_KEY=$(printf '%s' "${HYPRLAND_INSTANCE_SIGNATURE:-unknown}" | sha256sum | cut -d' ' -f1)
RULE_FLAG="$RULE_DIR/rules.${INSTANCE_KEY}"

install_rules() {
  if (( LUA )); then
    hyprctl eval 'hl.window_rule({ name = "omaq-float-demo", match = { title = "^OmaQ demo$" }, float = true })' >/dev/null || return 1
    hyprctl eval 'hl.window_rule({ name = "omaq-float-chat", match = { title = "^OmaQ chat" }, float = true })' >/dev/null || return 1
    return 0
  fi

  command -v flock >/dev/null 2>&1 || return 1
  exec 9>"${RULE_FLAG}.lock" || return 1
  flock -x 9 || return 1
  local generation previous temporary
  generation=$(
    {
      printf '%s\n' "${HYPRLAND_INSTANCE_SIGNATURE:-unknown}"
      find "$HOME/.config/hypr" "$HOME/.local/state/omarchy/current/theme" \
        "$HOME/.local/state/omarchy/toggles/hypr" -maxdepth 2 -type f \
        \( -name '*.conf' -o -name '*.lua' \) -printf '%p:%i:%T@\n' 2>/dev/null | sort
    } | sha256sum | cut -d' ' -f1
  )
  previous=$(cat -- "$RULE_FLAG" 2>/dev/null || true)
  if [[ "$previous" != "$generation" ]]; then
    if ! hyprctl keyword 'windowrulev2 = float, title:^(OmaQ demo)$' >/dev/null ||
       ! hyprctl keyword 'windowrulev2 = float, title:^(OmaQ chat)' >/dev/null; then
      flock -u 9
      exec 9>&-
      return 1
    fi
    umask 077
    temporary=$(mktemp "${RULE_FLAG}.tmp.XXXXXX") || {
      flock -u 9
      exec 9>&-
      return 1
    }
    if ! printf '%s\n' "$generation" >"$temporary" || ! mv -f -- "$temporary" "$RULE_FLAG"; then
      rm -f -- "$temporary"
      flock -u 9
      exec 9>&-
      return 1
    fi
  fi
  flock -u 9
  exec 9>&-
  return 0
}

if [[ "$MODE" == "watch-rules" ]]; then
  command -v flock >/dev/null 2>&1 || exit 2
  exec 8>"${RULE_FLAG}.watch.lock" || exit 2
  flock -x 8 || exit 2
  SOCKET="${XDG_RUNTIME_DIR:-}/hypr/${HYPRLAND_INSTANCE_SIGNATURE:-}/.socket2.sock"
  [[ -S "$SOCKET" ]] || exit 2
  command -v socat >/dev/null 2>&1 || exit 2
  coproc RULE_EVENTS {
    exec 8>&-
    exec socat -u "UNIX-CONNECT:${SOCKET}" - 2>/dev/null
  }
  EVENT_FD=${RULE_EVENTS[0]}
  EVENT_PID=$RULE_EVENTS_PID
  # shellcheck disable=SC2329 # Invoked by the signal trap.
  cleanup_events() {
    if [[ -n "${EVENT_PID:-}" ]]; then
      kill "$EVENT_PID" 2>/dev/null || true
      wait "$EVENT_PID" 2>/dev/null || true
      EVENT_PID=""
    fi
  }
  trap 'cleanup_events; exit 0' HUP INT TERM
  exec {EVENT_READ_FD}<&"$EVENT_FD"
  install_rules >/dev/null 2>&1 || true
  while IFS= read -r -u "$EVENT_READ_FD" event; do
    if [[ "$event" == configreloaded\>* ]]; then
      rm -f -- "$RULE_FLAG"
      install_rules >/dev/null 2>&1 || exit 5
    fi
  done
  exec {EVENT_READ_FD}<&-
  wait "$EVENT_PID" 2>/dev/null || true
  EVENT_PID=""
  trap - HUP INT TERM
  exit 0
fi

install_rules || exit 1
[[ "$MODE" == "install-rules" ]] && exit 0
[[ "$MODE" == "focus-title" && -n "$TARGET_TITLE" ]] || exit 2
command -v jq >/dev/null 2>&1 || exit 3

address=$(hyprctl -j clients 2>/dev/null |
  jq -r --arg title "$TARGET_TITLE" '.[] | select((.title // "") == $title) | .address' |
  head -n 1)
[[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] || exit 3

if (( LUA )); then
  workspace=$(hyprctl -j activeworkspace 2>/dev/null | jq -r \
    'if ((.name // "") | startswith("special:")) then .name else (.id // empty | tostring) end')
  [[ "$workspace" =~ ^-?[0-9]+$ || "$workspace" =~ ^special:[A-Za-z0-9._-]+$ ]] || exit 4
  hyprctl dispatch "hl.dsp.window.move({ workspace = \"${workspace}\", window = \"address:${address}\", follow = true })" >/dev/null || exit 4
  hyprctl dispatch "hl.dsp.window.bring_to_top({ window = \"address:${address}\" })" >/dev/null || exit 4
else
  hyprctl dispatch "movetoworkspace" "current,address:${address}" >/dev/null || exit 4
  hyprctl dispatch "focuswindow" "address:${address}" >/dev/null || exit 4
fi
exit 0
