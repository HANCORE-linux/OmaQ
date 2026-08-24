# OmaQ User Guide

A short, visual guide. Follow the steps in order and use the labels shown in the screenshots.

To begin, you need a private `omaq://` invite from the other person.

## How messages travel

![How OmaQ messages travel](images/omaq-message-flow.png)

- The chat UI sends through `Service.qml` to the local `helper/omaq` process.
- Toxcore transports the packet between devices.
- Direct message content is protected by the Signal Double Ratchet.
- The identity and chat history remain local; there is no central chat server.

## 1. Join and accept

![Join and accept](images/01-join-and-accept.png)

1. Open OmaQ.
2. Click **Add**.
3. Paste the complete `omaq://` invite into **Paste omaq:// invite**.
4. Click **Join chat**.
5. The other person accepts **Someone wants to chat**.

The invite and the explicit **Accept** step are the trust decision. Confirm the person through another trusted channel.

## 2. Open a chat

![Open chat](images/02-open-chat.png)

1. Click **Chat**.
2. Click the accepted friend.
3. The floating chat window opens.

## 3. Chat controls

![Chat controls](images/03-chat-controls.png)

1. Move the pointer into the message field or focus it with the keyboard, then type.
2. Press **Send** or `Ctrl+Enter`.
3. Use the formatting buttons above the field when needed.
4. Hover over a direct message to use the compact reaction controls beside it. Applied reactions overlap the message's lower-left edge without covering its contents.
5. Hover over your own message and click the edit icon, or select it with the keyboard and press `E`.
6. The right-click menu provides **Copy**, **Reply**, and confirmed **Delete** actions.

Direct messages use the Signal Double Ratchet. OmaQ does not use plaintext fallback.

## 4. Send files

![Send files](images/04-files.png)

1. Click the file icon to expand **File transfer**.
2. Click **Choose**, or enter an **Absolute file path**.
3. Click **Send file**. Collapse the section with its arrow or use **Cancel** to stop an active outgoing transfer.
4. A completed transfer keeps its success message and local file path visible until dismissed or replaced by the next file action.
5. The recipient chooses **Accept** or **Decline**.
6. Accepted files are stored in `~/Downloads/omaq/` by default.
7. Received audio files provide an in-chat Play/Pause control. Image and video previews are not shown inside the chat.

The download directory can be changed with `OMAQ_DOWNLOAD_DIR` or `XDG_DOWNLOAD_DIR`.

## 5. Notifications

![Notifications](images/05-notifications.png)

- **Auto-open** opens a chat window when a new message arrives.
- Open **Settings** for **Mute**, **Demo**, **Theme**, and **Sounds**.
- **Sounds** offers short notification presets and previews the selected sound.
- **Mute** changes sound only and does not disable unread badges, unread state, encryption, or delivery.
- **Connecting…** or **Reconnecting…** is shown while the local helper is establishing service.

Unread messages are marked in the chat with a **New messages** divider.

## 6. Protect and move your identity

![Protect identity](images/06-protect-identity.png)

Your OmaQ identity is local. The private identity is not stored on a central chat server.

1. Open **Advanced** → **Identity**.
2. Enter a passphrase in **Passphrase for identity file**.
3. Click **Protect**.
4. Use **Export** to create a protected copy.
5. Move that file securely to the new computer.
6. Open **Advanced** → **Identity** there and use **Import**.
7. If the imported identity is encrypted, enter its passphrase first. Use **Replace** only when intentionally replacing an existing identity, then confirm **Replace now**. OmaQ validates a staged copy, creates a unique recovery backup, and restores the current identity if replacement fails.

The identity file is stored locally at `~/.local/share/omaq/tox.save`. The passphrase encrypts this file. It does not encrypt chat history.

Never send the identity file through a public channel. Do not copy private ratchet state or chat history unless you deliberately want to move that local data as well.
