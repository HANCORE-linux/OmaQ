# OmaQ — execution plan

**Authority:** This file is how we build. German product contract: [`../../Prompt-Uebergabe/OmaQ.md`](../../Prompt-Uebergabe/OmaQ.md). If they disagree, stop and fix both before writing code.

**Status:** Phases 0–6 and 8 are done. Phase 7 (AUR) is halted. Live plugin is **on this machine** (copy under `~/.config/omarchy/plugins/hancore.omaq`; source remains `/home/hancore/Projects/omaq`). Snapshot: [`CURRENT.md`](CURRENT.md).
**Tree:** `/home/hancore/Projects/omaq`  
**Id:** `hancore.omaq`

OmaQ is a Quattro bar plugin. Tox carries the messages. Invite by link or QR. No account. Direct chat first, groups later. Not Discord.

## Architecture law (read this in every new session)

This is **hexagonal / ports-and-adapters**, not Clean Architecture. One UI, one helper process, one file store. The process boundary is the main port. No rings, no DI, no Vtables.

**Shape**

```text
QML (views)  →  Service (exec + socket)  →  helper ops  →  adapters (store, tox_adapt)
                      ↑                         ↑
                   settings                 roles, invite, conversation, message
                                              (pure where a rule can be wrong)
```

**Rules (break one = stop and fix the plan first)**

1. New capability = new module + new `op`/`event` + gold test, **then** code. Do not fatten an existing module.
2. Extend **sideways** (new helper module) or **up** (new view on the same snapshot). Never put domain rules in QML. Never let the helper import QML.
3. Address chat only as a **conversation** (`direct` | `group`). No second chat stack for groups.
4. **One helper process** per user session. Service always execs. Helper `flock`s. Exit `2` = already running → Service connects. No flock in QML.
5. **Helper is authoritative.** `Model.js` is UX precheck only. It never decides redeem, roles, or history.
6. Pure policy has no IO: `roles.c`, `invite.c`, `conversation.c`. IO lives in `store.c` (history) and `surface.c` (`surfaces.jsonl`), while `tox_adapt.c` owns (`tox.h` + `toxav.h` + `toxencryptsave.h` only). Signal/Olm headers live only in `ratchet_adapt.c`.
7. `message.c` talks `store.h`, not paths. `group.c` talks `roles.c` + `tox_adapt`, not a second policy.
8. Every rule that can be wrong must be testable **without network and without toxcore**. `make arch` in `verify-0` enforces the include/IO greps.
9. Untrusted input (`json_io`, invite parse) builds under `-fsanitize=address,undefined -Wall -Werror`.
10. Do not invent RSS numbers. Do not invent crypto. Do not bind Omarchy window keys. Do not write the live plugin dir except one announced install.

A later session does not re-decide this. Change a rule only by editing this section.

### Review disposition

External review of PLAN + `OmaQ.md` vs the live Quattro shell. Verdicts:

| # | Claim | Use? |
|---|---|---|
| 1 | Per-monitor Panel ⇒ two helpers / two `tox.save` | **Use.** Singleton lock+socket in phase 1. `verify-1-tox` asserts one process. |
| 2 | History undefined; `tox.save` is not messages | **Use.** JSONL store §5. |
| 3 | `verify-1` mixes three phases; `plugin validate` is offline | **Use.** Split offline / tox. Validate on `verify-1-offline`. |
| 4 | `/usr/share/omaq/` is not scanned | **Use.** Package drops files; user enables. `verify-7` checks enable. |
| 5 | QR still contains the durable Tox id | **Use the fact. Discard the fork.** Token stays; Tox needs the address. Clarify contract. Do not hide the id or invent a directory. Nospam voids all invites. |
| 6 | Invite grammar incomplete for gold files | **Use.** Full grammar §4. |
| 3a | NGC has `observer` | **Fact only. Discard as product.** Three roles stay. Observer unused in 0.x. |
| 3b | No dissolve primitive | **Use as risk.** Dissolve = kick all + leave + mark dissolved. Confirm in `03-toxcore.md`. |
| 3c | Private group needs an existing friend | **Use as constraint.** Group invite after 1:1. No public DHT directory. Confirm in `03-toxcore.md`. |
| 3d | `group.c` must not duplicate toxcore | **Use.** Headers first, implement only the gap. |
| 3e | Group invite roles | **Member-only.** Promote a joined member with a separate stable-key `setRole` operation. |
| — | Manifest `version`; SPDX `GPL-3.0-or-later`; verify-0 is a C driver; do not invent RSS | **Use.** |
| — | Helper death; rate limit; import must not clobber | **Use.** |
| — | NGC API details as facts | **Discard until headers.** Marked suspicion only. |

Second review (hexagonal / seams):

| # | Claim | Use? |
|---|---|---|
| — | This is hexagonal, not full Clean Architecture — and that is right | **Use.** No rings, no Vtables. |
| 1 | Split `roles.c` (pure) from `group.c` | **Use.** Gold in `verify-1-offline`. |
| 2 | flock belongs in the helper; Service only exec + connect | **Use.** Verified: Quickshell.Io has Process, Socket, FileView — no flock. |
| 3 | `make arch` grep as a mechanical Dependency Rule | **Use.** In `verify-0`. |
| 4 | `message` vs `store` — `store.h` seam | **Use.** |
| 5 | `surfaces.json` has no owner | **Use helper ownership.** `surface.set`/`get`. Discard QML last-writer-wins. |
| 6 | sanitizers + mutation corpus on `json_io` / invite | **Use.** Discard a full fuzzing product in phase 0. |
| 7 | Model.js must not decide redeem; `toxav.h` lives in `tox_adapt` | **Use.** |
| — | Four-ring Clean Architecture / DI | **Discard.** |

