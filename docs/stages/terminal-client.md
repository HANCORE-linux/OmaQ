# Terminal client: omaq-connect

**Status:** built on `feat/cli`, not released.
**Date:** 2026-10-10

`omaq-connect` is a text-only OmaQ client for any terminal. It is one more client of the helper socket beside the bar, so the bar and its behavior stay unchanged.

## Landed

- `cli/omaq.py`, Python 3.11 or newer, standard library only. It speaks the helper's JSON-line protocol on `$OMAQ_STATE/omaq.sock` exactly like `Service.qml`:
  - **Handshake:** `status` with a nonce, and a snapshot correlated by `request`.
  - **Protocol gate:** Protocol 16 or newer.
  - **Key binding:** every direct operation carries the stable `key`, and direct events whose `key` does not match the friend are dropped.
- **Usage:**
  - `omaq-connect` opens the chat with the first friend; `omaq-connect <name>` opens it with that friend.
  - The friends bar shows online, AFK and offline dots. Names longer than 18 columns end in `…`. Friends that no longer fit are summarized as `+N`, drawn in the unread color when one of them has unread messages. A friend with unread messages whose chat is not on screen gets a name filled in the unread color until the chat is read; there are no numeric badges. A new message from a chat that is not on screen also shows its first line in the status line for 8 seconds.
  - Each pane header shows `online`, `typing…` or `offline` beside the name. The focused pane's header starts with `❯`, and the input line shows the chat partner's name on an accent background, followed by `❯`.
  - Own messages end in a compact receipt that never regresses: `·` sending, `→` sent, `⇉` delivered (dim), `⇉` read (green, bold). A failure shows `✕` and the bar's failure text. `:help` lists the receipt legend.
  - Every message shows only `HH:MM`. A dim divider (`── today ──`, `── yesterday ──`, `── Thu 8 Oct ──`, with the year when it differs) marks each new day.
- **Split views:**
  - Panes are arranged in columns. `:connect <name>` (or `ctrl+n`) opens a pane on the right, `:split <name>` opens one below.
  - `alt+arrows` (also `alt+h/j/k/l`) move the focus, `alt+shift+arrows` move the pane, `alt+=`/`alt+-` resize the column, and `tab` switches the pane's chat.
  - Too small a terminal shows one pane with a tab strip.
- **Commands:**
  - `:quit` (also `:q`) quits OmaQ, `:close` (also `ctrl+w`) closes the pane.
  - `:help` (or `F1`) lists the commands to select with the arrow keys.
  - `ctrl+c` clears the draft and shows `Type :quit and press Enter to quit`; it never quits.
  - `up`/`down` browse this chat's own messages, newest first, including unsent and failed ones, so they never cross into another chat. After `:` they browse earlier commands. Going below the newest entry restores the draft that was there before.
  - `:clear` asks `Clear this chat? y/n` for 15 seconds; an expired or vanished prompt never consumes a key. On `y` it sends the request-correlated `history.clear` like the bar's Clear chat (only with `historyClear` 2 in the snapshot). It removes this conversation's history on this computer only; the helper also clears its unread state. The result (`history.clear.succeeded`/`failed`) is matched by request, key and helper instance, as in the bar. An open bar chat window ignores this foreign result until it is reopened.
  - The bottom line keeps `:help` and `:quit` and shows one short tip beside them, such as `ctrl+k opens the emoji picker`. The 19 tips cover the other commands and keys; one changes every 8 seconds while the terminal has focus and no command is being typed. When the line gets narrow, the tip goes first, then the text size; `:help` and `:quit` stay. Typing `:` turns it into filtered suggestions with the selection highlighted. `tab`/`shift+tab` cycle commands, and friend names after `:connect`/`:split `. A unique prefix runs its command (`:he` opens help).
