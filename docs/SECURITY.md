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

## Protect the local session

OmaQ keeps its helper socket and runtime markers private to your user account. These permissions exclude other local accounts; they do not isolate OmaQ from another process already running as you. Treat applications and scripts running under your account as trusted, and lock the session when you leave it.

A same-user process that can read the helper instance marker and connect to the private socket can submit helper operations. The compatibility operation `helper.shutdown` can stop the helper without checking private group state, interrupting active messaging, transfers, invitations, or calls until Service starts it again. OmaQ's updater and uninstaller do not use that operation. They request only `helper.shutdown_if_no_groups`, which fails closed when registered or native groups exist or group state is uncertain.

Source updates build an externally staged Git checkout before stopping the shell supervisor and watcher. Every public network Git operation uses a fresh private credential-free `HOME` plus sanitized configuration and transport state, so it cannot consume the user's `.netrc`, GitHub CLI credentials, or Git credential helpers. The updater changes the live path with one atomic no-copy exchange. Consumer failures before helper activation roll the tree back under the same shell-off boundary. Restart acceptance tolerates only the wrapper's exact readiness-timeout result, waits for one bound ready supervisor, Quickshell process, and watcher, and then rechecks the session lock; every other restart failure remains fail-closed. A rollback stop terminates each exact replacement supervisor that appears before its deadline. A final verification failure after helper activation leaves the already accepted new tree in place and returns an error because the helper process may have changed. This is a cooperative same-user boundary: do not start or restart the Omarchy shell from another process during an update.

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