Owner addendum (binding): cards move to any monitor and stay; pin = Hyprland toplevel + stock Omarchy keys (`SUPER+T`, `SUPER+SHIFT+arrows`); unpin = card.

---

## 1. Decisions (closed)

| | |
|---|---|
| Transport | Tox (`toxcore`). Not SimpleX. Not LAN-only. |
| Helper | **Exactly one** C11 process per user session. Not one per Panel, not one per monitor. |
| Singleton | **Helper** takes `flock` on `$OMAQ_HOME/omaq.lock`. Service always `exec`s the helper. Lock owner binds `$OMAQ_STATE/omaq.sock`. Second starter exits `2` (`already_running`); Service then connects. No flock in QML (Quickshell.Io has Process, Socket, FileView — no lock). |
| License | QML/plugin **MIT**. Helper **GPL-3.0-or-later** (matches toxcore). |
| Invite | Full grammar in §4. One use, 24 h, revocable, never auto-accept. |
| QR | Always tokenised `omaq://invite/…`. The Tox address is inside because Tox cannot add a friend without it. The token is what dies on revoke. **Nospam rotation voids every open invite at once.** |
| Model | `conversation` is `direct` or `group`. One chat page for both. |
| History | Append-only JSONL per conversation, `0600`, paged from disk. See §5. |
| Groups | `owner > admin > member`. NGC `observer` unused in 0.x. Phase 3 starts by reading toxcore headers, then implements only the gap. |
| Surfaces | **Card:** Quickshell overlay, drag to any monitor, stay there. **Pinned:** real Hyprland `xdg-toplevel`, terminal look. Then stock Omarchy keys apply (`SUPER+T` tile↔float, `SUPER+SHIFT+arrows` swap). Unpin (`lösen`) returns to the card. OmaQ does not bind those keys. |
| Theme | Default System (`colors.toml`). Palettes: Paper, Ink, Moss, Dusk, Ember. Terminal-pin style uses the same palette, monospace. |
| Sound | Compact Settings picker: off, ICQ, four branded-style presets backed by licensed open audio, five pleasant Ocean presets, and legacy/custom files. |
| Memory | **50 MB RSS** for everything OmaQ starts (one helper + QML in a session). Do not invent a number in prose — measure. `verify-1-tox` records idle RSS; phase 6 records call-peak; phase 8 records ratchet idle + one-text. Fail if a single helper > 51200 kB. |
| Payload | **Double Ratchet** (Signal spec) on **direct** conversations. Tox is the pipe. Not SimpleX. Not a second handshake we write ourselves. Library: Arch extra `libsignal-protocol-c` (existing). Groups stay Tox-native until a later go. |
| Ratchet bootstrap | Direct invite requires `rk=` (32-byte identity key, 64 hex). The redeeming peer returns its own ratchet identity in the token-authenticated friend request; both expected pins and Signal identity keys persist before a bundle is accepted. Safety code still binds the Tox ids. |
| Out of 50 MB | No Tor child. No second Tox instance per contact. No SimpleX stack. |
| Bootstrap | Public Tox nodes, compiled in. |
| Live | Never write `~/.config/omarchy/plugins/` except one announced install. Verify uses temp dirs only. |
| Package | AUR in **phase 7**. **AUR account registration is temporarily off** — do not upload, do not create an AUR account, do not start phase 7 until a new **go**. Package files live under `/usr/share/omaq/`; the shell does **not** scan that path. Activation is an explicit user command (see §11). |

Incoming friend requests without a live token are rejected. Rate limit: 5 / minute / key, 20 / hour global; extras dropped. A leaked QR can knock, not flood.

---

## 2. Decomposition

One reason per file. Tests per module. Invite and message are reused by 1:1 and groups.

```text
Panel.qml  (one instance per monitor — host fact)
        → Service.qml          # always exec helper; on exit 2, connect to socket
        → $OMAQ_STATE/omaq.sock
        → helper/omaq          # one process; flock lives here
              json_io.c        # closed scanner; sanitize-tested
              invite.c         # pure parse/issue/revoke (no IO)
              roles.c          # pure role_may(); no tox, no IO
              conversation.c   # id, kind, unread, last (no IO)
              message.c        # send/history use cases via store.h
              store.c          # only module that opens history files
              identity.c
              roster.c
              group.c          # phase 3: orchestrates roles + tox_adapt
              file.c av.c      # phase 6
              ratchet.c        # phase 8: session use cases
              ratchet_adapt.c  # only Signal protocol headers
              tox_adapt.c      # only tox.h, toxav.h, toxencryptsave.h
        → toxcore
        → libsignal-protocol-c
Model.js                       # UX precheck only; helper is authoritative
ChatSurface.qml                # unpinned overlay + pinned terminal window
```

New capability = new module + new `op`/`event` **and** a gold test, then code.

**Mechanical (not prose):** `make arch` in `verify-0` (~grep):

- only `tox_adapt.c` may include `<tox/tox.h>`, `<tox/toxav.h>`, or `<tox/toxencryptsave.h>`
- only `ratchet_adapt.c` may include Signal protocol headers (`<signal/…>`)
- only `store.c` may open `$OMAQ_HOME/history`
- `roles.c`, `invite.c`, `conversation.c` contain no `open(`, `fopen`, `socket`, `tox_`
- `Model.js` contains no `Qt`, `Quickshell`, `XMLHttpRequest`
- no QML file contains `tox`