- **Friends:**
  - `:invite` sends `invite.create` (direct, 24 hours) like the bar's invite page. It shows the link and a QR code drawn by `/usr/bin/qrencode -t UTF8` (the link follows `--`). With an Omarchy theme the code is truecolor white on black, so a theme's palette can never invert it. The validity countdown refreshes every minute and then shows that the invite expired. The link goes to the clipboard through OSC 52, which the terminal handles, also over SSH. An active, unexpired invite is shown again instead of being replaced. `:invite revoke` asks `Revoke this invite? It cannot be used again. y/n`, the bar's wording, and sends `invite.revoke`.
  - `:redeem <omaq:// link>` sends `invite.redeem` with a request id; links over 511 bytes, the helper's payload limit, are refused locally. `invite.redeemed` shows the bar's result ("Invite checked. Waiting for the other person to accept." or the group variant) and the safety code the friend will see.
  - Errors use the bar's wording, such as "That is not an OmaQ invite.". The helper broadcasts uncorrelated errors to every client, so the CLI shows one only within 10 seconds of its own invite, redeem, accept or decline; errors caused by the bar stay in the bar. A failed send says "Not connected" or "Could not send the request".
  - A pending request (the `request` event, which every `status` repeats) shows `1 request` or `1 group invitation` at the front of the friends bar. `:requests` shows it with the safety code ("Compare this code with your friend before accepting") and the bar's `request.conflict` warning "Another device used this invite link".
  - `:accept` and `:decline` send `contact.decide`, with the request key for a direct request, like the bar. A direct request disappears when the helper clears the invite; a group invitation disappears at once. Both commands need the full word.
  - Group invitations can be accepted, but group chats stay in the bar for now.
- **Files:**
  - `:sendfile <path>` sends `file.send` (kind `file`) with the friend's stable key. Relative paths, `~` and quoted paths work.
  - The client first checks for a readable regular file and a path of at most 511 bytes, the helper's field limit. The helper keeps the final say.
  - One transfer per chat, as in the bar ("A file is already sending"). `file.sending` supplies the transfer id. `:cancelfile` sends `file.cancel`, and waits for that id if it has not arrived yet.
  - A `file.offer` bound to the friend's key is held per chat; a second offer is declined, as in the bar. The chat shows `⇣ File offer: name (size)` with `:acceptfile or :declinefile`, the status line names the sender, and a friend whose chat is off screen is highlighted.
  - Incoming files stay paused until accepted. `:acceptfile` sends `file.accept` without a path, so the helper picks its default folder: `$OMAQ_DOWNLOAD_DIR/omaq` or `$XDG_DOWNLOAD_DIR/omaq`, else `~/Downloads/omaq`. The status line shows the saved path. `:declinefile` sends `file.cancel`.
  - Results (`file.done`, `file.failed`, `file.canceled`) are matched by request or transfer id and key, and use the bar's wording. After a transfer the chat reloads its history. A dropped connection clears transfer state with a notice.
  - The helper reports no progress, so the client shows `Sending …` without a percentage.
- **Emoji:**
  - `:)` `:D` `;)` `:(` `:'(` `:P` `;P` `:O` `:*` `:/` `:|` (also with a `-` nose), `xD`, `^^`, `<3` and `(y)` become 🙂 😀 😉 🙁 😢 😛 😜 😮 😘 😕 😐 😆 😊 ❤️ 👍.
  - An emoticon converts only when it stands alone and a space or a new line follows; on Enter the whole message is converted. Links such as `https://` stay intact.
  - A draft that starts with an emoticon is a message, not a command; command lines are never converted.
  - `ctrl+k` opens a picker above the input line with the same emoji as the bar's composer picker (`composerEmojiSet` in `pages/ChatPage.qml`: the 22 reaction emoji followed by 🤟); a unit test keeps both lists equal. Arrows choose, Enter inserts at the cursor and Esc closes. Typing searches the English Unicode names of the single-code-point wide symbols in the emoji ranges (U+2300–23FF, U+2600–27BF, U+2B00–2BFF, 〽 ㊗ ㊙ and U+1F000–1FAFF), such as `fire` or `star`.
  - At startup the client measures how the terminal draws `❤️` (U+2764 U+FE0F) and a ZWJ sequence with two cursor position reports (`CSI 6n`), and adopts both widths. Alacritty draws `❤️` in 1 column, while Ghostty, foot and kitty use 2 and draw ZWJ and skin-tone sequences as one 2-column cluster. Without an answer within 0.3 seconds, U+FE0F counts as 2 columns and ZWJ sequences per code point. The cursor, backspace and delete treat a character and its zero-width followers as one unit.
