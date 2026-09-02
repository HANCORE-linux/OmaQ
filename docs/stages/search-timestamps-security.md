# Search, timestamps, and security hardening

**Status:** merged and deployed

This note records the completed per-chat search, authoritative timestamp, PlainText, and helper-security iterations.

## Per-chat search and timestamps

- Every DirectChat and GroupChat header provides search and `Ctrl+F`. Query, pending state, timeout, and results belong to that exact chat.
- Conversation, stable Direct key, and request correlation reject delayed or cross-chat results.
- Results are bounded to 20 helper-authored history records and show sender, complete local date and time, and a two-line PlainText excerpt.
- Message models retain helper-persisted `ts` values through history reload and optimistic reconciliation.
- Current-day messages show `HH:mm`; older messages include `YYYY-MM-DD`. A confirmed row never invents a timestamp.
- One helper-captured value is used for each local history record and its matching live event.

## QML text boundary

- Ordinary QML text uses `SafeText`, whose default is `Text.PlainText`.
- Only the escaped chat header and escaped Markdown message renderer may use RichText.
- The exact-source gate canonicalizes QML through Qt's parser, checks inherited controls, rejects AutoText and dynamic or reflective writes, and exercises the real message renderer with hostile input.
- HTTP fixtures prove that escaped OmaQ output performs no external resource request.

## Helper security boundary

- Concurrent native invite callbacks use a first-writer claim, so a later request cannot replace the key or request associated with the visible invitation.
- Tox support without Signal support fails at compile time.
- Incoming text requires exact decrypted length, strict scalar UTF-8, no embedded NUL, and message-safe controls.
- Direct and group messages use stable-sender plus global budgets before replay lookup, history, unread persistence, or event fan-out.
- A bounded keyed index avoids repeated full-history replay scans while preserving exact recent and on-disk fallback decisions.
- Group-file offers are bounded per sender and globally. The accepted-ID ledger is versioned, capped at 4,096 entries, and atomically compacted.
- Outgoing file reads, avatar hashing, state-directory synchronization, and history access use descriptor-based no-follow checks.
- Native and pure-policy role conversion accepts only member, admin, and owner.

## Verification recorded for these iterations

The iterations passed `make test`, architecture checks, native phases 2, 3, 6, and 8, protocol compatibility, Signal prekey restart tests, group administration, QML PlainText adversarial fixtures, and helper security review. Offscreen fixtures did not claim native Wayland or multi-monitor acceptance.
