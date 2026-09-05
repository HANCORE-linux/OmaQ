# Stage and follow-up notes

This folder is the historical progress log. Numbered files record completed execution-plan phases; descriptive files record shipped work that followed the phased plan.

The live product and release snapshot is [`../CURRENT.md`](../CURRENT.md).

## Numbered phases

| Note | Scope |
|---|---|
| [`00-harness.md`](00-harness.md) | Phase 0: harness |
| [`01-chat.md`](01-chat.md) | Phase 1: direct chat |
| [`02-invite-safety.md`](02-invite-safety.md) | Phase 2: invitation safety |
| [`03-toxcore.md`](03-toxcore.md) | Phase 3 header probe |
| [`03-groups.md`](03-groups.md) | Phase 3: groups |
| [`04-surfaces.md`](04-surfaces.md) | Phase 4: surfaces |
| [`05-daily.md`](05-daily.md) | Phase 5: daily use |
| [`06-media.md`](06-media.md) | Phase 6: files and calls |
| _No note_ | Phase 7: AUR paused |
| [`08-ratchet.md`](08-ratchet.md) | Phase 8: Direct Double Ratchet |

## Follow-up work

| Note | Scope |
|---|---|
| [`product-hardening.md`](product-hardening.md) | Product, state, interface, and removal hardening |
| [`protocol-11-14.md`](protocol-11-14.md) | Stable Direct state, GroupChat attachments, restart parity, windows, sounds, Reply, and Copy |
| [`protocol-15.md`](protocol-15.md) | Confirmed local hangup, call control leases, and fail-closed teardown |
| [`search-timestamps-security.md`](search-timestamps-security.md) | Per-chat search, local timestamps, PlainText, and helper security |
| [`helper-updater-validation.md`](helper-updater-validation.md) | Manual helper updater and earlier validation rounds |
| [`trigger-free-updates.md`](trigger-free-updates.md) | External staging, shell-off source exchange, consumer checks, and rollback |
| [`safe-source-install.md`](safe-source-install.md) | External first-install build, atomic placement, controlled discovery, and enablement |

Together, these notes record what landed, how the work was verified, measured RSS where relevant, and what remained outside each iteration. No note means the planned phase did not finish. Never add keys, complete Tox identifiers, or real chat logs.