Do not put domain rules in QML. Do not invert the arrow. `make arch` fails the build if that erodes.

This is hexagonal / ports-and-adapters. Not four-ring Clean Architecture: no DI container, no Vtables, one UI, one file store. The process boundary is the main port.

A new **view** (pin, other monitor) is an upward extension of `ChatSurface` only.

---

## 3. Process: one helper

The bar is built **per monitor**. `plugins/bar/Bar.qml` (line 468): a widget in the layout is live once per screen. After a crash relaunch, two bar instances can exist briefly.

`Service {}` stays a child of `Panel`. It is dumb: always `exec` the helper, read the exit code, connect if needed.

```text
Service:
  exec helper/omaq
  if helper stays up: talk stdin/stdout (or the socket the owner bound)
  if helper exits 2 (already_running): connect $OMAQ_STATE/omaq.sock
  if helper exits otherwise / socket dies: emit helper_down; backoff 200ms → 1s → 5s, cap 30s; exec again

Helper (C):
  open $OMAQ_HOME/omaq.lock (0600)
  flock(LOCK_EX | LOCK_NB)
  if acquired: unlink stale sock, bind, write pid, serve
  else: exit 2
```

An in-flight `msg.send` has unknown delivery status if the helper dies before reporting its correlated outcome. The UI must not offer automatic Resend because transport may already have succeeded. Restart reloads `tox.save`, history JSONL, persisted ratchet pins/identities/sessions; no second identity.

**Lock contention is `verify-1-offline`:** two helper processes, one temp home, no network, no toxcore. Exactly one stays up, the other exits 2. `verify-1-tox` still asserts one process when Tox is on.

stdin/stdout stay the protocol on the socket (one JSON object per line). stderr: diagnostics, never keys or full tox ids.

```text
$OMAQ_HOME          default ~/.local/share/omaq   0700
$OMAQ_STATE         default ~/.local/state/omaq   0700
$OMAQ_HOME/tox.save
$OMAQ_HOME/omaq.lock
$OMAQ_HOME/history/<storage-id>/messages.jsonl
$OMAQ_STATE/omaq.sock
$OMAQ_STATE/surfaces.jsonl     # monitor, x, y, pinned, per conversation
```

Verify sets `OMAQ_HOME` / `OMAQ_STATE` to temp dirs and **fails** if they resolve to the real home paths.

Dev helper: `./helper/omaq`. Packaged: `/usr/lib/omaq/omaq`.

---

## 4. Invite grammar (complete)

```text
omaq://invite/<tox-addr>?i=<invite-id>&e=<unix-expiry>&k=<kind>[&g=<group-id>][&r=<role>][&rk=<64-hex>]
```

| Part | Rule |
|---|---|
| scheme | `omaq` only, lowercase |
| `<tox-addr>` | 76 hex chars (32-byte pk + 4-byte nospam + 2-byte checksum), case-insensitive |
| `i` | required, 1–64 `[A-Za-z0-9_-]` |
| `e` | required, decimal unix seconds |
| `k` | required, `direct` or `group` |
| `g` | required iff `k=group`, forbidden iff `k=direct` |
| `r` | iff `k=group`: `member` only. Default `member` if omitted. Forbidden on `direct` |
| `rk` | optional, 64 hex: Signal identity public key for the Double Ratchet. Direct only. Forbidden on `group`. Phase 8. |
| query order | irrelevant |
| duplicate key | invalid |
| unknown key or unknown `k`/`r` | invalid (not “half accept”) |
| `k=group` in phase 1–2 | parse ok, redeem → `unsupported` |

This is the **only** invite string. Gold files in `tests/gold/invite/` cover: happy direct, happy group, missing `i`/`e`/`k`, `k=direct` with `g`, `k=group` without `g`, unknown `k`, duplicate `i`, mixed case scheme, param order swap.

**Helper is authoritative.** `Model.js` may prettify or reject obviously broken paste in the UI. It must never decide redeem. If Model.js and `invite.c` disagree, the helper wins and the gold file is updated from C.

`json_io.c` and `invite.c` are the only modules that parse untrusted input. `verify-0` / `verify-1-offline` build those tests with `-fsanitize=address,undefined -Wall -Werror`. Add a small mutation corpus (flipped bytes, long strings, bad escapes) next to the gold files. No extra JSON library.

The QR is **not** a bare Tox id. The address is still in the URL because `tox_friend_add` needs it. Revoke deletes `i`. The address remains knowable to anyone who saw the URL; they can knock, we reject without a live token. **`nospam.rotate` changes `<tox-addr>` and invalidates every outstanding invite.** Document that in the Profile UI.

Group links are member-only. Tox cannot safely bind an elevated role in the invite packet, so owners promote a joined member separately by stable member key.

---

## 5. Message store

`tox.save` is keys and friends. It is **not** history.

```text
$OMAQ_HOME/history/<storage-id>/messages.jsonl
```

Direct storage IDs are `d:<64-hex-tox-public-key>`; group storage IDs are the stable `g:<64-hex-chat-id>`. Numeric Tox friend numbers remain transport handles in IPC events and are never the durable namespace for direct history, avatars, Ratchet pins, Signal identities, Signal sessions, unread state, or receipt debt. A versioned `direct-friends.tsv` binds legacy handles to the Tox public key that owned them. Startup migrates numeric state only with that durable proof, never overwrites an existing stable namespace, and quarantines collisions under a `legacy-direct` suffix. Numeric state without a pre-existing binding is archived as ambiguous and requires a fresh contact invite rather than being assigned to the current holder of a reused number.

