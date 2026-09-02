# OmaQ — execution plan

**Authority:** This file is how we build. German product contract: [`../../Prompt-Uebergabe/OmaQ.md`](../../Prompt-Uebergabe/OmaQ.md). If they disagree, stop and fix both before writing code.

**Status:** Phases 0–6 and 8 are done. Phase 7 (AUR) is halted. The live plugin is installed under `~/.config/omarchy/plugins/hancore.omaq`; source remains `/home/hancore/Projects/omaq`. Snapshot: [`CURRENT.md`](CURRENT.md).
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
4. **One helper process** per user session. Service starts a detached candidate and attaches through the private socket. The helper `flock`s; a contending candidate exits `2` without replacing the owner. No flock or stdio transport lives in QML.
5. **Helper is authoritative.** `Model.js` is UX precheck only. It never decides redeem, roles, or history.
6. Pure policy has no IO: `roles.c`, `invite.c`, `conversation.c`. IO lives in focused helper modules: `store.c` owns history, `surface.c` owns `surfaces.jsonl`, `auto_open.c` owns active Auto-open preferences, `group_file.c` owns strict Protocol-12 attachment framing, and `state_archive.c` owns non-overwriting private state copies. `tox_adapt.c` owns (`tox.h` + `toxav.h` + `toxencryptsave.h` only). Signal/Olm headers live only in `ratchet_adapt.c`.
7. `message.c` talks `store.h`, not paths. `group.c` talks `roles.c` + `tox_adapt`, not a second policy.
8. Every rule that can be wrong must be testable **without network and without toxcore**. `make arch` in `verify-0` enforces the include/IO greps.
9. Untrusted input (`json_io`, invite parse) builds under `-fsanitize=address,undefined -Wall -Werror`.
10. Do not invent RSS numbers. Do not invent crypto. Do not bind Omarchy window keys. Do not write the live plugin dir except one announced install.

A later session does not re-decide this. Change a rule only by editing this section.

### Structural follow-up

The current iteration introduces `SafeText.qml` as the PlainText-default sink for ordinary QML text while retaining the exact-source trust-boundary gate. The remaining architecture work stays behavior-neutral and separate from product changes:

1. Replace the monolithic `handle_op()` conditional with an explicit operation registry and extract message, group, identity, and storage operation families into focused translation units.
2. Register every remotely influenced path with a default-deny admission policy class. Keep specialized message, control, reaction, and group-file fairness/reservation behavior instead of flattening them into one token bucket.
3. Move related runtime globals into lifecycle-owned Invite, Group, I/O, Rate, and Identity contexts without creating one unbounded aggregate state object.
4. Add an explicit socket-only launch mode that omits stdout spooling while retaining and testing the foreground stdio compatibility mode. Do not remove the spool until launch contracts select the transport explicitly.

