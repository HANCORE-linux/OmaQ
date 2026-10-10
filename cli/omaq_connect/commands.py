import os


VERBS = [("help", False), ("connect", True), ("split", True), ("invite", False), ("redeem", True),
         ("requests", False), ("accept", False), ("decline", False), ("sendfile", True), ("acceptfile", False),
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
    (":sendfile ", "Send a file to this chat", "<path>"),
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
    ("tab", "Next chat in this pane; after : it completes commands and names"),
    ("up / down", "Earlier messages of this chat; after : earlier commands"),
    ("pgup / pgdn", "Scroll"),
    ("shift+enter", "New line in a message"),
    ("ctrl+k", "Choose an emoji"),
    ("esc", "Clear the message"),
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
]
NAMED = ("connect", "c", "vsplit", "vs", "split", "sp")
RECEIPTS = ["·", "→", "⇉", "⇉"]