- One JSON object per line, append-only.
- Fields: `id`, `ts`, `from` (short id, not full key in logs), `text`, `dir` (`in`|`out`).
- Directory and file `0700` / `0600`.
- `history` with `limit:50` = last 50 lines from the tail. Do not load the file into QML.
- Rotate when the file exceeds 2 MiB: rename to `messages.jsonl.1`, start a new file. Keep one rotated file. Older lines drop.
- **`message.c` talks only `store.h`.** `store.c` is the only file that opens history paths. A later at-rest wrap (libsodium already linked via toxcore) is a new `store` file, not a rewrite of `message.c`. Phase 1: 0600 only, no extra cipher.
- Phase 5 search: scan jsonl on disk for the **open** conversation. Still not the whole archive in QML.
- Tests use a fixture file under the temp `OMAQ_HOME`. Never the user’s real history.

---

## 6. Wire contract

Service → helper (unknown or not-yet-built `op` → `unsupported`):

```text
{"op":"status","id":"optional-fresh-handshake-nonce"}
{"op":"invite.create","ttlSec":86400,"kind":"direct"}
{"op":"invite.create","ttlSec":86400,"kind":"group","group":"...","role":"member"}
{"op":"invite.create","ttlSec":86400,"kind":"group","group":"...","role":"member","id":"friend-number","key":"64-hex-friend-key","request":"gi-client-request"}
{"op":"invite.revoke","id":"..."}
{"op":"invite.redeem","payload":"omaq://invite/..."}
{"op":"contact.decide","id":"...","accept":true}
{"op":"contact.remove","id":"friend-number","key":"64-hex-friend-key"}
{"op":"group.create","title":"..."}
{"op":"group.dissolve","group":"..."}
{"op":"group.member.setRole","group":"...","member":"...","role":"admin"}
{"op":"group.member.setRole","group":"...","member":"...","role":"member"}
{"op":"group.member.remove","group":"...","member":"..."}
{"op":"group.leave","group":"..."}
{"op":"msg.send","conversation":"...","text":"...","id":"client-request-id"}
{"op":"message.react","conversation":"...","id":"...","text":"❤️"}
{"op":"history","conversation":"...","limit":50}
{"op":"nospam.rotate"}
{"op":"search","conversation":"...","text":"...","limit":20}
{"op":"identity.export","path":"...","id":"client-request-id"}
{"op":"identity.inspect","path":"...","id":"client-request-id"}
{"op":"identity.import","path":"..."}
{"op":"identity.import","path":"...","replace":true}
{"op":"identity.protect","passphrase":"..."}
{"op":"identity.unlock","passphrase":"..."}
{"op":"identity.unprotect","passphrase":"..."}
{"op":"surface.set","conversation":"...","monitor":"...","x":0,"y":0,"pinned":false}
{"op":"surface.get","conversation":"..."}
{"op":"attachment.inspect","path":"...","id":"client-request-id"}
{"op":"attachment.stage.create","id":"client-request-id"}
{"op":"attachment.stage.commit","path":"helper-created-staging-path","id":"client-request-id"}
{"op":"attachment.stage.discard","path":"helper-created-staging-or-pending-path","id":"client-request-id"}
{"op":"file.send","conversation":"...","path":"...","kind":"file|image","id":"client-request-id"}
{"op":"file.accept","id":"...","path":"..."}
{"op":"file.cancel","id":"..."}
{"op":"file.status","conversation":"...","id":"client-request-id"}
{"op":"call.start","conversation":"..."}
{"op":"call.answer","conversation":"..."}
{"op":"call.stop","conversation":"..."}
```

Helper → service: `snapshot`, `request`, `message`, `message.failed`, `message.reaction`, `receipt`, `receipt.sent`, `receipt.failed`, `connection`, `group.changed`, `attachment.stage`, `attachment.inspected`, `attachment.rejected`, `attachment.discarded`, `file.offer`, `file.sending`, `file.done`, `file.canceled`, `file.failed`, `call.incoming`, `call.state`, `helper.probe`, `helper.shutdown`, `helper_down`, `error` (`invite_expired` | `unsupported` | `forbidden` | `invalid_image` | `identity_exists` | `identity_backup_failed` | `identity_passphrase_required` | `identity_import_failed` | `identity_state_archive_failed` | `identity_rollback_failed` | `rate_limited` | `locked` | `request_required` | `no_ratchet` | `ratchet_pending` | `history_failed` | `busy`).

