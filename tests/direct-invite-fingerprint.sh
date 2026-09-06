#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
helper = (root / "helper/omaq.c").read_text(encoding="utf-8")
invite = (root / "helper/invite.c").read_text(encoding="utf-8")
service = (root / "Service.qml").read_text(encoding="utf-8")
panel = (root / "Panel.qml").read_text(encoding="utf-8")
protocol = (root / "docs/stages/protocol-16.md").read_text(encoding="utf-8")
security = (root / "docs/SECURITY.md").read_text(encoding="utf-8")

for marker in (
    "#define OMAQ_PROTOCOL_VERSION 16",
    "static int emit_pending_direct_request(void)",
    "omaq_direct_request_event(event, sizeof(event), self, key)",
    "omaq_direct_request_conflict_event(event, sizeof(event), key)",
    "omaq_direct_redeemed_event(redeemed_event,",
    "omaq_invite_issue_clear(&g_pending_invite, &g_invite_conflicts,",
    "static void emit_recorded_direct_conflicts(void)",
    "omaq_invite_issue_busy(&g_pending_invite,",
    'emit_error("nospam_rotate_failed")',
):
    if marker not in helper:
        raise SystemExit(f"direct-invite-fingerprint: helper lost {marker!r}")

hook_start = helper.index("static void hook_req(")
hook_end = helper.index("\nstatic void hook_ginv(", hook_start)
hook = helper[hook_start:hook_end]
validation = [
    'sep = strstr(msg, "|rk=")',
    "strlen(sep + 4) != OMAQ_RK_HEX",
    "!omaq_rk_ok(sep + 4)",
    "g_issued_exp && now >= g_issued_exp",
    "if (g_pending_invite.used)",
    "omaq_invite_conflict_note(&g_invite_conflicts",
]
positions = [hook.index(marker) for marker in validation]
if positions != sorted(positions):
    raise SystemExit("direct-invite-fingerprint: conflict precedes token validation")
if "g_pending_invite.public_key, pk32" not in hook:
    raise SystemExit("direct-invite-fingerprint: conflict is not bound to pending and attempt keys")
if ("g_pending_announced" not in hook or
        hook.index("omaq_invite_conflict_note(&g_invite_conflicts") >
        hook.index("g_pending_announced")):
    raise SystemExit("direct-invite-fingerprint: delayed conflict is not recorded before gating")
announce = helper[helper.index("static void announce_pending_direct(void)"):
                  helper.index("\nstatic void clear_group_auth(void)")]
if "emit_recorded_direct_conflicts();" not in announce:
    raise SystemExit("direct-invite-fingerprint: delayed conflicts are not announced after request")

status_start = helper.index('if (strcmp(op->op, "status") == 0)')
status_end = helper.index('\n\tif (strcmp(op->op, "contact.remove") == 0)', status_start)
status = helper[status_start:status_end]
for marker in ("emit_pending_direct_request() == 0",
               "emit_recorded_direct_conflicts();"):
    if marker not in status:
        raise SystemExit(f"direct-invite-fingerprint: status lost pending replay {marker!r}")
contact_start = helper.index('if (strcmp(op->op, "contact.decide") == 0)')
contact = helper[contact_start:status_end]
if len(re.findall(r"clear_invite_and_emit\(\);\s+"
                  r"rotate_consumed_invite_nospam\(\);", contact)) != 2:
    raise SystemExit("direct-invite-fingerprint: accept/decline do not both revoke and rotate")
if "omaq_pending_invite_clear(&g_pending_invite)" in contact:
    raise SystemExit("direct-invite-fingerprint: decline still leaves the issued ID live")
for marker in ("OMAQ_JSON_FIELD_KEY | OMAQ_JSON_FIELD_ACCEPT",
               "omaq_pending_invite_key_matches(&g_pending_invite",
               "(op->field_mask & OMAQ_JSON_FIELD_KEY) != 0"):
    if marker not in contact:
        raise SystemExit(f"direct-invite-fingerprint: decision lost binding {marker!r}")