Each item requires its own regression-preserving change and review. Do not combine these refactors with a protocol or UI feature.

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
| — | Manifest `version`; helper-source and linked-binary GPL scope; verify-0 is a C driver; do not invent RSS | **Use.** |
| — | Helper death; sender and global rate limits; import must not clobber | **Use.** Ordinary incoming messages are bounded before replay lookup, history, unread persistence, and event fan-out. |
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
| Singleton | **Helper** takes `flock` on `$OMAQ_HOME/omaq.lock`. Service uses `Process.startDetached()` and then attaches to `$OMAQ_STATE/omaq.sock`. The lock owner binds that socket; a second starter exits `2` (`already_running`) without replacing it. No flock in QML (Quickshell.Io has Process, Socket, FileView — no lock). |
| License | QML/plugin **MIT**. OmaQ helper source **GPL-3.0-or-later**; the distributed helper binary is **GPL-3.0-only** while linked to `libsignal-protocol-c` 2.3.3. |
| Invite | Full grammar in §4. One use, 24 h, revocable, never auto-accept. |
| QR | Always tokenised `omaq://invite/…`. The Tox address is inside because Tox cannot add a friend without it. The token is what dies on revoke. **Nospam rotation voids every open invite at once.** |
| Model | `conversation` is `direct` or `group`. One chat page for both. |
| History | Append-only JSONL per conversation, `0600`, paged from disk. See §5. |
| Groups | `owner > admin > member`. NGC `observer` unused in 0.x. Phase 3 starts by reading toxcore headers, then implements only the gap. |
| Surfaces | **Card:** Quickshell overlay, drag to any monitor, stay there. **Pinned:** real Hyprland `xdg-toplevel`, terminal look. Then stock Omarchy keys apply (`SUPER+T` tile↔float, `SUPER+SHIFT+arrows` swap). Unpin (`lösen`) returns to the card. OmaQ does not bind those keys. |
| Theme | Default System (`colors.toml`). Palettes: Paper, Ink, Moss, Dusk, Ember. Terminal-pin style uses the same palette, monospace. |
| Sound | Compact Settings picker: off, UHOH, PING, MAIL, Aurora, Glow, Click, Knock, and user-imported files. UHOH uses the project-generated lossless `sounds/uhoh.wav`; its stable setting id remains `icq-message`. Users may remove only OmaQ-managed custom copies; bundled sounds remain immutable. Removed preset selections fall back to UHOH. |
| Memory | **50 MB RSS** for everything OmaQ starts (one helper + QML in a session). Do not invent a number in prose — measure. `verify-1-tox` records idle RSS; phase 6 records call-peak; phase 8 records ratchet idle + one-text. Fail if a single helper > 51200 kB. |
| Payload | **Double Ratchet** (Signal spec) on **direct** conversations. Tox is the pipe. Not SimpleX. Not a second handshake we write ourselves. Library: Arch extra `libsignal-protocol-c` (existing). Groups stay Tox-native until a later go. |
| Ratchet bootstrap | Direct invite requires `rk=` (32-byte identity key, 64 hex). The redeeming peer returns its own ratchet identity in the token-authenticated friend request; both expected pins and Signal identity keys persist before a bundle is accepted. Safety code still binds the Tox ids. |
| Out of 50 MB | No Tor child. No second Tox instance per contact. No SimpleX stack. |
| Bootstrap | Public Tox nodes, compiled in. Startup and each bounded periodic retry register both bootstrap nodes and TCP relays; TCP-only recovery never relies on a helper restart. |
| Live | Write `~/.config/omarchy/plugins/` only during an announced install or the verified shell-off update and rollback transaction. Verify uses temp dirs only. |
| Package | AUR in **phase 7**. **AUR account registration is temporarily off** — do not upload, do not create an AUR account, do not start phase 7 until a new **go**. Package files live under `/usr/share/omaq/`; the shell does **not** scan that path. Activation is an explicit user command (see §11). |

Incoming friend requests without a live token are rejected. Rate limit: 5 / minute / key, 20 / hour global; extras dropped. A leaked QR can knock, not flood.

---

## 2. Decomposition

One reason per file. Tests per module. Invite and message are reused by 1:1 and groups.

```text
Panel.qml  (one instance per monitor — host fact)
SafeText.qml                   # PlainText-default ordinary QML text sink
        → Service.qml          # start detached candidate, then attach/reconnect by socket
        → $OMAQ_STATE/omaq.sock
        → helper/omaq          # one process; flock lives here
              json_io.c        # closed scanner; sanitize-tested
              invite.c         # pure parse/issue/revoke (no IO)
              roles.c          # pure role_may(); no tox, no IO
              conversation.c   # id, kind, unread, last (no IO)
              message.c        # send/history use cases via store.h
              store.c          # only module that opens history files
              identity.c
              presence.c receipt.c
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

`Service {}` stays a child of `Panel`. It starts a detached helper candidate, waits briefly for socket setup, and communicates only through the private socket. A transient QML reload therefore does not own or terminate the established helper.

```text
Service:
  Process.startDetached(helper/omaq)
  attach $OMAQ_STATE/omaq.sock after the bounded startup delay
  request an instance/nonce-bound status handshake
  if the socket dies: emit helper_down; back off to 30 s; start a candidate and attach again

Helper (C):
  open $OMAQ_HOME/omaq.lock (0600)
  flock(LOCK_EX | LOCK_NB)
  if acquired: unlink stale sock, bind, write pid/protocol markers, serve
  else: exit 2 without disturbing the lock owner