Invite creation and revocation expose the helper-issued absolute expiry and echo a UI request nonce. The panel derives only the live countdown presentation; confirmed **New link** waits for the old invite's authoritative revocation event before creating its replacement. Protocol 3 introduced the requirement that every `msg.send` must carry a non-empty client request `id`; missing IDs fail with `request_required` before transport. `file.*` and `call.*` are 1:1 only (`conversation` is a friend number). Group ids (`g…`) return `forbidden`. Incoming files stay paused until `file.accept`. Dest default: `~/Downloads/omaq/<name>`, `0600`, cap 8 MiB. An explicit destination override remains supported but never follows symlinks. Peer avatars use exclusive staging, full PNG/JPEG/WebP decode with bounded dimensions and decoded bytes, canonical PNG rewriting, and atomic installation before any avatar event. The optional client request `id` on `file.send` is echoed as `request` so multiple UI clients correlate `file.sending`; that event exposes the validated transfer id required for outgoing cancellation. Protocol 3 adds request-correlated message outcomes: `msg.send.id` is echoed as `message.request`, while pre-delivery failures use `message.failed` with `delivered:false`. A post-transport history failure emits the successful `message` event followed by `message.failed` with `delivered:true`; this means transport was committed, not that a peer receipt arrived, and it must never offer Resend. If the helper disconnects after accepting a send but before reporting an outcome, the UI marks that request `delivery_unknown` and does not offer automatic Resend because transport may already have succeeded. Status handshakes require protocol 4, include a per-process `instance` id, and echo a fresh status-request nonce so stale replay records or older attached helpers cannot release queued operations. The helper persists per-conversation unread counts and broadcasts `unread` updates so every bar instance shows and clears the same badge state. After the authoritative friend and group registries load, unavailable-conversation unread entries are pruned transactionally so an orphan cannot leave a permanent, non-actionable widget badge. After reconnecting to the same instance, `file.status` replays the cached state for the originating request. `receipt.sent` and `receipt.failed` correlate local read-receipt attempts by message id so transient failures can be retried without affecting message-send state. A local or remote normal-file cancellation ends with direction-tagged `file.canceled` on both peers, while transport errors use `file.failed`. File events include `dir:"in"|"out"`. Completed incoming files are persisted as history messages with `kind:"file"`; persistence failure emits `history_failed` after the completed path event. Protocol 4 uses the stable 32-byte Tox group chat ID for group conversation identity and persistent state; process-local Tox group numbers never cross the helper boundary. The private `groups.tsv` registry uses identity-bound proofs before reconnecting a missing group, reconciles metadata with groups present in Tox saved state, participates in identity replacement and versioned identity export bundles, and must not be synchronized separately. An orphaned private-group ID is visibly pruned because a private group cannot be rejoined from its chat ID alone. Legacy `g<number>` history remains archived in place, while obsolete unread/surface entries are ignored and reported as `legacy_group_state_archived` because mapping a reused process-local number would be unsafe. Protocol 5 adds generation-tagged `friend.list.begin`, `friend.info`, and `friend.list.end` projections plus correlated `group.invite.sent`/`group.invite.failed` outcomes. Protocol 6 binds destructive contact removal and targeted group invites to the projected stable friend public key and requires the helper to re-read that key at the transport boundary. Every targeted group invite also carries a client request nonce echoed by its correlated `group.invite.sent` or `group.invite.failed` outcome; the UI keeps the successful result visible as sent and waiting for acceptance. The helper keeps a bounded in-memory terminal-result ring and replays it on status so a same-instance socket reconnect cannot strand the UI; clients reject older helpers before releasing queued operations. Protocol 7 makes `conversation.read` helper-authoritative: a crash-recoverable journal couples unread clearing to a bounded persistent receipt outbox, receipt-capable peers silently advertise support through an old-helper-safe control frame, and authenticated application acknowledgements retire entries only after the message author processed them. Direct legacy peers receive a bounded terminal fallback, retry work is batched, and reconnect history carries an authoritative unread snapshot so lost IPC events cannot leave a stale `Delivered` indicator. Protocol 8 gates request-correlated Identity inspection/actions and stable direct-friend projections for group members, so the updated UI never releases these operations to a protocol-7 helper. Named private groups are helper-authoritative and capped at 10 members through both local invite policy and Tox NGC's peer limit. Inviting an existing contact sends the group token only through the established Signal-encrypted direct session (bootstrapping that Ratchet session first when necessary), then delivers the native private Tox invite for explicit acceptance; the control payload is never rendered as a direct chat message. A self-disconnected or kicked recipient removes the stale local Tox group before registry cleanup, allowing a later fresh targeted invite to be displayed and accepted. Because a Tox NGC peer key is group-specific and differs from the direct friend key, the recipient first confirms its claimed group key over the authenticated direct Ratchet session and only then proves possession of the encrypted invite secret from that exact group-message sender. The reserved `OQX1` control envelope remains invisible to older helpers. The inviter persists at most nine friend/member mappings per group in the `OMAQGF1`-versioned `group-friends.tsv` sidecar, journals both handshake sides across helper restarts, and acknowledges the result over the direct Ratchet session. The recipient fsyncs a pre-accept transaction before native acceptance; an interrupted transaction removes the accepted group during recovery, while a committed proof debt remains durable until acknowledged rather than expiring offline. If an inviter loses an unestablished expectation, a fresh authenticated direct member descriptor can recreate it only for a member key currently present in that exact group; the subsequent group-key possession proof remains mandatory. Identity export fails with correlated `busy` while any live invite or binding debt remains, so a bundle never silently drops that debt. Group removal journals the paired `groups.tsv`/`group-friends.tsv` update and completes an interrupted pair before startup reconciliation. Unknown mappings fail closed unless they belong to the exact registry entries pruned during the same startup reconciliation. The legacy three-column `groups.tsv` format remains unchanged; version-2 Identity bundles include both private registries while version-1 bundles remain importable. Generation-tagged `group.list.begin`, `group.info`, `group.member`, and `group.list.end` events atomically project names, group-specific member keys, optional bound direct friend keys, roles, and online state to every UI client. Group message actions, allowlisted reactions, and receipts use Tox-native group messages; group file transfer remains unavailable because Tox NGC has no group file primitive. Calls are direct-chat-only, audio-only (48 kbit, video 0), and use bounded 48 kHz mono PCM rings with a single interruptible `libpulse` event-loop thread; ToxAV send/iterate remains on the helper thread, while hangup wakes and joins the audio loop without canceling a blocked PulseAudio call. One process-wide QML singleton loops the bundled `phone.oga` progress tone only while at least one surface requests `incoming` or `ringing`, then stops it for `active`, declined, ended, or otherwise terminal call state; multiple monitor surfaces never layer duplicate playback. Tox runs in TCP-relay privacy mode with UDP, local discovery, and hole punching disabled so contacts do not receive each other's IP addresses. Hangup and incoming-call decline use `TOXAV_CALL_CONTROL_CANCEL`. Active calls emit `call.state:active`; status snapshots reconcile the authoritative call state after reconnect, and the UI owns only the elapsed-time presentation. Protocol 9 adds helper-authoritative PNG/JPEG/WebP inspection and `kind:"image"` history. Inline images remain ordinary direct-chat file transfers, are capped by the 8 MiB file limit, render only as 56×56 local previews, and open through the stored local path; groups still reject every file operation. Incoming images are fully decoded and canonically rewritten before QML receives an image-kind event. Selected and clipboard images first enter an exclusive 0600 helper-created path under the private attachment directory, are bounded while copied, then decoded and atomically adopted as canonical PNG. Pending sidecars let startup remove abandoned staging without deleting an attachment already accepted for transport. Discard requests remain request-correlated cleanup debts until the helper acknowledges `attachment.discarded`, so a same-instance IPC reconnect cannot orphan a canceled stage. Protocol 9 also provides an instance-bound helper probe and acknowledged shutdown. Uninstall binds the protocol marker to PID, UID, process start time, executable, instance, and socket, prevents respawn during removal, and signals only the still-verified process.

