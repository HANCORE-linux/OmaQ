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
  - The friends bar shows online, AFK and offline dots. A friend with unread messages is underlined; there are no numeric badges.
  - Each pane header shows `online`, `typing…` or `offline` beside the name.
  - Own messages end in a compact receipt that never regresses: `·` sending, `→` sent, `⇉` delivered (dim), `⇉` read (green, bold). A failure shows `✕` and the bar's failure text. `:help` lists the receipt legend.
  - Timestamps are `HH:MM` today and `YYYY-MM-DD HH:MM` before.
- **Split views:**
  - Panes are arranged in columns. `:connect <name>` (or `ctrl+n`) opens a pane on the right, `:split <name>` opens one below.
  - `alt+arrows` (also `alt+h/j/k/l`) move the focus, `alt+shift+arrows` move the pane, `alt+=`/`alt+-` resize the column, and `tab` switches the pane's chat.
  - Too small a terminal shows one pane with a tab strip.
- **Commands:**
  - `:q` closes the pane, and the last pane quits. `:qa` quits.
  - `:help` (or `F1`) lists the commands to select with the arrow keys.
  - `ctrl+c` only shows `Type :qa and press Enter to quit`.
  - The bottom line lists only `:help   :connect   :split   :quit   :qa`. Typing `:` turns it into filtered suggestions with the selection highlighted. `tab`/`shift+tab` cycle commands, and friend names after `:connect`/`:split `. A unique prefix runs its command (`:he` opens help).
- **Read state:** panes that actually show message rows are marked read with `conversation.read`, and only while the terminal has focus (focus reporting `CSI ?1004`). A header-only pane never counts. Unread state stays helper-authoritative, so the bar follows.
- **Stable identity:**
  - Drafts belong to the contact's stable key, not to the pane, so `tab` never carries a draft to another friend.
  - When a Tox friend number is rebound to a different key, its history, unread state and in-flight sends are purged, as the bar's `purgeReboundConversation` does, and a pane follows the contact to its new number.
  - Every handshake sends `identity.ready` with the instance, as the bar does, so the client keeps working after an identity import, which resets every client's readiness. `identity` `import`/`recovered` events and `identity_changed` errors rebuild the client state.
- **Reconnects:** a dropped socket marks every loaded history stale and every in-flight send `delivery_unknown`, as the bar does. After the next handshake every open pane reloads its history. `history.failed` retries after 3 seconds.
- **Typing:** `typing.set` while composing, with idle and send stops.
- **Locked identity:** a hidden passphrase prompt sends `identity.unlock`; a wrong passphrase reports the bar's wording.
- **Narrow panes:** a pane too narrow for the time-and-name prefix shows that metadata on its own line and keeps the message body. Names are shortened with `…`. The single-pane tab strip keeps the focused tab visible.
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
```

- `cli_unit_test.py` (37 tests) pins each review finding: rebound numbers, drafts, ownership nonce, the bar probe, history retry and reconnect reload, `identity.ready`, private directories, header-only panes, narrow panes and the tab strip, interrupted cleanup, semantic palettes and the receipt glyphs.
- `cli-pty.py` runs offline. It covers the self-started helper and its shutdown, the command line, `:help`, `ctrl+c`, paste, resize, SIGTERM, SIGHUP, a locked identity with wrong and correct passphrases, and a foreign helper restart. It also checks that the pty is back in canonical, echoing mode.
- `cli-two-homes.py` and `cli-split.py` need the Tox network. They pair two or three throwaway identities and check delivery, `Delivered`/`Read`, typing, sanitizing, read marking and two side-by-side panes.

## Known open points (review package B)

- **Exit coordination:** the `started` marker is not scoped by state directory, and simultaneous exits are not serialized; both fail safe by leaving the helper running.
- **Races:** link creation and removal still have a check-then-act race, and the socket peer is not checked with `SO_PEERCRED`.
- **Failure display:** `message.failed` with `delivered:true` after an acknowledged send is not shown ("Message sent, but could not be saved locally.").
- **Width:** emoji ZWJ sequences are measured per code point.
- **Group exits:** a group member's CLI-only exit keeps the helper online, and the note appears only after the screen is left.

## Stays out

- Group chats, invitations, contact removal and the other management commands.
- Files, calls, reactions, edits, deletes and search.
- The keyboard-and-hands visual.