```

An in-flight `msg.send` has unknown delivery status if the helper dies before reporting its correlated outcome. The UI must not offer automatic Resend because transport may already have succeeded. Restart reloads `tox.save`, history JSONL, persisted ratchet pins/identities/sessions; no second identity.

Source updates use an external shell-off transaction. Runtime and state staging paths must resolve outside the monitored plugin directory. The updater runs security-sensitive Git commands with fixed system binaries, sanitized configuration and transport environments, canonical HTTPS, and optional exact-commit binding. It binds a clean `main` checkout with the canonical `origin` and resolves canonical `origin/main`. An equal commit skips staging and every shell stop, while a previously pending helper may retry group-safe activation. Otherwise, the updater clones the complete replacement including `.git` below the private state directory, validates it, builds the helper there, validates it again, and records the exact commit plus helper SHA-256. Clone and build acquisition is bounded while files are written, as are retained update trees and required free space; a limit or timeout terminates the complete staging process group. Before stopping anything, the updater verifies equal filesystem devices and mount IDs, then exercises the required `mv -T --exchange --no-copy` operation with disposable directories on that mount outside the monitored tree. It then refuses a locked session, stops Quickshell, terminates the exact `omarchy-launch-shell` supervisor when it remains in backoff, and proves that the supervisor, shell, plugin watcher, and shell IPC are absent. Immediately before activation it repeats that stopped-state check and exchanges the staged and live trees only with `mv -T --exchange --no-copy`. Different filesystems, copy fallback, changed identities, concurrent cooperative restart, local changes, non-fast-forward history, and unsupported tree structure fail closed.

The complete old Git checkout becomes the external source backup. Before any replaceable-tree work, the controller copies its validated `helper-runtime.py` into the private runtime lock directory and uses that bound copy for the whole transaction. Before and after the exchange, `helper-runtime.py backup` copies the bound `/proc/<pid>/exe` image rather than the path contents. The post-exchange copy becomes the new tree's `helper/omaq.prev`. The updater binds the restarted launcher and Quickshell PID, start time, parent, executable, arguments, and session environment through every consumer poll. It requires the Omarchy plugin list, the OmaQ IPC target, running and available helper hashes, protocol compatibility, and the exact post-start journal cursor to pass before helper activation. A consumer failure stops the shell and exchanges the old tree back before activation. Explicit activation continues to use only `helper.shutdown_if_no_groups` with `--expect-sha256`; a blocked, uncertain, or unsupported result reports `update-pending: old helper, new tree`. The updater then repeats the bound-shell, helper-hash, and protocol checks; an inactive, substituted, or incompatible helper is never a successful terminal state. The staged `requiredHelperProtocol` must not exceed the running helper protocol, so this mixed state cannot turn into QML's hard protocol refusal. A degraded helper activation uses `helper-runtime.py restore` only while the shell watcher is stopped. There is no signal fallback, binary hot-swap, copy-based tree replacement, timer, or crash-recovery journal.

**Lock contention is `verify-1-offline`:** two helper processes, one temp home, no network, no toxcore. Exactly one stays up, the other exits 2. `verify-1-tox` still asserts one process when Tox is on.

The socket carries one JSON object per line. Foreground stdin/stdout remains a separately tested compatibility transport inside the helper; detached QML does not consume it. stderr contains diagnostics, never keys or full Tox IDs.

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
- Incoming replay checks use a bounded, process-keyed Bloom index for the complete retained history plus an exact recent-ID cache. A negative lookup avoids disk scanning; a possible match still uses the exact on-disk decision. Rotation and conversation clearing invalidate the index before mutation.
- **`message.c` talks only `store.h`.** `store.c` is the only file that opens history paths. A later at-rest wrap (libsodium already linked via toxcore) is a new `store` file, not a rewrite of `message.c`. Phase 1: 0600 only, no extra cipher.
- Phase 5 search: scan jsonl on disk for one explicitly requested conversation. Each ChatPage owns its request id and result state, and stale or cross-conversation results are ignored. Still not the whole archive in QML.
- `ts` is the helper-authored local history-acceptance time. The same captured value is persisted and projected in a successful live `message` event. Older helpers may omit the additive live field; confirmed rows then remain without a displayed time until authoritative history supplies it. Only an optimistic local row uses its enqueue time as a temporary presentation value.
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
{"op":"search","conversation":"...","text":"...","limit":20,"id":"client-request-id"}
{"event":"message","conversation":"...","id":"...","text":"...","dir":"in","ts":1700000000}
{"event":"search","conversation":"...","request":"client-request-id","items":[{"id":"...","ts":1700000000,"from":"peer","text":"...","dir":"in"}]}
{"op":"identity.export","path":"...","id":"client-request-id"}
{"op":"identity.inspect","path":"...","id":"client-request-id"}
{"op":"identity.import","path":"..."}
{"op":"identity.import","path":"...","replace":true}
{"op":"identity.protect","passphrase":"..."}
{"op":"identity.unlock","passphrase":"..."}
{"op":"identity.unprotect","passphrase":"..."}
{"op":"surface.set","conversation":"...","monitor":"...","x":0,"y":0,"width":420,"height":420,"pinned":false}
{"op":"surface.get","conversation":"..."}
{"op":"attachment.inspect","path":"...","id":"client-request-id"}
{"op":"attachment.stage.create","id":"client-request-id"}
{"op":"attachment.stage.commit","path":"helper-created-staging-path","id":"client-request-id"}
{"op":"attachment.stage.discard","path":"helper-created-staging-or-pending-path","id":"client-request-id"}
{"op":"sound.list","request":"client-request-id"}
{"op":"sound.import","path":"absolute-source-path","request":"client-request-id"}
{"op":"sound.remove","id":"32-hex-managed-sound-id","request":"client-request-id"}
{"op":"file.send","conversation":"...","path":"...","kind":"file|image","id":"client-request-id"}
{"op":"file.accept","id":"...","path":"..."}
{"op":"file.cancel","id":"..."}
{"op":"file.status","conversation":"...","id":"client-request-id"}
{"op":"call.start","conversation":"..."}
{"op":"call.answer","conversation":"..."}
{"op":"call.stop","conversation":"..."}
{"op":"helper.probe","id":"32-hex-helper-instance","request":"client-request-id"}
{"op":"helper.shutdown_if_no_groups","id":"32-hex-helper-instance","request":"client-request-id"}
```

