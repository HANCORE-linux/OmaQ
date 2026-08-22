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

found=0
i=0
while (( i < 25 )); do
  if hyprctl -j clients 2>/dev/null | grep -Eq '"title": "OmaQ (demo|chat)'; then
    found=1
    break
  fi
  sleep 0.04
  i=$((i + 1))
done
(( found )) || exit 0

float_match() {
  local re="$1"
  if (( LUA )); then
    hyprctl dispatch "hl.dsp.window.fullscreen({ action = \"unset\", window = \"title:${re}\" })" >/dev/null || true
    hyprctl dispatch "hl.dsp.window.float({ action = \"on\", window = \"title:${re}\" })" >/dev/null || true
  else
    hyprctl dispatch "fullscreenstate" "0 0,title:${re}" >/dev/null || true
    hyprctl dispatch "setfloating" "title:${re}" >/dev/null || true
  fi
}

float_match "^OmaQ demo$"
float_match "^OmaQ chat"
exit 0