create_start = helper.index('if (strcmp(op->op, "invite.create") == 0)')
create_end = helper.index('\n\tif (strcmp(op->op, "invite.redeem") == 0)', create_start)
create = helper[create_start:create_end]
if create.count("clear_invite();") != 2:
    raise SystemExit("direct-invite-fingerprint: successful issue paths do not reset old type/state")
if "g_group_bind_proof.used &&\n\t\t\tg_group_bind_proof.pending_accept" not in create:
    raise SystemExit("direct-invite-fingerprint: settled group proof still blocks new issue")

for marker in (
    "void omaq_invite_issue_clear(",
    "int omaq_invite_issue_busy(",
    "int omaq_pending_invite_key_matches(",
    "int omaq_invite_conflict_note(",
    "int omaq_direct_request_event(",
    "int omaq_direct_request_conflict_event(",
    "int omaq_direct_redeemed_event(",
):
    if marker not in invite:
        raise SystemExit(f"direct-invite-fingerprint: invite model lost {marker!r}")

for marker in (
    "readonly property bool supportsInviteRequestSafety: root.activeHelperProtocol >= 16",
    "function validInviteSafetyCode(value)",
    'if (ev.event === "request.conflict")',
    'if (ev.event === "invite.redeemed")',
    'property string lastRedeemSafety: ""',
    'root.lastError = "helper_event_invalid"',
    "conflictKey !== root.pendingRequestKey",
    "function clearPendingRequest()",
    "operation.key = root.pendingRequestKey",
    "? root.sendImmediateOp(operation) : root.sendOp(operation)",
    "if (!protocol16Direct)",
    "Number(ev.expires || 0) === 0)",
):
    if marker not in service:
        raise SystemExit(f"direct-invite-fingerprint: Service lost {marker!r}")

pending_start = panel.index("              id: pendingRequestContent")
pending_end = panel.index("        Rectangle {\n          id: supportLinks", pending_start)
pending = panel[pending_start:pending_end]
for marker in (
    'text: omaq.pendingGroup ? "Group invite" : "Friend request"',
    'omaq.pendingRequestSafety.replace(" / ", "\\n")',
    'text: "Compare this code with your friend before accepting"',
    'text: "Another device used this invite link"',
    '"Decline and revoke link"',
    "onClicked: omaq.decide(false)",
):
    if marker not in pending:
        raise SystemExit(f"direct-invite-fingerprint: pending card lost {marker!r}")
for forbidden in ("omaq.lastAddr", "76-character", "onClicked: omaq.decide(true); omaq.decide"):
    if forbidden in pending:
        raise SystemExit(f"direct-invite-fingerprint: pending card exposes forbidden path {forbidden!r}")
redeemed_start = panel.index("                id: redeemedInviteSafety")
redeemed_end = panel.index("\n            TokenButton {", redeemed_start)
redeemed = panel[redeemed_start:redeemed_end]
for marker in (
    'root.redeemSafety.replace(" / ", "\\n")',
    'text: "Compare this code with your friend before they accept"',
):
    if marker not in redeemed:
        raise SystemExit(f"direct-invite-fingerprint: redeemed card lost {marker!r}")
for marker in (
    'if (code === "nospam_rotate_failed")',
    'return "The invite was cleared, but its one-use address could not be refreshed."',
):
    if marker not in panel:
        raise SystemExit(f"direct-invite-fingerprint: error copy lost {marker!r}")
for document, marker in (
    (protocol, '"event":"invite.redeemed"'),
    (protocol, "Accepting or declining a direct request completely clears"),
    (security, "Accepting or declining a direct request retires"),
):
    if marker not in document:
        raise SystemExit(f"direct-invite-fingerprint: documentation lost {marker!r}")
PY

echo "direct-invite-fingerprint: ok"
