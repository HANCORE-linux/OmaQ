# Protocol 11 through 14 follow-up

**Status:** merged and deployed

This note records the completed Protocol 11 through 14 product follow-up. The live capabilities remain summarized in [`../CURRENT.md`](../CURRENT.md).

## Protocol 11: stable Direct state

- Direct window persistence stores canonical `d:<public-key>` identifiers. Ambiguous numeric records are archived before removal and are never reassigned from a reused friend number.
- Auto-open schema version 2 stores only stable Direct and Group keys. The helper archives pre-v2 sources, disables numeric Direct preferences, and atomically returns a conservative replacement state.
- Every Direct card, queued open, and queued operation retains the expected public key. A stale binding closes instead of displaying or sending as another contact.
- Contact removal remains blocked during active files or calls. The picker focus-grab regression releases panel ownership before an external picker starts.

## Protocol 12: GroupChat attachments

- GroupChat uses the Direct composer for files, images, clipboard paste, drag and drop, canonical staging, previews, acceptance, cancellation, history, playback, and path actions. Calls remain Direct-only.
- Each group member accepts independently. The helper sends at most 8 MiB through bounded ordered lossless packets to members recorded online at offer time.
- Transfer state binds the stable group, sender member key, durable random transfer ID, exact size, and SHA-256 digest.
- Pending offers use sender-fair memory slots. Durable transfer-ID reservation and history lookup start only after explicit acceptance.
- Application acknowledgement distinguishes confirmed, failed, partial, and unknown delivery. The sender writes history only while the original source still matches.
- Malformed frames, sender reuse, path errors, hash mismatches, stalls, and unsupported images fail closed.

## Protocol 13: restart and chat parity

- Valid Unicode emoji-only sequences use the same fixed 56-pixel presentation in DirectChat and GroupChat. Mixed text stays at ordinary message size.
- Clipboard images report reconnect and update failures instead of falling through to text. Stages stay bound to their IPC client until transfer adoption.
- Group projections bind helper instance, request, generation, group count, and member count. Incomplete projections retain the last good list and retry.
- Quickshell starts the helper detached and reconnects through the private socket, preserving the helper PID and active private-group state across shell reloads.
- Group typing uses bounded transient custom packets. Group receipts persist per stable member key.

## Protocol 14: windows, sounds, and message interaction

- Each chat delegate is keyed to one conversation. Opening or closing another chat cannot transfer its saved size or placement.
- Message-size steps scale message bodies and composer text without scaling the composer frame, controls, receipts, or member labels.
- Settings import bounded structurally valid PCM WAV sounds into private helper-managed copies. Removal never targets the source or bundled sounds.
- Group admins can moderate offline ordinary members. Native tests cover admin invitation, acceptance, ordinary-member removal, and denied owner removal.
- Replyable messages expose inline Reply without changing the context menu.
- DirectChat and GroupChat support exact pointer and keyboard selection with a selection-only Copy action.