- **Text size:**
  - The terminal owns the font size, so the CLI never changes it. `ctrl+=`/`ctrl+-` (kitty: `ctrl+shift+=`/`ctrl+shift+-`) zoom the terminal, and `:help` names the keys for the terminal it runs in.
  - The client derives the cell height from the terminal's pixel size (`TIOCGWINSZ`, `ypix // rows`). The size at start is 100%. Only after a zoom does the bottom line show the new size at its right end for 4 seconds, such as `Text 112%`: in the accent color, or dim when back at 100%. It gives way while a command is typed and when the line is too narrow.
  - The probe found pixel sizes in Alacritty, Ghostty, foot and kitty. A terminal that reports no pixels shows no size.
- **Read state:** panes that actually show message rows are marked read with `conversation.read`, and only while the terminal has focus (focus reporting `CSI ?1004`). A header-only pane never counts. Unread state stays helper-authoritative, so the bar follows.
- **Stable identity:**
  - Drafts belong to the contact's stable key, not to the pane, so `tab` never carries a draft to another friend.
  - When a Tox friend number is rebound to a different key, its history, unread state and in-flight sends are purged, as the bar's `purgeReboundConversation` does, and a pane follows the contact to its new number.
  - Every handshake sends `identity.ready` with the instance, as the bar does, so the client keeps working after an identity import, which resets every client's readiness. `identity` `import`/`recovered` events and a snapshot for a different address rebuild the client state. An `identity_changed` error, which the helper broadcasts to every client, only re-handshakes and reloads the histories; panes and drafts stay, as in the bar.
- **Reconnects:** a dropped socket marks every loaded history stale and every in-flight send `delivery_unknown`, as the bar does. After the next handshake every open pane reloads its history. `history.failed` retries after 3 seconds.
- **Typing:** `typing.set` while composing, with idle and send stops.
- **Locked identity:** a hidden passphrase prompt sends `identity.unlock`; a wrong passphrase reports the bar's wording.
- **Narrow panes:** a pane too narrow for the time-and-name prefix shows that metadata on its own line and keeps the message body. Names are shortened with `…`. The single-pane tab strip keeps the focused tab visible.
- **Cursor:** the client asks for a blinking block (`CSI 1 SP q`); the terminal keeps it steady while typing. Exit resets it to the terminal's configured style (`CSI 0 SP q`).
- **Cleanup:** terminal restoration runs in an unconditional `finally` with SIGTERM, SIGHUP and SIGINT blocked during cleanup, and it closes synchronized output (`CSI ?2026l`).
- **Untrusted text:**
  - Peer messages, names and senders pass through `clean()` before reaching the terminal. C0 controls become control pictures, DEL becomes `␡`, and C1 and bidi controls become U+FFFD.
  - The helper itself rejects ESC/C0 in message bodies (`message.c`) and filters names with `omaq_group_member_name_bytes_ok`. `\r`, DEL, C1 and bidi controls in bodies still pass, which is why `clean()` is needed.
- **Outgoing text:**
  - It is limited to 511 bytes, the size of the helper's `omaq_op.text`.
  - It is serialized with raw UTF-8, because the helper JSON parser accepts only `\n`, `\t`, `\"` and `\\` escapes.
- **Colors:**
  - With Omarchy, the bar's System roles come from `colors.toml` in truecolor (name `color03`, status `color02`, unread `color01`, `accent`, online `#7dce6a`).
  - Like the bar's `parseSystemColors`, semantic names (`red`, `green`, `yellow`, `background`, `foreground`) are read first and `color0`–`color7` override them. 52 of 142 theme files on the development machine define only the semantic names.
  - Without Omarchy, the terminal's own ANSI palette is used.
