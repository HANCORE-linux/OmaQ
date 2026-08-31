# Understand OmaQ security

This page explains what OmaQ encrypts, what public Tox nodes can observe, and which local files need protection.

## Follow the message path

[![OmaQ encrypted message flow](images/omaq-message-flow.png)](images/omaq-message-flow.png)

Toxcore transports encrypted packets between peers. Direct text messages receive an additional Signal Double Ratchet layer. OmaQ never falls back to plaintext direct messaging when a Ratchet session is unavailable.

Private Tox New Group Chats use Tox group transport. Group attachments use authenticated, bounded OmaQ packets and remain capped at 8 MiB.

## Understand relay privacy

OmaQ keeps Tox in Transmission Control Protocol (TCP) relay privacy mode. It disables direct User Datagram Protocol (UDP) discovery, local discovery, and hole punching. Contacts therefore do not receive each other's IP addresses through direct Tox connections.

OmaQ connects to public bootstrap and relay nodes run by Tox community volunteers. These nodes help peers discover the network and forward encrypted packets. They cannot read message contents, but relay operators can observe ordinary connection metadata.

OmaQ runs no project-operated server, account service, or recovery service. The project author cannot access, intercept, recover, or delete your messages, identity, or contacts.

## Protect local data

OmaQ stores identity, contact, Ratchet, preference, and history data locally:

- `~/.local/share/omaq/`: identity, contacts, groups, avatars, history, and Ratchet state
- `~/.local/state/omaq/`: identity recovery, preferences, unread state, receipts, surfaces, and journals
- `~/Downloads/omaq/`: accepted incoming files

Private filesystem permissions protect these locations. Do not synchronize them as source files or commit them to Git.

## Know the passphrase boundary

A passphrase encrypts the Tox savedata in `tox.save`. It does not encrypt Ratchet state, avatars, receipts, preferences, or chat history.

OmaQ maintains a fingerprint-bound identity-presence record and a current recovery copy. A stale or fingerprint-mismatched copy is never restored automatically. Export a current identity bundle before moving or repairing an established identity.

Identity bundles include Tox savedata, private group mappings, and group friend-binding mappings. They exclude Ratchet sessions, local history, avatars, receipts, and received files. Exchange fresh invitations after importing an identity with existing contacts.

## Verify trusted contacts

Invitations are private, one-use entry points. Share them through a trusted channel, verify incoming requests before accepting, and revoke unused links that may have leaked.

Compare the direct contact's safety code through another trusted channel. Safety codes verify the selected contact's stable identity and are not setup secrets.

Use OmaQ only for lawful private communication with people you trust. Read the [illustrated user guide](USER-GUIDE.md#create-and-redeem-invitations) for invitation and safety-code steps.

## Review the technical contract

The [architecture and verification plan](PLAN.md) defines protocol boundaries, helper authority, fail-closed behavior, and verification gates. The [current product status](CURRENT.md) records the deployed protocol and known limitations.