Helper → service: `snapshot`, `request`, `message`, `message.failed`, `message.reaction`, `receipt`, `receipt.sent`, `receipt.failed`, `connection`, `group.changed`, `attachment.stage`, `attachment.inspected`, `attachment.rejected`, `attachment.discarded`, `file.offer`, `file.sending`, `file.done`, `file.canceled`, `file.failed`, `call.incoming`, `call.state`, `helper.probe`, `helper.shutdown`, `helper.shutdown_blocked`, `identity.recovery`, `identity.primary`, `invite.redeemed`, `direct.reinvite`, `settings.auto-open`, `settings.auto-open.failed`, `sound.list`, `sound.failed`, `helper_down`, `error` (`invite_expired` | `unsupported` | `forbidden` | `invalid_image` | `identity_exists` | `identity_backup_failed` | `identity_passphrase_required` | `identity_import_failed` | `identity_state_archive_failed` | `identity_rollback_failed` | `identity_missing` | `identity_mismatch` | `identity_guard_invalid` | `identity_recovery_degraded` | `identity_primary_uncertain` | `invite_self` | `contact_exists` | `contact_limit` | `invite_rejected` | `safety_key_changed` | `rate_limited` | `locked` | `request_required` | `no_ratchet` | `ratchet_pending` | `history_failed` | `busy`).