- **Helper lifecycle:**
  - Without a running helper, the client starts `helper/omaq` detached, as the bar does. It first creates missing OmaQ home and state directories with mode `0700`; existing ones must already be private, otherwise it shows the helper's `chmod 700` advice. Helper start errors go to `$XDG_RUNTIME_DIR/omaq-cli/helper.log`, and the last line is shown in the status line.
  - Ownership requires the launch nonce: `omaq.protocol` must carry the client's `OMAQ_PROTOCOL_NONCE`, its child's pid and the handshake instance. A helper is stopped only when an `omaq-connect` started that instance (`started` marker with instance and nonce) and no other `omaq-connect` is alive.
  - The bar must also not be using it. When the client uses the bar's default state directory, it asks `omarchy-shell hancore.omaq status`; success or a timeout keeps the helper running.
  - When the last client process that started it exits, it sends `helper.shutdown_if_no_groups` and waits for the correlated acknowledgement; the helper cancels a shutdown whose requester disconnects first.
  - Group membership blocks the shutdown, and the client says so after leaving the screen.
  - A helper started by the bar or by anything else is never stopped, and the client never starts a competing helper while that helper restarts.
  - Instance tracking uses flock files in `$XDG_RUNTIME_DIR/omaq-cli`.
- **The command:**
  - `omaq-connect --link` creates `~/.local/bin/omaq-connect` atomically, and only over a missing path or an existing OmaQ link. `--unlink` removes only OmaQ's link.
  - `install.sh` (live-root path) and `scripts/install-omaq.py` (source install) call `--link` after a successful installation and only warn on failure.
  - `scripts/uninstall-omaq.sh` removes the link after a successful plugin removal when it points to `…/hancore.omaq/cli/omaq.py` and its target is gone. A link to a still existing tree, or to anything else, stays.
  - Updating an existing installation does not create the link; run `~/.config/omarchy/plugins/hancore.omaq/cli/omaq.py --link` once.

## How to check

```text
python3 -I tests/cli_unit_test.py
python3 -I tests/cli-pty.py
python3 -I tests/cli-two-homes.py
python3 -I tests/cli-split.py
python3 -I tests/cli-invite.py
```

- `cli_unit_test.py` (66 tests, also run by `make test` and `make test-ci`) pins each review finding: rebound numbers, drafts, ownership nonce, the bar probe, history retry and reconnect reload, `identity.ready`, private directories, header-only panes, narrow panes and the tab strip, interrupted cleanup, semantic palettes and the receipt glyphs.
- `cli-pty.py` needs no peer; its helper still bootstraps Tox. It covers the self-started helper and its shutdown, the command line, `:help`, `ctrl+c`, paste, resize, SIGTERM, SIGHUP, a locked identity with wrong and correct passphrases, and a foreign helper restart. It also checks that the pty is back in canonical, echoing mode.
- `cli-two-homes.py` and `cli-split.py` need the Tox network. They pair two or three throwaway identities and check delivery, `Delivered`/`Read`, typing, sanitizing, read marking and two side-by-side panes. `cli-two-homes.py` also recalls the last message with `up`, clears the chat through the real helper and sends `:) hallo <3`, which must arrive as `🙂 hallo ❤️`. It also sends a 3000-byte file with `:sendfile` and receives one with `:acceptfile`, compares the bytes, and checks that both copies stay in the test download folders. `cli-invite.py` pairs two CLIs with `:invite`, `:invite revoke`, a fresh `:invite`, `:redeem`, `:requests` and `:accept`, and checks the clipboard write and the shared safety code.

## Known open points (review package B)

- **Exit coordination:** the `started` marker is not scoped by state directory, and simultaneous exits are not serialized; both fail safe by leaving the helper running.
- **Races:** link creation and removal still have a check-then-act race, and the socket peer is not checked with `SO_PEERCRED`.
- **Failure display:** `message.failed` with `delivered:true` after an acknowledged send is not shown ("Message sent, but could not be saved locally.").
- **Width:** emoji widths follow the startup measurement. Regional-indicator flags and other multi-code-point sequences besides U+FE0F, ZWJ and skin tones are still measured per code point.
- **Group exits:** a group member's CLI-only exit keeps the helper online, and the note appears only after the screen is left.

## Stays out

- Group chats, creating and leaving groups, and contact removal.
- Calls, reactions, edits, deletes and search.
- The keyboard-and-hands visual.
