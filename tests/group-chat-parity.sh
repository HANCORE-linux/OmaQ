#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
page = (root / "pages/ChatPage.qml").read_text()
service = (root / "Service.qml").read_text()
panel = (root / "Panel.qml").read_text()
helper = (root / "helper/omaq.c").read_text()
tox = (root / "helper/tox_adapt.c").read_text()

presence = (
    "informational: true",
    'informationalIconColor: memberButton.modelData.online ? "#7dce6a"',
    "informationalIconFill: memberButton.modelData.online ? 1 : 0",
)
if not all(value in page for value in presence):
    raise SystemExit("group-chat-parity: member presence row lost its filled online state")

invite = (
    "function selectGroupInviteFriend(friend)",
    "function sendGroupInvite()",
    "root.service.groupInviteCandidateMatches(root.conversation",
    "root.service.inviteToGroup(root.groupInviteFriendId,",
    "onClicked: root.sendGroupInvite()",
)
if not all(value in page for value in invite):
    raise SystemExit("group-chat-parity: GroupChat invite path diverged from Service validation")
if "function groupInviteCandidateMatches(groupId, friendId, expectedKey)" not in service:
    raise SystemExit("group-chat-parity: authoritative invite candidate validation missing")
if "return omaq.groupInviteCandidateMatches" not in panel:
    raise SystemExit("group-chat-parity: Panel and GroupChat invite validation diverged")

attachments = (
    "readonly property bool attachmentsAvailable: !root.groupConversation",
    "enabled: !root.demo && root.attachmentsAvailable",
    "visible: root.attachmentsAvailable",
    "root.service.supportsGroupAttachments",
)
if not all(value in page for value in attachments):
    raise SystemExit("group-chat-parity: GroupChat composer attachment parity missing")
if "Files are available in direct chats only" in page:
    raise SystemExit("group-chat-parity: obsolete Direct-only attachment gate remains")
if "readonly property bool supportsGroupAttachments: root.activeHelperProtocol >= 12" not in service:
    raise SystemExit("group-chat-parity: Protocol-12 capability gate missing")
if '"file.accept", "file.cancel"' not in service:
    raise SystemExit("group-chat-parity: Group operations do not admit file lifecycle operations")
if "groupById(c)" in service:
    raise SystemExit("group-chat-parity: transient QML group projection still authorizes file operations")
if 'import "../Emoji.js" as Emoji' not in page or \
        "Emoji.splitEmojiOnly" not in page:
    raise SystemExit("group-chat-parity: arbitrary Unicode emoji-only messages are not enlarged")

wire = (
    "group_file_send_begin(cid, op->path",
    "group_file_accept(op->conversation, op->id, over)",
    "group_file_cancel(op->conversation, op->id)",
    "group_file_pump();",
)
if not all(value in helper for value in wire):
    raise SystemExit("group-chat-parity: helper-owned group transfer lifecycle incomplete")
if "omaq_message_id_reserved(wire_id)" not in helper or \
        "omaq_group_file_id_reserve(state_dir()" not in helper:
    raise SystemExit("group-chat-parity: group-file message namespace is not durably reserved")
if '"accept_failed"' not in helper or \
        'group_file_in_fail(index, "failed", "local_history_failed", 1)' not in helper:
    raise SystemExit("group-chat-parity: failed GroupChat acceptance/history is not terminally projected")
if "if (g_group_file_in[index].completed)\n\t\treturn;" not in helper or \
        "if (incoming->completed) {" not in helper:
    raise SystemExit("group-chat-parity: completed GroupChat downloads are not idempotent")
if "tox_group_send_custom_private_packet" not in tox or \
        "tox_callback_group_custom_private_packet" not in tox:
    raise SystemExit("group-chat-parity: private lossless NGC transport seam missing")

projection = (
    'readonly property bool supportsCorrelatedGroupProjection: root.activeHelperProtocol >= 13',
    'return root.sendOp({ op: "group.list", id: root.expectedGroupRequest })',
    'String(ev.instance || "") !== root.helperInstance',
    'root.pendingGroupReceivedMembers !== root.pendingGroupExpectedMembers',
)
if not all(value in service for value in projection):
    raise SystemExit("group-chat-parity: restart-safe group projection is not correlated")
if r'\"instance\":\"%s\"%s,\"groups\":%d,\"members\":%d' not in helper or \
        'request_field' not in helper or 'strcmp(op->op, "group.list")' not in helper:
    raise SystemExit("group-chat-parity: helper group snapshot contract is incomplete")
if "group_typing_magic" not in helper or "groupTypingActors" not in service or \
        "groupTypingNames" not in page:
    raise SystemExit("group-chat-parity: helper-bound GroupChat typing is missing")
if "omaq_store_update_group_receipt_changed" not in helper or \
        "lastReceiptActor" not in service or "groupReceiptSummary" not in page:
    raise SystemExit("group-chat-parity: per-member GroupChat receipts are missing")

# Calls must remain explicitly Direct-only.
guide = (root / "docs/USER-GUIDE.md").read_text()
plan = (root / "docs/PLAN.md").read_text()
if "files remain unavailable in group chats" in guide or \
        "group file transfer remains unavailable" in guide.lower() or \
        "group file transfer remains unavailable because" in plan.lower():
    raise SystemExit("group-chat-parity: user guide still denies group attachments")

if 'if (!c || c.charAt(0) === "g")' not in service or \
        "readonly property bool directConversation:" not in page:
    raise SystemExit("group-chat-parity: GroupChat call exclusion was weakened")
PY

echo "group-chat-parity: ok"
