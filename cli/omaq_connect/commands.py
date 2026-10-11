import os
from dataclasses import dataclass


VERBS = [("help", False), ("connect", True), ("split", True), ("invite", False), ("redeem", True),
         ("requests", False), ("accept", False), ("decline", False), ("nickname", True), ("sendfile", True), ("acceptfile", False),
         ("declinefile", False), ("cancelfile", False), ("clear", False), ("close", False), ("quit", False)]
HINT = " " + "   ".join(":" + verb for verb in ("help", "connect", "split", "invite", "requests", "clear",
                                                   "close", "quit"))
COMMANDS = [
    (":help", "Show this list", ""),
    (":connect ", "Open a chat in a new pane on the right", "<name>"),
    (":split ", "Open a chat in a new pane below", "<name>"),
    (":invite", "Invite link and QR code; the link is copied", ""),
    (":invite revoke", "Revoke the active invite (asks first)", ""),
    (":redeem ", "Send a request with a friend's invite link", "<link>"),
    (":requests", "Show a pending friend or group request", ""),
    (":accept", "Accept the pending request", ""),
    (":decline", "Decline the pending request", ""),
    (":nickname ", "Change the name your friends see", "<name>"),
    (":sendfile ", "Send a file to this chat; tab lists files", "<path>"),
    (":acceptfile", "Save the offered file in ~/Downloads/omaq", ""),
    (":declinefile", "Decline the offered file", ""),
    (":cancelfile", "Cancel your file transfer in this chat", ""),
    (":clear", "Clear this chat on this computer (asks first)", ""),
    (":close", "Close this pane (also ctrl+w)", ""),
    (":quit", "Quit OmaQ (also :q)", ""),
]
KITTY = os.environ.get("TERM") == "xterm-kitty"
ZOOM_KEYS = "ctrl+shift+= / ctrl+shift+-" if KITTY else "ctrl+= / ctrl+-"
KEYS = [
    ("ctrl+n", "New pane with the next friend"),
    ("ctrl+w", "Close this pane"),
    ("alt+arrows", "Move the focus between panes"),
    ("alt+shift+arrows", "Move this pane"),
    ("alt+= / alt+-", "Make this column wider or narrower"),
    ("tab", "Next chat in this pane; after : completes commands, names and file paths"),
    ("up / down", "Earlier messages of this chat; after : earlier commands"),
    ("pgup / pgdn", "Scroll"),
    ("shift+enter", "New line in a message"),
    ("ctrl+k", "Choose an emoji"),
    ("esc", "Clear the message, then hide a file notice"),
    (ZOOM_KEYS, "Text size: your terminal's zoom (%s resets); 100%% is the size at start"
     % ("ctrl+shift+backspace" if KITTY else "ctrl+0")),
]
TIPS = [
    ("ctrl+k", "opens the emoji picker"),
    (":) <3", "turn into emoji when a space follows"),
    ("up", "recalls your earlier messages in this chat"),
    (":connect <name>", "opens a chat on the right"),
    (":split <name>", "opens a chat below"),
    ("ctrl+n", "opens the next friend in a new pane"),
    ("alt+arrows", "move between panes"),
    ("alt+shift+arrows", "move this pane"),
    ("alt+= / alt+-", "resize this column"),
    ("tab", "switches the chat in this pane"),
    ("shift+enter", "starts a new line"),
    (":invite", "shows your invite link and QR code"),
    (":redeem <link>", "sends a request with a friend's invite link"),
    (":requests", "shows a pending friend request"),
    (":accept / :decline", "answer a pending request"),
    (":sendfile <path>", "sends a file to this chat"),
    (":acceptfile", "saves an offered file in ~/Downloads/omaq"),
    (":clear", "clears this chat on this computer"),
    (":close / ctrl+w", "closes this pane"),
    (ZOOM_KEYS, "change the text size"),
    ("pgup / pgdn", "scroll the chat"),
    (":sendfile + tab", "lists your folders and files"),
]
NAMED = ("connect", "c", "vsplit", "vs", "split", "sp")
KNOWN = ("q", "q!", "quit", "quit!", "close", "clo", "help", "h", "clear", "invite", "redeem", "requests",
         "accept", "decline", "nickname", "sendfile", "acceptfile", "declinefile", "cancelfile") + NAMED
RECEIPTS = ["·", "→", "⇉", "⇉"]


def parse_command(line):
    parts = line[1:].strip().split(None, 1)
    verb = parts[0].lower() if parts else ""
    arg = parts[1].strip() if len(parts) > 1 else ""
    if verb and verb not in KNOWN:
        candidates = [name for name, _ in VERBS
                      if name.startswith(verb) and name not in ("accept", "decline", "nickname", "acceptfile", "declinefile")]
        if len(candidates) == 1:
            verb = candidates[0]
    return verb, arg


def suggestions(draft, names):
    body = draft[1:]
    if " " not in body:
        want = body.casefold()
        return "verb", body, [name for name, _ in VERBS if name.startswith(want)]
    verb, rest = body.split(" ", 1)
    if verb.lower() not in NAMED:
        return "none", rest, []
    want = rest.casefold()
    return "name", rest, [name for name in names if name.casefold().startswith(want)]


def sendfile_path(draft):
    if not draft.startswith(":") or " " not in draft:
        return None
    verb, rest = draft[1:].split(" ", 1)
    return rest if parse_command(":" + verb)[0] == "sendfile" else None


@dataclass(eq=False, slots=True)
class Completion:
    kind: str
    verb: str
    items: list
    index: int

    def step(self, step):
        self.index = (self.index + step) % len(self.items)
        choice = self.items[self.index]
        return (":" + choice) if self.kind == "verb" else ":%s %s" % (self.verb, choice)