`identity.inspect` validates an exact staged bundle and its optional imported passphrase without mutating the active identity. `identity.import` without `replace` **refuses** if `tox.save` already exists (`identity_exists`). `replace:true` needs an explicit UI confirmation bound to the selected path. Identity outcomes echo a client request ID, and passphrase-bearing operations are sent only to a ready helper rather than entering the QML retry queue. **Validate bundle** validates without activation; **Import identity** is the separately confirmed destructive boundary and leaves a persistent fresh-invite warning for restored Tox contacts because Ratchet state is deliberately excluded from rollback-sensitive identity bundles. The helper validates an exact staged copy, requires the imported passphrase when encrypted, creates a unique non-overwriting recovery backup, archives the previous identity's history, avatars, files, unread/surface/Auto-open state, receipt outbox and recovery transaction, direct-key bindings and journals, and rotates Ratchet state. Confirmed contact removal clears that peer's pinned Ratchet identity, sessions, bootstrap record, and outstanding prekey reservation while preserving history, so both users can remove and re-invite after **Import identity**. A per-client instance gate rejects operations queued under the old identity. Invalid replacement data restores the previous identity. Never default to replace. Contact removal carries both the current numeric transport id and the selected contact's stable public key; the helper re-reads and compares that key immediately before deletion so a reused friend number cannot target a different identity.

`identity.protect` encrypts `tox.save` with toxcore `toxencryptsave` (passphrase, RAM only). New passphrases require at least 8 Unicode characters and at most 128 UTF-8 bytes; legacy unlock and unprotect remain compatible with older non-empty passphrases. Default remains plaintext. Encrypted save on helper start emits `locked`; other ops return `locked` until `identity.unlock`. Wrong passphrase stays locked. This is at-rest for the identity file only — not a second chat protocol, not SimpleX.

Phase 1 implements direct invite, decide, send, history, status, revoke. Group ops and export/import return `unsupported` until their phase.

---

## 7. Surfaces (phase 4)

Per conversation, user’s choice:

| Mode | What it is | Keys |
|---|---|---|
| **Card** (default) | Quickshell overlay. Drag to any connected monitor; it stays. Small panel, not an app. | Overlay only. Hyprland window binds do **not** apply (no toplevel). |
| **Pinned** | Real Hyprland `xdg-toplevel`, looks like a terminal (monospace, no fake chrome). Same `ChatPage`. | Stock Omarchy window binds, because it *is* a window. |
| **Lösen** | Destroy the toplevel, restore the card at last card position/monitor. | OmaQ control (button), not a Hyprland bind. |

Pinned window uses the **existing** Omarchy tiling map (`default/hypr/bindings/tiling.lua` on this box):

| Keys | What Omarchy already does |
|---|---|
| `SUPER+T` | Toggle this window tiling ↔ floating |
| `SUPER+SHIFT+arrows` | Swap this window in that direction |
| `SUPER+arrows` | Focus neighbor |
| `SUPER+SHIFT+ALT+arrows` | Move the whole workspace to that monitor |

OmaQ **must not** bind `SUPER+T` or `SUPER+SHIFT+arrows`. No new Hyprland config. If those binds change upstream, the pinned chat follows.

`SUPER+T` is not “lösen”. Tile↔float stays a Hyprland window. Lösen is unpin to card.

**The helper owns `$OMAQ_STATE/surfaces.jsonl`.** N Panels must not write it. `surface.set` / `surface.get` are the only writers/readers. That is the same singleton that owns conversation ids. Do not use last-writer-wins from QML.

Remember: monitor, x, y, `pinned` true/false.

Overlay: one Quickshell layer per monitor that has a card. Not N exclusive-focus shells.

---

## 8. Groups (phase 3)

Roles stay three on purpose (small, Matrix-like). NGC’s fourth role `observer` is **not** exposed in 0.x.

**Phase 3 step 0 (mandatory, before code):** install toxcore (already approved by then), read the headers, write `docs/stages/03-toxcore.md`: what NGC guarantees for invite, kick, roles, dissolve, public vs private. Implement **only the gap** in `group.c`. Do not double-implement rules toxcore already enforces.

