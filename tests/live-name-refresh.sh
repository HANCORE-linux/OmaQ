#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
omaq = (root / "helper/omaq.c").read_text(encoding="utf-8")
tox = (root / "helper/tox_adapt.c").read_text(encoding="utf-8")
tox_h = (root / "helper/tox_adapt.h").read_text(encoding="utf-8")
surface = (root / "ChatSurface.qml").read_text(encoding="utf-8")
page = (root / "pages/ChatPage.qml").read_text(encoding="utf-8")

contracts = {
    "helper/omaq.c": [
        "static void hook_friend_name(void *ud, uint32_t friend)",
        "omaq_tox_set_friend_name_hook(g_tox, hook_friend_name, NULL)",
    ],
    "helper/tox_adapt.c": [
        "tox_callback_friend_name(t->tox, on_friend_name)",
        "t->on_friend_name(t->ud, friend_number)",
        "tox_callback_group_peer_name(t->tox, on_gpeer_name)",
    ],
    "helper/tox_adapt.h": [
        "typedef void (*omaq_on_friend_name)(void *ud, uint32_t friend);",
        "void omaq_tox_set_friend_name_hook",
    ],
}
sources = {
    "helper/omaq.c": omaq,
    "helper/tox_adapt.c": tox,
    "helper/tox_adapt.h": tox_h,
}
for name, needles in contracts.items():
    for needle in needles:
        if needle not in sources[name]:
            raise SystemExit(f"live-name-refresh: missing {name} contract: {needle}")

hook_start = omaq.index("static void hook_friend_name(")
hook_end = omaq.index("static void flush_friend_names(", hook_start)
flush_end = omaq.index("static void hook_typing(", hook_end)
if "g_friend_names_dirty = 1;" not in omaq[hook_start:hook_end] or \
        "emit_friends();" in omaq[hook_start:hook_end] or \
        "emit_friends();" not in omaq[hook_end:flush_end]:
    raise SystemExit("live-name-refresh: Direct names must refresh after the native callback")
if len(re.findall(r"omaq_tox_iterate\(g_tox\);\s+flush_friend_names\(\);", omaq)) != 2:
    raise SystemExit("live-name-refresh: a backend loop omitted the post-iterate name refresh")
if surface.count("peerName: root.friendLabel") != 3:
    raise SystemExit("live-name-refresh: an open ChatPage lost its reactive Direct name")
for marker in ("Number(root.service.groupsTick || 0)",
               "names.push(root.groupMemberName(actors[i]))",
               "return root.groupMemberName(sender)"):
    if marker not in page:
        raise SystemExit(f"live-name-refresh: Group name surface lost {marker!r}")
PY

tmp=$(mktemp -d /tmp/omaq-live-name-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cp "$root/Service.qml" "$tmp/Service.qml"
cat >"$tmp/qmldir" <<'EOF'
module TestOmaq
Service 1.0 Service.qml
EOF
python3 - "$tmp/Service.qml" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = "  Component.onCompleted: root.launchHelperDetached()\n"
if text.count(old) != 1:
    raise SystemExit("live-name-refresh: Service launch seam changed")
path.write_text(text.replace(old, "  Component.onCompleted: {}\n"), encoding="utf-8")
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "." as TestOmaq

ShellRoot {
  TestOmaq.Service { id: service }
  Timer {
    interval: 0
    running: true
    onTriggered: {
      var key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      var group = "g:" + key
      var instance = "11111111111111111111111111111111"
      service.helperCompatibility = "compatible"
      service.activeHelperProtocol = 15
      service.helperInstance = instance
      service.friendsReady = true
      service.applyFriendSnapshot([{ id: "0", key: key, name: "Before" }])
      var friendTick = service.friendsTick
      service.applyFriendSnapshot([{ id: "0", key: key, name: "After" }])
      var directUpdated = service.friendsTick === friendTick + 1 &&
        service.friends.length === 1 && service.friends[0].name === "After"

      service.groups = [{ id: group, title: "Group", memberCount: 1, limit: 10,
        members: [{ peer: "0", key: key, friendKey: key, name: "Before",
          role: "member", online: true, self: false }] }]
      service.groupsReady = true
      service.expectedGroupRequest = "name-refresh"
      service.handleLine(JSON.stringify({ event: "group.list.begin", generation: "1",
        instance: instance, request: "name-refresh", groups: 1, members: 1 }))
      service.handleLine(JSON.stringify({ event: "group.info", generation: "1",
        instance: instance, request: "name-refresh", group: group,
        title: "Group", members: 1, limit: 10 }))
      service.handleLine(JSON.stringify({ event: "group.member", generation: "1",
        instance: instance, request: "name-refresh", group: group, peer: "0",
        key: key, friendKey: key, name: "After", role: "member",
        online: true, self: false }))
      service.handleLine(JSON.stringify({ event: "group.list.end", generation: "1",
        instance: instance, request: "name-refresh", groups: 1, members: 1 }))
      var groupUpdated = service.groupsReady && service.groups.length === 1 &&
        service.groups[0].members.length === 1 &&
        service.groups[0].members[0].name === "After"
      console.log(directUpdated && groupUpdated
        ? "OMAQ_LIVE_NAME_REFRESH_OK" : "OMAQ_LIVE_NAME_REFRESH_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 8 quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1 ||
   ! grep -q 'OMAQ_LIVE_NAME_REFRESH_OK' "$out"; then
  cat "$out" >&2
  echo "live-name-refresh: QML fixture failed" >&2
  exit 1
fi

echo "live-name-refresh: ok"
