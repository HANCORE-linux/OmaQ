#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
chat = (root / "ChatSurface.qml").read_text()
service = (root / "Service.qml").read_text()
coordinator = (root / "SurfaceCoordinator.qml").read_text()
page = (root / "pages/ChatPage.qml").read_text()
panel = (root / "Panel.qml").read_text()
helper = (root / "helper/omaq.c").read_text()

required_chat = [
    'return "d:" + key',
    'root.service.requestAutoOpen(root.autoOpenRequest)',
    'root.service.setAutoOpen(stable,',
    'root.service.directBindingMatches(conversation, String(card.directKey || ""))',
    'function onFriendsTickChanged() { root.reconcileOpenCards() }',
    'peerKey: String(service.lastChatKey || "")',
]
for needle in required_chat:
    if needle not in chat:
        raise SystemExit(f"stable-direct-state: missing ChatSurface guard: {needle}")
if "FileView" in chat or "auto-open." in chat:
    raise SystemExit("stable-direct-state: QML still reads or writes Auto-open files")

required_service = [
    "readonly property bool supportsStableDirectState: root.activeHelperProtocol >= 11",
    "function directBindingMatches(conversation, expectedKey)",
    "function applySurfaceEvent(event)",
    "root.applySurfaceEvent(ev)",
    "function operationBindingValid(operation)",
    'root.rejectBoundOperation(operation, "identity_changed")',
    "function eventNeedsFriendProjection(event)",
    "root.releaseDirectEvents()",
    'bound.key = key',
]
for needle in required_service:
    if needle not in service:
        raise SystemExit(f"stable-direct-state: missing Service guard: {needle}")

if "property string pendingKey" not in coordinator or \
        "host.acceptOpenRequest(conversation, expectedKey, name, monitor)" not in coordinator:
    raise SystemExit("stable-direct-state: queued chat opens lost their key binding")

if "omaq.answerCall(omaq.lastCallConv, omaq.lastCallKey)" not in panel or \
        "omaq.stopCall(omaq.lastCallConv, omaq.lastCallKey)" not in panel:
    raise SystemExit("stable-direct-state: Panel call actions lost their key binding")

if "property string peerKey" not in page or "root.directBindingValid" not in page or \
        "onPeerKeyChanged: {" not in page or \
        "root.service.requestHistory(root.conversation, root.peerKey)" not in page or \
        "service.sendConversationOp({ op: \"msg.send\"" not in page:
    raise SystemExit("stable-direct-state: ChatPage direct actions are not key-bound")

required_helper = [
    "#define OMAQ_PROTOCOL_VERSION 15",
    'omaq_state_archive_copy(state_dir(), "surfaces.jsonl")',
    "omaq_surface_discard_legacy_direct(state_dir())",
    'omaq_direct_state_id(current_key, surface.conversation',
    'memcpy(current_key, surfaces[i].conversation + 2, 65)',
    'strcmp(op->op, "settings.auto-open.get") == 0',
    'strcmp(op->op, "settings.auto-open.set") == 0',
    "load_auto_open_state(fingerprint, &settings)",
]
for needle in required_helper:
    if needle not in helper:
        raise SystemExit(f"stable-direct-state: missing helper boundary: {needle}")
prepare_start = helper.index("static int prepare_surface_state(void)")
prepare_end = helper.index("static int load_auto_open_state", prepare_start)
prepare = helper[prepare_start:prepare_end]
if prepare.index('omaq_state_archive_copy(state_dir(), "surfaces.jsonl")') > \
        prepare.index("omaq_surface_discard_legacy_direct(state_dir())"):
    raise SystemExit("stable-direct-state: numeric surfaces can be discarded before archival")
auto_start = helper.index("static int load_auto_open_state")
auto_end = helper.index("static void emit_auto_open_state", auto_start)
auto = helper[auto_start:auto_end]
if auto.index("omaq_state_archive_copy") > auto.index("omaq_auto_open_save"):
    raise SystemExit("stable-direct-state: legacy Auto-open can be rewritten before archival")
PY

tmp=$(mktemp -d /tmp/omaq-surface-cache-XXXXXX)
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
text = path.read_text()
old = "  Component.onCompleted: root.launchHelperDetached()\n"
if text.count(old) != 1:
    raise SystemExit("stable-direct-state: Service launch seam changed")
path.write_text(text.replace(old, "  Component.onCompleted: {}\n"))
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
      var wrongKey = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
      service.activeHelperProtocol = 14
      service.helperCompatibility = "compatible"
      service.friendsReady = true
      service.friends = [{ id: "1", key: key }]
      service.surfaces = [{ conversation: "1", key: key, monitor: "DP-1",
        x: 10, y: 20, width: 420, height: 420, pinned: true }]
      var tick = service.surfacesTick
      service.handleLine(JSON.stringify({ event: "surface", conversation: "1",
        key: key, monitor: "DP-1", x: 179, y: 232, width: 500, height: 600,
        pinned: false }))
      var current = service.surfaces[0]
      var valid = service.surfaces.length === 1 &&
        current.x === 179 && current.y === 232 && current.width === 500 &&
        current.height === 600 && current.pinned === false &&
        service.surfacesTick === tick
      var rejected = !service.applySurfaceEvent({ conversation: "1", key: wrongKey,
        monitor: "DP-1", x: 900, y: 900, width: 700, height: 700,
        pinned: true }) && service.surfaces[0].x === 179
      var legacy = service.applySurfaceEvent({ conversation: "1", key: key,
        monitor: "DP-1", x: 180, y: 233, pinned: true })
      current = service.surfaces[0]
      valid = valid && rejected && legacy && current.x === 180 &&
        current.y === 233 && current.width === 500 && current.height === 600 &&
        current.pinned === true && service.surfacesTick === tick
      var wideLayout = service.applySurfaceEvent({ conversation: "1", key: key,
        monitor: "DP-1", x: 40000, y: -40000, width: 500, height: 600,
        pinned: false })
      current = service.surfaces[0]
      valid = valid && wideLayout && current.x === 40000 && current.y === -40000 &&
        current.pinned === false && service.surfacesTick === tick
      console.log(valid ? "OMAQ_SURFACE_CACHE_OK" : "OMAQ_SURFACE_CACHE_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 5 quickshell -p "$tmp/shell.qml" >"$out" 2>&1 ||
   ! grep -q 'OMAQ_SURFACE_CACHE_OK' "$out"; then
  cat "$out" >&2
  echo "stable-direct-state: authoritative surface cache fixture failed" >&2
  exit 1
fi

echo "stable-direct-state: ok"