Confirmed in `docs/stages/03-toxcore.md` (headers, 0.2.22-2):

- **Dissolve** is not a Tox primitive. OmaQ dissolve = kick everyone `role_may` allows + leave + mark dissolved. The NGC group may linger; we do not promise it is gone.
- **Private group join** needs `tox_group_invite_friend` and an **existing Tox friend**. Group invite is: already-accepted 1:1, or redeem does the 1:1 token dance then the group invite. No public DHT directory. No `tox_group_join` (that is the public Chat-ID path).
- **Invite roles:** group links accept only `member`; owner/admin changes are separate, confirmed operations keyed by the member's stable public key.
- **Gap vs product:** Tox lets only the founder promote to moderator. OmaQ `roles.c` still allows admin → admin; `tox_group_set_role` then returns `forbidden`. We do not invent a side channel.
- **No peer-list API.** `group.c` tracks join/exit. Observer is never set.

---

## 9. Quattro host

```json
{
  "schemaVersion": 1,
  "id": "hancore.omaq",
  "name": "OmaQ",
  "version": "0.1.0",
  "kinds": ["bar-widget"],
  "entryPoints": { "barWidget": "Panel.qml" },
  "barWidget": {
    "displayName": "OmaQ",
    "defaultSection": "right",
    "allowMultiple": false
  }
}
```

`version` is required (`omarchy plugin validate`). `import qs.Ui` / `qs.Commons`. No Shibumi. No `/usr/share/omarchy/` edits.

IPC: `open`, `close`, `toggle`, `invite`, `status`.

QR tools: system `qrencode` / `zbarimg`.

Settings (phase 4): `notifyBadge`, `notifyRightPanel`, `notifyDesktop`, `surfaceMode` (`separate`|`bundled`), `sound`, `soundCustomPath`, `chatTheme`, `messageScale` (`0.9`, `1.0`, `1.1`, `1.2`, or `1.4`), `animateUnread`. The compact panel uses a fixed self/request header, a fixed support-link frame, a Friends/Groups frame, and a fixed action rail. The two compact upper frames share the former header height; the transparent GitHub and monochrome Ko-fi support glyphs remain borderless, use `color03` on hover/focus, and match the cell size, glyph scale, and column spacing of the rail below. Friends begin at the top of the lower-left frame and fill five vertical entries per left/center/right column before continuing in a vertically scrollable area with a fading scrollbar; the heading shows online/total counts, unread names are underlined, and hover/focus uses `color03`. Every rail selection and confirmation remains enclosed by the thin lower-left frame. Rail menus extend the panel only to their natural height while the header and right frames stay stationary; the lower-left area scrolls only when constrained by screen height. Group-name and Search inputs match the full-width Identity path field. Selecting a listed group opens its chat directly. The group-chat header can invite an existing contact or let any role leave after confirmation; member and invite entries remain unframed. Complete group sender names determine the minimum bubble width, and helper-persisted system rows identify member joins and leaves. Message scaling affects message bodies only and provides a live preview.

---

## 10. Tree

```text
omaq/
  Makefile
  LICENSE.MIT
  LICENSE.GPL-3
  THIRD_PARTY.md
  manifest.json
  Panel.qml Service.qml Model.js ChatSurface.qml
  pages/
  helper/
  tests/gold/invite/ tests/gold/json/ tests/gold/store/
  tests/run.sh
  tests/two-clients.sh      # two socket clients, assert one helper
  packaging/PKGBUILD
  docs/PLAN.md
  docs/stages/
```

---

## 11. Package and how the shell finds it

`PluginRegistry` scans **only** `~/.config/omarchy/plugins/` and the first-party tree. It will **not** see `/usr/share/omaq/plugin/`.

Phase 7 therefore:

- Package **ships** helper → `/usr/lib/omaq/omaq` and a plugin tree → `/usr/share/omaq/plugin/`.
- Package **does not** write `~/.config` in `post_install`.
- Activation is one documented command the user runs:

```text
omarchy plugin add /usr/share/omaq/plugin
```

If `plugin add` only accepts git URLs, the command is a symlink:

```text
ln -s /usr/share/omaq/plugin ~/.config/omarchy/plugins/hancore.omaq
```

`verify-7` must perform that enable into a **temp** `HOME`/`XDG_CONFIG_HOME` and run `omarchy plugin validate` on the result. If we cannot automate `plugin add`, verify the symlink path.

`omarchy plugin add <git-url>` remains valid for people who do not use AUR.

No `install=` daemon. No `Restart=always`.

`depends`: `toxcore`, `libsignal-protocol-c`, `libpulse`, `ttf-material-symbols-variable`, `qrencode`, `zbar`.
`license`: `MIT` and `GPL-3.0-or-later`.  
`source=`: tagged tarball + real `sha256sums`, never `SKIP` on a release.

---

## 12. The only gate

```text
make verify          # current PHASE
make verify-N
```

