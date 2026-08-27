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
        "host.acceptOpenRequest(conversation, expectedKey, name)" not in coordinator:
    raise SystemExit("stable-direct-state: queued chat opens lost their key binding")

if "omaq.answerCall(omaq.lastCallConv, omaq.lastCallKey)" not in panel or \
        "omaq.stopCall(omaq.lastCallConv, omaq.lastCallKey)" not in panel:
    raise SystemExit("stable-direct-state: Panel call actions lost their key binding")

if "property string peerKey" not in page or "root.directBindingValid" not in page or \
        "onPeerKeyChanged: {\n    lines.clear()" not in page or \
        "service.sendConversationOp({ op: \"msg.send\"" not in page:
    raise SystemExit("stable-direct-state: ChatPage direct actions are not key-bound")

required_helper = [
    "#define OMAQ_PROTOCOL_VERSION 11",
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

echo "stable-direct-state: ok"
