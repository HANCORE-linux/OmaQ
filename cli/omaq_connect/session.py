from .protocol import is_direct


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