| Target | Must prove | Network | toxcore |
|---|---|---|---|
| **verify-0** | C test driver **compiles and runs** (not a shell script). `make arch` greps pass. Sanitizer flags on the driver. PKGBUILD stub parses. | no | no |
| **verify-1-offline** | Invite + JSON + **roles** gold (`role_may`). Store via `store.h`. Two helpers, one lock, one survivor exit 2. Mutation corpus. `omarchy plugin validate $PWD`. | no | no |
| **verify-1-tox** | Two socket clients, **one** helper process, one invite + one text. Idle RSS **recorded**. Lock/socket: second starter does not create a second tox.save writer. | yes (bootstrap) | yes, after owner approved the package |
| **verify-1** | offline + (tox if approved, else skip tox with a loud “tox not enabled”) | | |
| **verify-2** | expire, revoke, nospam voids all invites, safety-code match, rate-limit | temp only | yes |
| **verify-3** | after `03-toxcore.md` exists: role matrix, dissolve = our definition, no second helper | yes | yes |
| **verify-4** | validate + schema; surfaces.jsonl read/write; still one helper | no extra | yes |
| **verify-5** | import refuses without `replace`; `replace` on temp home; search hits one fixture line | no | yes |
| **verify-6** | file on disk; call start/stop; peak RSS recorded | yes | yes |
| **verify-7** | `makepkg -f`; `namcap`; tar has no `home/`, no `tox.save`; enable path (symlink or plugin add) validates | no | build dep |
| **verify-8** | two homes: invite with `rk`, one ratchet text; ciphertext is not plaintext on the wire; helper RSS ≤ 51200 kB | yes | toxcore + libsignal-protocol-c |

Missing binary → fail. Real home paths → fail. No stage note `docs/stages/0N-….md` → phase not done.

Live plugin copy is **not** a verify step.

---

## 13. Phases

### Phase 0 — harness

Makefile, C test driver, gold runner, PKGBUILD stub, both LICENSE files, empty `THIRD_PARTY.md`.  
**Done:** `make verify-0` + `docs/stages/00-harness.md`.

### Phase 1 — 1:1 chat

1. Offline: `json_io`, `invite`, `roles`, `conversation`, `store.h`, gold + sanitizers + lock election. Manifest with `version`. `make arch`. Plugin validate.  
2. Owner approves `toxcore`; write `THIRD_PARTY.md`.  
3. Singleton helper + `tox_adapt` + `identity` + `message`. Two clients, one process.  
4. Quattro skeleton: Panel, Service (socket client only), list, chat, badge.

**Done:** `make verify-1` + `docs/stages/01-chat.md` (include measured idle RSS).  
Live install: separate yes.

### Phase 2 — invite is safe

QR PNG, revoke UI, remove contact, nospam UI (warns: voids all invites), safety code, rate limit.

**Done:** `make verify-2` + `docs/stages/02-invite-safety.md`.

### Phase 3 — groups

Read headers → `03-toxcore.md` → then `group.c` and UI. Same chat page.

**Done:** `make verify-3` + `docs/stages/03-groups.md`.

### Phase 4 — how mail appears

Cards on monitors, pin to tiling terminal window, sounds, themes, unread motion, desktop notify, bundled vs separate **cards**.

**Done:** `make verify-4` + `docs/stages/04-surfaces.md`.

### Phase 5 — daily

Export/import (`identity_exists` / `replace`), search on disk.

**Done:** `make verify-5` + `docs/stages/05-daily.md`.

### Phase 6 — file, then voice

**Done:** `make verify-6` + `docs/stages/06-media.md`.

### Phase 7 — package

**Halted.** Owner: AUR registration is temporarily off. Do not run `verify-7`, do not `makepkg` for upload, do not register an AUR account. Local PKGBUILD work waits for a new **go**. AUR upload is a second go after that.

**Done (later):** `make verify-7` + `docs/stages/07-package.md`. AUR upload only after another explicit go.

### Phase 8 — Double Ratchet on 1:1 (50 MB)

Tox stays the pipe. Direct `msg.send` is Double Ratchet (Arch extra `libsignal-protocol-c`). One helper, one Tox identity. Invite `rk=`. No Tor process. No per-contact Tox. Groups unchanged. Each direct peer receives a distinct persisted one-time prekey reservation; the receiver verifies that the ciphertext consumes the reservation issued to that exact stable peer. Request and response bundle frames are distinct and idempotent so a response can be replayed after a crash without a bundle ping-pong, while legacy `OQB1` remains a compatibility path.

**Starts** only after owner installs: `omarchy pkg add libsignal-protocol-c`.  
**Done:** `make verify-8` + `docs/stages/08-ratchet.md` (measured RSS). Live plugin: separate yes.

---

## 14. Time

Phase 0–1 ≈ 2 weeks (singleton + store are now in 1; that is the real work). Through 4 ≈ 6–8 weeks (pin/tiling is the long pole). Through 6 ≈ 8–12 weeks. Phase 7 a few days after.

If two local clients cannot exchange one message after bootstrap, **stop**. No fake transport.

---

## 15. First go

On **go**, do **phase 0 only**. Then halt. Phase 1 offline is the next go. Phase 1 tox is a third go (`toxcore` package).

---

## 16. Git (private)

Remote: `https://github.com/HANCORE-linux/OmaQ`  
**Private.** Only the owner may switch it to public. Do not change visibility. Do not add collaborators. Do not force-push to `main`.

After a phase is **tested and accepted** (`make verify-N` green + `docs/stages/0N-….md`):

1. Commit on `main` (or a phase branch merged to `main`): message `phase N: <short what landed>`.
2. Push to `origin`. That commit is the snapshot for that phase.
3. Do not commit unfinished work as a phase snapshot. Do not put keys, `tox.save`, real `OMAQ_HOME`, or live plugin paths in git.

`.gitignore` must keep `helper/omaq`, `tests/omaq_test`, `*.pkg.tar*`. History files never belong in the tree.

Phase 1 tox is not a snapshot until `verify-1-tox` is green. Offline-only may be committed as `phase 1 offline` if the owner wants that snapshot on the remote before toxcore.
