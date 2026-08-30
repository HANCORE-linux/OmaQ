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
group_file_store = (root / "helper/group_file_store.c").read_text()
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
manage_start = page.index("function mayManageGroupMember(member)")
manage_end = page.index("function selectGroupInviteFriend", manage_start)
manage = page[manage_start:manage_end]
if "!member.online" in manage or \
        '(selfRole === "admin" && targetRole === "member")' not in manage:
    raise SystemExit("group-chat-parity: admin removal remains online-only or role-blind")
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
group_message_start = helper.index("static void hook_gmsg(")
group_message_end = helper.index("static void persist_forced_group_removal", group_message_start)
group_message = helper[group_message_start:group_message_end]
direct_message_start = helper.index("static void hook_msg(")
direct_message_end = helper.index("\n}\n#endif\n", direct_message_start) + 2
direct_message = helper[direct_message_start:direct_message_end]
for name, callback in (("group", group_message), ("direct", direct_message)):
    if "omaq_message_rate_allow" not in callback or \
            callback.index("omaq_message_rate_allow") > callback.index("omaq_message_wire_unpack") or \
            "omaq_store_message_id_used" in callback:
        raise SystemExit(f"group-chat-parity: {name} message admission is not bounded before one indexed append")
if "omaq_message_text_bytes_ok" not in direct_message or \
        direct_message.index("omaq_message_text_bytes_ok") > direct_message.index("OQX1|gmbd|"):
    raise SystemExit("group-chat-parity: decrypted direct text is not validated before control parsing")
receive_start = helper.index("static void group_file_receive_offer(")
receive_end = helper.index("static int group_file_accept(", receive_start)
receive_offer = helper[receive_start:receive_end]
if "omaq_group_file_id_reserve" in receive_offer or \
        "omaq_store_message_id_used" in receive_offer:
    raise SystemExit("group-chat-parity: unaccepted offers still mutate or scan durable state")
if receive_offer.index("omaq_group_file_offer_rate_allow") > \
        receive_offer.index("omaq_group_file_offer_unpack") or \
        "!g_group_file_in[i].accepted" not in receive_offer:
    raise SystemExit("group-chat-parity: offer admission is not early or sender-fair")
accept_start = receive_end
accept_end = helper.index("static int group_file_cancel(", accept_start)
accept = helper[accept_start:accept_end]
if "omaq_group_file_id_reserve(state_dir(), id)" not in accept or \
        accept.index("omaq_group_file_id_reserve(state_dir(), id)") > \
        accept.index("omaq_file_download_create"):
    raise SystemExit("group-chat-parity: incoming id is not reserved before acceptance")
if "id_store_v2_header" not in group_file_store or \
        "OMAQ_GROUP_FILE_ID_STORE_LIMIT - 1" not in group_file_store:
    raise SystemExit("group-chat-parity: id-store migration/compaction is missing")
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
remove_start = helper.index('if (strcmp(op->op, "group.member.remove") == 0)')
remove_end = helper.index('if (strcmp(op->op, "group.leave") == 0)', remove_start)
remove = helper[remove_start:remove_end]
if remove.index("omaq_role_may(self, ACT_KICK, victim)") > \
        remove.index("group_binding_forget_member(gid, member_key)"):
    raise SystemExit("group-chat-parity: denied moderation mutates binding state first")
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
