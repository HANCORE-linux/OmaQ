#!/bin/bash
# Install first-map rules for OmaQ windows and focus one exact chat on request.
# Existing windows are never forced back to floating after the user tiles them.
set -u

MODE="${1:-install-rules}"
TARGET_TITLE="${2:-}"
TARGET_X="${3:-}"
TARGET_Y="${4:-}"
TARGET_WIDTH="${5:-}"
TARGET_HEIGHT="${6:-}"

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
if [[ "$MODE" != "observe-title" && "$MODE" != "list-geometry" ]] &&
   hyprctl eval 'return true' >/dev/null 2>&1; then
  LUA=1
fi
INSTANCE_KEY=$(printf '%s' "${HYPRLAND_INSTANCE_SIGNATURE:-unknown}" | sha256sum | cut -d' ' -f1)
RULE_FLAG="$RULE_DIR/rules.${INSTANCE_KEY}"

install_rules() {
  if (( LUA )); then
    # Named Lua rules are removed when their returned handles are collected.
    # Retain both handles in Hyprland's global Lua state across hyprctl calls.
    hyprctl eval 'omaq_window_rules = omaq_window_rules or {}; if omaq_window_rules.demo_v4 == nil then omaq_window_rules.demo_v4 = hl.window_rule({ name = "omaq-float-demo-v4", match = { initial_title = "^OmaQ demo$" }, float = true, no_anim = true }) else omaq_window_rules.demo_v4:set_enabled(true) end' >/dev/null || return 1
    hyprctl eval 'omaq_window_rules = omaq_window_rules or {}; if omaq_window_rules.chat_v5 == nil then omaq_window_rules.chat_v5 = hl.window_rule({ name = "omaq-float-chat-v5", match = { initial_title = "^OmaQ chat.*$" }, float = true, no_anim = true }) else omaq_window_rules.chat_v5:set_enabled(true) end' >/dev/null || return 1
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
       ! hyprctl keyword 'windowrulev2 = noanim, title:^(OmaQ demo)$' >/dev/null ||
       ! hyprctl keyword 'windowrulev2 = float, title:^(OmaQ chat.*)$' >/dev/null ||
       ! hyprctl keyword 'windowrulev2 = noanim, title:^(OmaQ chat.*)$' >/dev/null; then
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

if [[ "$MODE" == "list-geometry" ]]; then
  command -v jq >/dev/null 2>&1 || exit 3
  clients=$(hyprctl -j clients 2>/dev/null) || exit 3
  monitors=$(hyprctl -j monitors 2>/dev/null) || exit 3
  (( ${#clients} <= 1048576 && ${#monitors} <= 262144 )) || exit 3
  jq -cn --argjson clients "$clients" --argjson monitors "$monitors" '
    [$clients[] |
      select(((.title // "") | startswith("OmaQ chat — ")) and
        ((.at // []) | length) == 2 and ((.size // []) | length) == 2) |
      . as $client |
      {title: .title, x: .at[0], y: .at[1], width: .size[0], height: .size[1],
       floating: ((.floating // false) == true),
       monitor: (($monitors[] | select(.id == $client.monitor) | .name) // "")}]
  ' || exit 3
  exit 0
fi

if [[ "$MODE" == "install-rules" ]]; then
  install_rules || exit 1
  exit 0
fi
if [[ "$MODE" != "observe-title" ]]; then
  install_rules || exit 1
fi
command -v jq >/dev/null 2>&1 || exit 3

resolve_title_address() {
  local clients address
  clients=$(hyprctl -j clients 2>/dev/null) || return 1
  (( ${#clients} <= 1048576 )) || return 1
  address=$(jq -r --arg title "$TARGET_TITLE" '
    [.[] | select((.title // "") == $title)] |
    if length == 1 and (.[0].floating // false) == true and
       ((.[0].address // "") | test("^0x[0-9a-fA-F]+$"))
    then .[0].address else empty end' <<<"$clients") || return 1
  [[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] || return 1
  printf '%s\n' "$address"
}

observe_address_geometry() {
  local address="$1" clients monitors placement
  clients=$(hyprctl -j clients 2>/dev/null) || return 1
  monitors=$(hyprctl -j monitors 2>/dev/null) || return 1
  (( ${#clients} <= 1048576 && ${#monitors} <= 262144 )) || return 1
  placement=$(jq -cn --arg title "$TARGET_TITLE" --arg address "$address" \
    --argjson clients "$clients" --argjson monitors "$monitors" '
    [$clients[] | select((.title // "") == $title and (.address // "") == $address)] as $matches |
    if ($matches | length) == 1 and ($matches[0].floating // false) == true and
       (($matches[0].at // []) | length) == 2 and
       (($matches[0].size // []) | length) == 2
    then $matches[0] as $client |
      {title: $title, x: $client.at[0], y: $client.at[1],
       width: $client.size[0], height: $client.size[1], floating: true,
       monitor: (($monitors[] | select(.id == $client.monitor) | .name) // "")}
    else empty end') || return 1
  (( ${#placement} > 0 && ${#placement} <= 2048 )) || return 1
  printf '%s\n' "$placement"
}

if [[ "$MODE" == "observe-title" ]]; then
  [[ -n "$TARGET_TITLE" ]] || exit 2
  client=$(resolve_title_address) || exit 3
  observe_address_geometry "$client" || exit 3
  exit 0
fi

if [[ "$MODE" == "place-title" ]]; then
  [[ -n "$TARGET_TITLE" && "$TARGET_X" =~ ^-?[0-9]+$ &&
     "$TARGET_Y" =~ ^-?[0-9]+$ && "$TARGET_WIDTH" =~ ^[0-9]+$ &&
     "$TARGET_HEIGHT" =~ ^[0-9]+$ ]] || exit 2
  (( TARGET_X >= -32768 && TARGET_X <= 32768 &&
     TARGET_Y >= -32768 && TARGET_Y <= 32768 &&
     TARGET_WIDTH >= 360 && TARGET_WIDTH <= 4096 &&
     TARGET_HEIGHT >= 420 && TARGET_HEIGHT <= 4096 )) || exit 2
  client=""
  for (( attempt = 0; attempt < 20; attempt++ )); do
    client=$(resolve_title_address 2>/dev/null || true)
    [[ -n "$client" ]] && break
    sleep 0.05
  done
  [[ "$client" =~ ^0x[0-9a-fA-F]+$ ]] || exit 3
  if (( LUA )); then
    hyprctl dispatch \
      "hl.dsp.window.resize({ x = ${TARGET_WIDTH}, y = ${TARGET_HEIGHT}, relative = false, window = \"address:${client}\" })" \
      >/dev/null || exit 4
    hyprctl dispatch \
      "hl.dsp.window.move({ x = ${TARGET_X}, y = ${TARGET_Y}, relative = false, window = \"address:${client}\" })" \
      >/dev/null || exit 4
  else
    hyprctl dispatch "resizewindowpixel" \
      "exact ${TARGET_WIDTH} ${TARGET_HEIGHT},address:${client}" >/dev/null || exit 4
    hyprctl dispatch "movewindowpixel" \
      "exact ${TARGET_X} ${TARGET_Y},address:${client}" >/dev/null || exit 4
  fi

  stable=0
  for (( attempt = 0; attempt < 40; attempt++ )); do
    placement=$(observe_address_geometry "$client" 2>/dev/null || true)
    if [[ -n "$placement" ]] && jq -e \
      --argjson x "$TARGET_X" --argjson y "$TARGET_Y" \
      --argjson width "$TARGET_WIDTH" --argjson height "$TARGET_HEIGHT" '
        .floating == true and (.monitor | type == "string" and length > 0) and
        .x == $x and .y == $y and .width == $width and .height == $height
      ' <<<"$placement" >/dev/null; then
      stable=$((stable + 1))
      if (( stable >= 2 )); then
        printf '%s\n' "$placement"
        exit 0
      fi
    else
      stable=0
    fi
    sleep 0.05
  done
  exit 5
fi

[[ "$MODE" == "focus-title" && -n "$TARGET_TITLE" ]] || exit 2
clients=$(hyprctl -j clients 2>/dev/null) || exit 3
active=$(hyprctl -j activeworkspace 2>/dev/null) || exit 3
(( ${#clients} <= 1048576 && ${#active} <= 65536 )) || exit 3
focus_target=$(jq -cn --arg title "$TARGET_TITLE" --argjson clients "$clients" '
  def workspace_key:
    if ((.name // "") | startswith("special:")) then .name
    elif ((.id // null) | type) == "number" then (.id | tostring)
    else "" end;
  [$clients[] | select((.title // "") == $title)] as $matches |
  if ($matches | length) == 1 and
     (($matches[0].address // "") | test("^0x[0-9a-fA-F]+$"))
  then {address: $matches[0].address,
        workspace: (($matches[0].workspace // {}) | workspace_key)}
  else empty end') || exit 3
address=$(jq -r '.address // empty' <<<"$focus_target")
window_workspace=$(jq -r '.workspace // empty' <<<"$focus_target")
active_workspace=$(jq -r '
  if ((.name // "") | startswith("special:")) then .name
  elif ((.id // null) | type) == "number" then (.id | tostring)
  else empty end' <<<"$active")
[[ "$address" =~ ^0x[0-9a-fA-F]+$ ]] || exit 3
[[ "$window_workspace" =~ ^-?[0-9]+$ ||
   "$window_workspace" =~ ^special:[A-Za-z0-9._-]+$ ]] || exit 4
[[ "$active_workspace" =~ ^-?[0-9]+$ ||
   "$active_workspace" =~ ^special:[A-Za-z0-9._-]+$ ]] || exit 4

if (( LUA )); then
  if [[ "$window_workspace" != "$active_workspace" ]]; then
    hyprctl dispatch "hl.dsp.window.move({ workspace = \"${active_workspace}\", window = \"address:${address}\", follow = true })" >/dev/null || exit 4
  fi
  hyprctl dispatch "hl.dsp.window.bring_to_top({ window = \"address:${address}\" })" >/dev/null || exit 4
else
  if [[ "$window_workspace" != "$active_workspace" ]]; then
    hyprctl dispatch "movetoworkspace" "current,address:${address}" >/dev/null || exit 4
  fi
  hyprctl dispatch "focuswindow" "address:${address}" >/dev/null || exit 4
fi
exit 0