Invite creation and revocation expose the helper-issued absolute expiry and echo a UI request nonce. The panel derives only the live countdown presentation; confirmed **New link** waits for the old invite's authoritative revocation event before creating its replacement. Protocol 3 introduced the requirement that every `msg.send` must carry a non-empty client request `id`; missing IDs fail with `request_required` before transport. In Protocol 3, `file.*` and `call.*` were 1:1 only (`conversation` was a friend number), and group ids (`g…`) returned `forbidden`. Protocol 12 changes only the file rule; calls remain Direct-only. Incoming files stay paused until `file.accept`. Dest default: `~/Downloads/omaq/<name>`, `0600`, cap 8 MiB. An explicit destination override remains supported but never follows symlinks. Peer avatars use exclusive staging, full PNG/JPEG/WebP decode with bounded dimensions and decoded bytes, canonical PNG rewriting, and atomic installation before any avatar event. The optional client request `id` on `file.send` is echoed as `request` so multiple UI clients correlate `file.sending`; that event exposes the validated transfer id required for outgoing cancellation. Protocol 3 adds request-correlated message outcomes: `msg.send.id` is echoed as `message.request`, while pre-delivery failures use `message.failed` with `delivered:false`. A post-transport history failure emits the successful `message` event followed by `message.failed` with `delivered:true`; this means transport was committed, not that a peer receipt arrived, and it must never offer Resend. If the helper disconnects after accepting a send but before reporting an outcome, the UI marks that request `delivery_unknown` and does not offer automatic Resend because transport may already have succeeded. Status handshakes require protocol 4, include a per-process `instance` id, and echo a fresh status-request nonce so stale replay records or older attached helpers cannot release queued operations. The helper persists per-conversation unread counts and broadcasts `unread` updates so every bar instance shows and clears the same badge state. After the authoritative friend and group registries load, unavailable-conversation unread entries are pruned transactionally so an orphan cannot leave a permanent, non-actionable widget badge. After reconnecting to the same instance, `file.status` replays the cached state for the originating request. `receipt.sent` and `receipt.failed` correlate local read-receipt attempts by message id so transient failures can be retried without affecting message-send state. A local or remote normal-file cancellation ends with direction-tagged `file.canceled` on both peers, while transport errors use `file.failed`. File events include `dir:"in"|"out"`. Completed incoming files are persisted as history messages with `kind:"file"`; persistence failure emits `history_failed` after the completed path event. Protocol 4 uses the stable 32-byte Tox group chat ID for group conversation identity and persistent state; process-local Tox group numbers never cross the helper boundary. The private `groups.tsv` registry uses identity-bound proofs before reconnecting a missing group, reconciles metadata with groups present in Tox saved state, participates in identity replacement and versioned identity export bundles, and must not be synchronized separately. An orphaned private-group ID is visibly pruned because a private group cannot be rejoined from its chat ID alone. Legacy `g<number>` history remains archived in place, while obsolete unread/surface entries are ignored and reported as `legacy_group_state_archived` because mapping a reused process-local number would be unsafe. Protocol 5 adds generation-tagged `friend.list.begin`, `friend.info`, and `friend.list.end` projections plus correlated `group.invite.sent`/`group.invite.failed` outcomes. Protocol 6 binds destructive contact removal and targeted group invites to the projected stable friend public key and requires the helper to re-read that key at the transport boundary. Every targeted group invite also carries a client request nonce echoed by its correlated `group.invite.sent` or `group.invite.failed` outcome; the UI keeps the successful result visible as sent and waiting for acceptance. The helper keeps a bounded in-memory terminal-result ring and replays it on status so a same-instance socket reconnect cannot strand the UI; clients reject older helpers before releasing queued operations. Protocol 7 makes `conversation.read` helper-authoritative: a crash-recoverable journal couples unread clearing to a bounded persistent receipt outbox, receipt-capable peers silently advertise support through an old-helper-safe control frame, and authenticated application acknowledgements retire entries only after the message author processed them. Direct legacy peers receive a bounded terminal fallback, retry work is batched, and reconnect history carries an authoritative unread snapshot so lost IPC events cannot leave a stale `Delivered` indicator. Protocol 8 gates request-correlated Identity inspection/actions and stable direct-friend projections for group members, so the updated UI never releases these operations to a protocol-7 helper. Named private groups are helper-authoritative and capped at 10 members through both local invite policy and Tox NGC's peer limit. Inviting an existing contact sends the group token only through the established Signal-encrypted direct session (bootstrapping that Ratchet session first when necessary), then delivers the native private Tox invite for explicit acceptance; the control payload is never rendered as a direct chat message. A self-disconnected or kicked recipient removes the stale local Tox group before registry cleanup, allowing a later fresh targeted invite to be displayed and accepted. Because a Tox NGC peer key is group-specific and differs from the direct friend key, the recipient first confirms its claimed group key over the authenticated direct Ratchet session and only then proves possession of the encrypted invite secret from that exact group-message sender. The reserved `OQX1` control envelope remains invisible to older helpers. The inviter persists at most nine friend/member mappings per group in the `OMAQGF1`-versioned `group-friends.tsv` sidecar, journals both handshake sides across helper restarts, and acknowledges the result over the direct Ratchet session. The recipient fsyncs a pre-accept transaction before native acceptance; an interrupted transaction removes the accepted group during recovery, while a committed proof debt remains durable until acknowledged rather than expiring offline. If an inviter loses an unestablished expectation, a fresh authenticated direct member descriptor can recreate it only for a member key currently present in that exact group; the subsequent group-key possession proof remains mandatory. Identity export fails with correlated `busy` while any live invite or binding debt remains, so a bundle never silently drops that debt. Group removal journals the paired `groups.tsv`/`group-friends.tsv` update and completes an interrupted pair before startup reconciliation. Unknown mappings fail closed unless they belong to the exact registry entries pruned during the same startup reconciliation. The legacy three-column `groups.tsv` format remains unchanged; version-2 Identity bundles include both private registries while version-1 bundles remain importable. Generation-tagged `group.list.begin`, `group.info`, `group.member`, and `group.list.end` events atomically project names, group-specific member keys, optional bound direct friend keys, roles, and online state to every UI client. Group message actions, allowlisted reactions, and receipts use Tox-native group messages. Through Protocol 11, group file transfer remained unavailable because Tox NGC has no native group file primitive; Protocol 12 adds the bounded custom-packet envelope described below. Calls are direct-chat-only, audio-only (48 kbit, video 0), and use bounded 48 kHz mono PCM rings with a single interruptible `libpulse` event-loop thread; ToxAV send/iterate remains on the helper thread, while hangup wakes and joins the audio loop without canceling a blocked PulseAudio call. One process-wide QML singleton loops the bundled `phone.oga` progress tone only while at least one surface requests `incoming` or `ringing`, then stops it for `active`, declined, ended, or otherwise terminal call state; multiple monitor surfaces never layer duplicate playback. Tox runs in TCP-relay privacy mode with UDP, local discovery, and hole punching disabled so contacts do not receive each other's IP addresses. Hangup and incoming-call decline use `TOXAV_CALL_CONTROL_CANCEL`. Active calls emit `call.state:active`; status snapshots reconcile the authoritative call state after reconnect, and the UI owns only the elapsed-time presentation. Protocol 9 adds helper-authoritative PNG/JPEG/WebP inspection and `kind:"image"` history. At Protocol 9, inline images remained ordinary direct-chat file transfers, were capped by the 8 MiB file limit, rendered only as 56×56 local previews, and opened through the stored local path; groups still rejected every file operation until Protocol 12. Incoming images are fully decoded and canonically rewritten before QML receives an image-kind event. Selected and clipboard images first enter an exclusive 0600 helper-created path under the private attachment directory, are bounded while copied, then decoded and atomically adopted as canonical PNG. Pending sidecars let startup remove abandoned staging without deleting an attachment already accepted for transport. Discard requests remain request-correlated cleanup debts until the helper acknowledges `attachment.discarded`, so a same-instance IPC reconnect cannot orphan a canceled stage. Protocol 9 also provides an instance-bound helper probe and acknowledged shutdown. Uninstall binds the protocol marker to PID, UID, process start time, exact executable inode, instance, and socket, owner-binds its anti-respawn marker to PID/UID/start time, and holds the private helper state lock throughout plugin removal. It uses only an atomic `helper.shutdown_if_no_groups` operation. The helper requires both registered and native Tox group counts to be zero with no unmapped registry or cleanup debt and a successful durable Tox save before quiescing. It sends the correlated acknowledgement only to the requesting socket and resumes service if that acknowledgement cannot be flushed. Active or uncertain group state, overlapping startup, an unsupported safe operation, incomplete runtime markers, a missing or replaced executable or socket, a malformed response, or a missing correlated acknowledgement abort removal without signaling the helper. Protocol 10 retains base UI compatibility with Protocol 7 and capability-gates Identity, attachment, and recovery operations by the negotiated helper protocol. A strict private identity-presence record binds startup to the established Tox public key, while a current recovery copy preserves the complete Tox savedata, including contacts, with the same passphrase protection as the primary save. The primary `tox.save` remains the mutation commit point. Every established save first durably arms both a primary-uncertainty marker and a recovery-stale marker. The first clears only after primary-directory `fsync`; the second clears only after the matching recovery copy is atomically renamed and its directory is synced. A failed secondary update therefore produces a visible degraded-recovery state, and a stale copy is never auto-restored after primary loss. A primary-directory durability failure is distinct and fail-closed: the running backend is discarded and the next start re-reads whichever state the filesystem durably retained. A durable `identity-primary-uncertain` marker keeps that warning visible across the automatic restart until the user reviews Identity/contact state and acknowledges it through the request-correlated `identity.primary.acknowledge` operation. While it remains, Tox iteration and all mutating IPC are paused; journaled add/remove outcomes are resolved against the saved Tox state only after acknowledgement, without replaying a contact removal that did not commit. A missing established `tox.save` is restored only from that helper-managed, non-stale copy; missing, malformed, symlinked, guard-less guard-era, or fingerprint-mismatched state fails closed without creating a replacement identity. A separately confirmed Import may repair missing or mismatched primary state only when the staged bundle has the exact protected public fingerprint; a crash-recoverable transaction preserves or restores any displaced primary. Unbound legacy direct state emits `direct.reinvite` with explicit retained-Identity/contact facts. Clearing that durable warning requires the request-correlated `direct.reinvite.clear` operation after an explicit UI confirmation. Direct invite redemption reports request-correlated self, existing-contact, capacity, transport-rejection, and safety-key-change outcomes instead of collapsing them into `forbidden`. Protocol 11 removes ephemeral friend numbers from durable window and Auto-open identity. The helper stores direct surfaces only as `d:<public-key>`, resolves them to a current friend number only for IPC projection, and includes the exact direct key on replayable direct events. Existing numeric surface records are ambiguous after contact removal, so the helper archives the complete private source before atomically discarding those records; it never maps them through the current holder of the number. Auto-open schema version 2 stores only stable `d:`/`g:` keys. The helper is its only reader and writer: it performs bounded descriptor-relative no-follow parsing, rejects duplicate or ambiguous schema fields, archives every pre-v2 source, disables numeric Direct records, and atomically rewrites a private file with a fail-closed Direct default before returning the request-correlated snapshot. Every live Direct card retains the expected public key, queued Direct operations carry that key and are revalidated before flush, and a Protocol-11 helper requires the exact key and canonical numeric transport handle on every Direct operation. A reused number therefore cannot restore, update, auto-open, or send from a window bound to another contact. Protocol-7 through Protocol-10 helpers remain usable: unsupported stable-state migration disables legacy Auto-open safely, direct surface restore requires a key-bearing projection, and ordinary current-contact actions continue with the existing wire operations. Protocol 12 adds group attachment parity without adding group calls. Because Tox NGC has no native file primitive, the helper broadcasts only an `OQGF1` offer and uses ordered lossless custom private packets for each accepting online member. Fixed transfer slots, exact framing, a 1373-byte packet ceiling, 1343-byte data chunks, an 8 MiB file cap, explicit acceptance, idle deadlines, durably reserved random transfer IDs, stable group/member-key checks, sequential offsets, an exact size plus SHA-256 digest, and application ACK/FAIL bind every transfer. Incoming files remain exclusive 0600 downloads and images pass the existing canonical decoder before history or QML projection. Offline or pre-Protocol-12 members do not receive attachments retroactively. Group member removal and group leave/dissolve fail with `busy` across relevant live transfers, while VoiceCall remains Direct-only. Protocol 13 makes restart projection and the remaining non-call chat semantics explicit: complete group lists carry helper instance, request, generation, and exact group/member counts; QML validates the whole snapshot before replacing its last good list and requests bounded retries after loss. `OQGT1` lossless custom packets project transient typing by stable member key without leaking controls into history or older clients. Group receipts persist monotonically per stable member key instead of treating the first member's receipt as the state of the whole group. Clipboard image preparation reports handshake/update failures without discarding the pending user action, and valid Unicode emoji-only sequences use the same fixed 56-pixel presentation whether selected or pasted. Protocol 14 keeps each native chat delegate keyed to one conversation so list insertion or removal cannot transfer another window's size, persists bounded width and height with the existing stable surface identity, and applies non-overlapping first placement only to the newly mapped floating chat. It also adds request-correlated custom-sound listing, import, and removal through the helper-owned `sound.c` boundary. Imports require a fully bounded, structurally valid PCM WAV of at most 8 MiB and 30 seconds and become private no-follow copies below `OMAQ_HOME/custom-sounds`; removal accepts only a projected 32-hex managed id and never targets the original source or bundled sounds. Message scaling now also reaches composer text, and selectable DirectChat/GroupChat text exposes exact-selection Copy plus inline Reply without changing the message context menu.

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

