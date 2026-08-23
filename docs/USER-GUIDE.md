# OmaQ User Guide

A short, visual guide. Follow the steps in order and use the labels shown in the screenshots.

To begin, you need a private `omaq://` invite from the other person.

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

1. Type in the message field.
2. Press **Send** or `Ctrl+Enter`.
3. Use the formatting buttons above the field when needed.
4. Right-click your own message for **Edit** or **Delete**.

Direct messages use the Signal Double Ratchet. OmaQ does not use plaintext fallback.

## 4. Send files

![Send files](images/04-files.png)

1. Click the file icon.
2. Click **Choose**, or enter an **Absolute file path**.
3. Click **Send file**.
4. The recipient chooses **Accept** or **Decline**.
5. Accepted files are stored in `~/Downloads/omaq/` by default.

The download directory can be changed with `OMAQ_DOWNLOAD_DIR` or `XDG_DOWNLOAD_DIR`.

## 5. Notifications

![Notifications](images/05-notifications.png)

- **Auto-open** opens a chat window when a new message arrives.
- **Mute** changes sound only.
- **Mute** does not disable unread badges, unread state, encryption, or delivery.

Unread messages are marked in the chat with a **New messages** divider.

## 6. Protect and move your identity

![Protect identity](images/06-protect-identity.png)

Your OmaQ identity is local. The private identity is not stored on a central chat server.

1. Open **More** → **Identity**.
2. Enter a passphrase in **Passphrase for identity file**.
3. Click **Protect**.
4. Use **Export** to create a protected copy.
5. Move that file securely to the new computer.
6. Open **More** → **Identity** there and use **Import**.
7. Use **Replace** only when intentionally replacing an existing identity.

The identity file is stored locally at `~/.local/share/omaq/tox.save`. The passphrase encrypts this file. It does not encrypt chat history.

Never send the identity file through a public channel. Do not copy private ratchet state or chat history unless you deliberately want to move that local data as well.
