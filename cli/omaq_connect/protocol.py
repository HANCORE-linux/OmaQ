PROTOCOL_MIN = 16


LINE_MAX = 4096


TEXT_MAX = 511
REPLY_ID_MAX = 79
FILE_MAX = 8 * 1024 * 1024


HISTORY_LIMIT = 50


FAILURES = {
    "offline": "Contact is offline",
    "ratchet_pending": "Secure session is being established",
    "ratchet_timeout": "No secure session yet; message not sent",
    "no_ratchet": "Secure session unavailable; pair this contact again",
    "history_failed": "Message could not be saved or sent",
    "busy": "Helper is busy",
    "helper_incompatible": "Helper update required",
    "locked": "Identity is locked",
    "identity_changed": "Identity changed; try again",
    "delivery_unknown": "Delivery status unknown; check with the recipient",
    "forbidden": "Message was rejected",
}


DIRECT_DIGITS = frozenset("0123456789")


REDEEMED_DIRECT = "Invite checked. Waiting for the other person to accept."


REDEEMED_GROUP = "Group invite checked. Waiting for the incoming group request."


HISTORY_ITEM_FAILED = "An item could not be added to chat history"


UNSAVED_SENT = "Message sent, but could not be saved locally."


DELETED = "Message deleted"


EDITED = "(edited)"


FILE_ERRORS = {
    "busy": "A file is already sending",
    "forbidden": "The file could not be sent",
    "unsupported": "The file path is not supported",
    "history_failed": "File delivered, but local history could not be saved",
}


INVITE_ERRORS = {
    "invite_expired": "Invite expired. Create a new link.",
    "invite_self": "This invite belongs to your own identity.",
    "contact_exists": "This person is already in your contacts. Remove the old contact on both devices before using a fresh invite.",
    "contact_limit": "The direct-contact limit has been reached.",
    "invite_rejected": "The invite could not be added. Create a fresh invite and try it once on the other device.",
    "nospam_rotate_failed": "The invite was cleared, but its one-use address could not be refreshed.",
    "safety_key_changed": "This contact's encryption identity changed. Remove the old contact state on both devices before exchanging a fresh invite.",
    "direct_state_reinvite_required": "Your OmaQ identity and contacts are intact. Some older direct-chat encryption state was safely archived and requires fresh invites.",
    "group_registry_failed": "Could not safely save private group state.",
    "unsupported": "That is not an OmaQ invite.",
    "busy": "Finish active calls, transfers, invites, or read receipts first.",
    "locked": "Unlock your identity to continue.",
}


PASSPHRASE_BYTES = 128
NEW_PASSPHRASE_CHARACTERS = 8
ONE_LINE = "Enter the passphrase as one line"


def passphrase_ok(value, new=False):
    if not value or len(value.encode("utf-8", "surrogatepass")) > PASSPHRASE_BYTES:
        return False
    if any((ord(ch) < 0x20 and ch != "\t") or ord(ch) == 0x7F or 0xD800 <= ord(ch) <= 0xDFFF for ch in value):
        return False
    return not new or len(value) >= NEW_PASSPHRASE_CHARACTERS


def secret_edit(value, kind, typed):
    if kind == "key" and typed == "backspace":
        return value[:-1], ""
    if kind == "key" and typed == "tab":
        typed, kind = "\t", "text"
    if kind not in ("text", "paste"):
        return value, ""
    if any(ch in "\r\n" or (ord(ch) < 0x20 and ch != "\t") or ord(ch) == 0x7F for ch in typed):
        return value, ONE_LINE
    joined = value + typed
    if len(joined.encode("utf-8", "surrogatepass")) > PASSPHRASE_BYTES:
        return value, "A passphrase has at most 128 bytes"
    return joined, ""


def safety_ok(value):
    halves = str(value or "").split(" / ")
    if len(halves) != 2:
        return False
    for half in halves:
        groups = half.split(" ")
        if len(groups) != 16 or not all(len(g) == 4 and all(ch in "0123456789abcdef" for ch in g) for g in groups):
            return False
    return True


def hex64(value):
    value = str(value or "")
    return len(value) == 64 and all(ch in "0123456789abcdef" for ch in value)


def is_direct(conversation):
    return bool(conversation) and set(conversation) <= DIRECT_DIGITS and (conversation == "0" or conversation[0] != "0")


class Message:
    __slots__ = ("id", "request", "dir", "text", "ts", "sender", "ack", "failed", "local", "kind", "reply", "edited",
                 "deleted")

    def __init__(self, mid, request, direction, text, ts, sender="", ack=0, failed="", local=False, kind="", reply="",
                 edited=False, deleted=False):
        self.id, self.request, self.dir, self.text = mid, request, direction, text
        self.ts, self.sender, self.ack, self.failed, self.local, self.kind = ts, sender, ack, failed, local, kind
        self.reply, self.edited, self.deleted = reply, edited, deleted


def shown_text(m):
    return DELETED if m.deleted else m.text


def replyable(m):
    return (bool(m.id) and len(m.id.encode("utf-8", "surrogatepass")) <= REPLY_ID_MAX and m.dir in ("in", "out")
            and not (m.failed or m.local or m.kind or m.deleted) and bool(m.text.strip()))


def message_from(item):
    kind = str(item.get("kind") or "")
    text = str(item.get("text") or "")
    if kind in ("file", "image"):
        text = "[%s] %s" % (kind, text) if text else "[%s]" % kind
    direction = str(item.get("dir") or ("out" if item.get("from") == "me" else "in"))
    receipt = str(item.get("receipt") or "")
    ack = 3 if receipt == "read" else (2 if receipt == "delivered" else (1 if direction == "out" else 0))
    ts = item.get("ts") or 0
    try:
        ts = float(ts)
    except (TypeError, ValueError):
        ts = 0.0
    if ts > 1e12:
        ts /= 1000.0
    deleted = item.get("deleted") is True
    return Message(str(item.get("id") or ""), str(item.get("request") or ""), direction, "" if deleted else text, ts,
                   str(item.get("sender") or ""), ack, kind=kind if kind in ("file", "image") else "",
                   reply=str(item.get("reply") or ""), edited=item.get("edited") is True, deleted=deleted)