Remember: monitor, x, y, width, height, `pinned` true/false.

Geometry remains isolated per conversation. Opening a chat restores only that chat's saved geometry or assigns non-overlapping initial placement. It must not recenter, stack, move, or resize another open chat. Closing a chat must not apply its dimensions to any remaining chat.

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
- **Admin moderation:** `roles.c` authorizes an admin to remove an ordinary member while denying the owner or another admin. The GroupChat UI admits offline ordinary members, and the helper refreshes the exact stable member key and current role before the native Tox kick.
- **Admin invitation:** admins use the same stable-friend-key, request-correlated Signal control and explicit native acceptance path as owners. A three-helper regression covers admin invitation, acceptance, ordinary-member removal, and denied owner removal.
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

Settings (phase 4): `notifyBadge`, `notifyRightPanel`, `notifyDesktop`, `surfaceMode` (`separate`|`bundled`), `sound`, `soundCustomPath`, `chatTheme`, `messageScale` (`0.9`, `1.0`, `1.1`, `1.2`, or `1.4`), `animateUnread`. The compact panel uses a fixed mutually exclusive self/request header: a pending friend or group request replaces the complete self presentation until the request ends. It also uses a fixed support-link frame, a Friends/Groups frame, and a fixed action rail. The two compact upper frames share the former header height; the transparent GitHub and monochrome Ko-fi support glyphs remain borderless, use `color03` on hover/focus, and match the cell size, glyph scale, and column spacing of the rail below. The panel card remains a literal 400 QML pixels wide. Friends begin at the top of the lower-left frame; their column count derives from the inner grid width rather than contact count, the standard font bases use one column, and entries after the fifth remain reachable in the vertically scrollable area with its fading scrollbar. The heading shows online/total counts, unread names are underlined, and hover/focus uses `color03`. An active direct invite explains sharing through a trusted channel, one-time redemption, and explicit verification and acceptance without claiming that acceptance opens the chat. Every rail selection and confirmation remains enclosed by the thin lower-left frame. Rail menus extend the panel only to their natural height while the header and right frames stay stationary; the lower-left area scrolls only when constrained by screen height. Group-name and Identity path inputs use the panel's full field width; message search belongs to each ChatPage. Selecting a listed group opens its chat directly. The group-chat header can invite an existing contact or let any role leave after confirmation; member and invite entries remain unframed. Complete group sender names determine the minimum bubble width, and helper-persisted system rows identify member joins and leaves. Message scaling affects message bodies and text typed in the composer while the composer frame and controls retain their normal size. Settings provide a live preview. Custom sound import stores a bounded private helper-managed copy; only projected custom entries expose removal, and bundled sounds remain immutable.

DirectChat and GroupChat expose a compact Reply icon on every replyable message in the same hover/focus action group as reactions and editing. The icon starts the existing reply flow; the existing right-click context menu remains unchanged. Message text supports pointer and keyboard selection. A compact Copy icon appears only while a non-empty selection exists and copies exactly the selected text.

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
`license`: `MIT` and `GPL-3.0-only` for the distributed payload; OmaQ's helper source remains GPL-3.0-or-later.

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

**Halted.** Owner: AUR registration is temporarily off. Do not run `verify-7`, do not `makepkg` for upload, and do not register an AUR account. Local PKGBUILD work and its stage note wait for a new **go**. AUR upload requires a separate approval after that.

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
