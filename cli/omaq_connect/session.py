from dataclasses import dataclass, field

from .protocol import UNSAVED_SENT, Message, is_direct, message_from


@dataclass(eq=False, slots=True)
class Conversation:
    items: list = field(default_factory=list)
    loaded: bool = False
    request: str = ""
    typing_until: float = 0.0


def friend_of(friends, fid):
    for f in friends:
        if f["id"] == fid:
            return f
    return None


def key_for(friends, conversation):
    f = friend_of(friends, conversation) if is_direct(conversation) else None
    return f["key"] if f else ""


def bound(friends, ev):
    conversation = str(ev.get("conversation") or "")
    if not is_direct(conversation):
        return True
    f = friend_of(friends, conversation)
    return bool(f) and bool(f["key"]) and str(ev.get("key") or "") == f["key"]


def rebound(old, new):
    old_keys = {f["id"]: f["key"] for f in old}
    new_keys = {f["id"]: f["key"] for f in new}
    by_key = {f["key"]: f["id"] for f in new if f["key"]}
    return [(fid, by_key.get(key, "")) for fid, key in old_keys.items() if new_keys.get(fid) != key]


def merge_history(current, items, pending):
    keep = [m for m in current if m.local or (m.dir == "out" and m.request and (m.failed or not m.id))]
    keep += [m for m in pending if m not in keep]
    merged = [message_from(i) for i in items if isinstance(i, dict)]
    ids = {m.id for m in merged if m.id}
    merged.extend(m for m in keep if not (m.id and m.id in ids))
    return merged


def find_sent(items, request):
    found = None
    for m in items:
        if m.dir == "out" and m.request == request:
            found = m
    return found


def mark_unsaved(items, m, now):
    m.failed, m.ack, m.local = "", max(m.ack, 1), True
    note = Message("", "", "sys", UNSAVED_SENT, m.ts or now, local=True)
    items.insert(items.index(m) + 1 if m in items else len(items), note)
