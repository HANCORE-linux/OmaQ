#!/bin/bash
# Keep OmaQ chat/demo as floating windows from the first map.
# Dual-mode: Lua window_rule + dsp, or classic windowrulev2 + setfloating.
# Friend chats are titled "OmaQ chat" or "OmaQ chat — <name>".
set -u

LUA=0
if hyprctl dispatch 'hl.dsp' 2>&1 | grep -q 'expected a dispatcher'; then
  LUA=1
fi

RULE_FLAG="${XDG_RUNTIME_DIR:-/tmp}/omaq-hypr-float-rules"

if (( LUA )); then
  hyprctl eval 'hl.window_rule({ name = "omaq-float-demo", match = { title = "^OmaQ demo$" }, float = true })' >/dev/null || true
  hyprctl eval 'hl.window_rule({ name = "omaq-float-chat", match = { title = "^OmaQ chat" }, float = true })' >/dev/null || true
elif [[ ! -f "$RULE_FLAG" ]]; then
  hyprctl keyword 'windowrulev2 = float, title:^(OmaQ demo)$' >/dev/null || true
  hyprctl keyword 'windowrulev2 = float, title:^(OmaQ chat)' >/dev/null || true
  : > "$RULE_FLAG"
fi

find_addresses() {
  if command -v jq >/dev/null 2>&1; then
    hyprctl -j clients 2>/dev/null | jq -r '.[] | select((.title // "") | test("^OmaQ (demo|chat)")) | .address'
  fi
}

found=0
i=0
while (( i < 25 )); do
  if [[ -n "$(find_addresses | head -1)" ]] || hyprctl -j clients 2>/dev/null | grep -Eq '"title": "OmaQ (demo|chat)'; then
    found=1
    break
  fi
  sleep 0.04
  i=$((i + 1))
done
(( found )) || exit 0

float_address() {
  local address="$1"
  if (( LUA )); then
    hyprctl dispatch "hl.dsp.window.fullscreen({ action = \"unset\", window = \"address:${address}\" })" >/dev/null || true
    hyprctl dispatch "hl.dsp.window.float({ action = \"on\", window = \"address:${address}\" })" >/dev/null || true
  else
    hyprctl dispatch "fullscreenstate" "0 0,address:${address}" >/dev/null || true
    hyprctl dispatch "setfloating" "address:${address}" >/dev/null || true
  fi
}

addresses=$(find_addresses)
if [[ -n "$addresses" ]]; then
  while IFS= read -r address; do
    [[ -n "$address" ]] && float_address "$address"
  done <<< "$addresses"
else
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
