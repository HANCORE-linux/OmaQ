# Handoff: validate + fix the 25bcd05 review

**For a later session.** Do not re-derive this. Work in `/home/hancore/Projects/omaq`. Chat German with the owner; code/docs English.

**Reviewed report:** `/tmp/omaq-review-25bcd05-report.md`  
**Review target then:** isolated archive of `7d31c7c` (code `25bcd05`).  
**Current `main` when this handoff was written:** `bbbecb0` (docs pin) on top of `6a9503c` (friends/avatars/presence/float).  
**Do not** check out `25bcd05` to “match the report”. Implement on current `main`.

**Do not** spawn a verifier unless the owner says yes. **Do not** AUR. **Do not** silent `omarchy restart shell`. **Do not** invent crypto. Copy to live plugin only after announcing. Commit+push only to private `HANCORE-linux/OmaQ` with noreply author `230438592+HANCORE-linux@users.noreply.github.com`.

Architecture law (`docs/PLAN.md`): new capability = new module + `op`/`event` + gold test, then code. Domain not in QML. Helper is authoritative.

---

## How to read the report

The report is mostly **correct against 25bcd05**. Several items are **already fixed or overtaken** on current `main`. A few evidence lines are **wrong** even for 25bcd05. Treat the table below as the source of truth, not the report’s Status: open.

Verdicts:

| Verdict | Meaning |
|---|---|
| **CONFIRMED** | Still true on current `main`. Fix as specified. |
| **PARTIAL** | Core claim true; some evidence stale or already half-done. |
| **STALE** | True at 25bcd05, **already addressed** after `6a9503c`. Do not re-implement; verify then skip. |
| **OVERSTATED** | Kernel of a point exists, but the report’s evidence or severity is wrong. |
| **FALSE** | Evidence does not hold. Do not “fix”. |

---

## Priority order (do in this sequence)

1. P0 security: #1, #2, #3  
2. P1 ship-blockers: #4, #12, #13, #18, #8, #9, #10, #11  
3. P1 UI restore: #6 (load surfaces), Close already fixed — do not re-break float  
4. P2 product gaps: #15, #16, #17, #7 (product decision)  
5. P3 UX: #19–#23  
6. P4 docs/nits: #24–#31 except **skip FALSE #26 evidence**, **skip STALE #14/#5 as code work**

After each P0/P1 cluster: `make helper test arch` and copy **announced** live (`install` of `helper/omaq` binary + QML). History event must include `conversation`.

---

## Issue-by-issue

### Issue 1 — plaintext fallback without ratchet — **CONFIRMED** — security

**Still true:** `helper/omaq.c` `msg.send` for direct: if `HAVE_SIGNAL` and `g_ratchet` then encrypt; **after `#endif` still `omaq_tox_send(..., op->text)`**. Makefile sets `-DHAVE_SIGNAL` only if pkg-config finds libsignal. README `plugin add` does not require it.

**Fix (exact):**

1. Direct 1:1 (`cid[0] != 'g'`): if `!g_ratchet` or encrypt/send fails → `emit_error("no_ratchet")` or `"forbidden"` and **do not** send plaintext. Groups may stay Tox-native (contract: groups not ratcheted).
2. Makefile: if `SIG_OK != yes`, **fail `make helper`** with a message to install `libsignal-protocol-c`. Do not build a helper that can send direct plaintext.
3. README Install: require `toxcore` and `libsignal-protocol-c` (or “build helper on the machine after clone”).
4. Test: gold or a small helper-IPC test that `msg.send` without ratchet emits error, not a successful snapshot with plaintext on the wire. Offline: compile-time `#ifndef HAVE_SIGNAL` path must not send direct text.

**Do not:** ratchet group/file/call.

---

### Issue 2 — `rk=` map 8 slots, RAM-only, TOFU trust — **CONFIRMED** — security

**Still true:** `g_rkmap[8]` in `helper/omaq.c`; `rk_set` drops the 9th silently (`free_i < 0` return). Not written to disk. `id_trust()` in `helper/ratchet_adapt.c` returns **1** if no stored identity (`!s || !s->buf`).

**Fix (exact):**

1. Persist expected `rk` per conversation under `$OMAQ_HOME` (new small store, e.g. `rk.jsonl` or files `rk/<conv>`), 0600, load on helper start. No 8-entry cap (or a high documented cap with error, not silent drop).
2. `rk_set` must fail/emit error if it cannot store.
3. `id_trust`: if we **have** a pinned rk/identity for that address, compare; if we **expect** a pin from invite `rk=` and none stored yet, store first (invite path only). If a session exists and key mismatches → **0**. Do **not** return 1 for a changed key. First-seen without invite pin: fail-closed for direct **or** document TOFU in STAND and refuse the current KCI sentence.
4. STAND: rewrite the KCI line to match the code after the fix. No “all sessions” claim until persist + no TOFU-1.

**Do not:** invent a new ratchet algorithm.

---

### Issue 3 — Tox group invite bypasses OmaQ token — **CONFIRMED** — security

**Still true:** `hook_ginv()` copies any NGC invite into `g_have_gpending` and emits `request` with no check against `g_issued_id` / redeemed group URL. `contact.decide` then `omaq_tox_group_invite_accept`. Direct friend requests **are** token-gated (`hook_req`).

**Fix (exact):**

1. Do not treat raw Tox group invites as OmaQ-token invites.
2. Preferred product: **ignore/cancel** NGC invites unless there is a matching, unexpired, redeemed `k=group` invite for that group (store pending redeem id).  
   Alternative (weaker, only if owner prefers): emit `request` with `"kind":"group-external"` and UI copy “not an OmaQ invite link”.
3. Rate-limit group-invite events like QR redeem (`omaq_rate_allow`).
4. Gold: a unit-level policy if you extract “may accept group invite”; otherwise a helper-IPC test. Update STAND: “link/QR is the only in-product group join”.

**Do not:** auto-accept.

---

### Issue 4 — Git install has no `helper/omaq` — **CONFIRMED** — bug

**Still true:** `.gitignore` has `helper/omaq`. `Service.qml` execs that path. PKGBUILD is still `package() { true; }` `depends=()`. README `omarchy plugin add <git>` cannot start.

**Fix (exact):**

1. README Install: clone/add is not enough. Document: `make helper` on the machine (`toxcore`, `libsignal-protocol-c`, gcc), then copy `helper/omaq` into the plugin dir, **or** wait for AUR (halted).
2. Optional: a `scripts/install-local.sh` that `make helper` and `install`s binary + QML into `~/.config/omarchy/plugins/hancore.omaq` (announce before run).
3. Do **not** commit the binary. Do **not** un-halt AUR.

---

### Issue 5 — Live history protocol mismatch — **STALE as a code bug; keep as process**

**Then:** live helper emitted history **without** `conversation`; QML `sameConv` dropped it.

**Now:** repo `helper/omaq.c` history event includes `conversation`. Live **binary** was compared equal to repo helper at handoff time; live **`.c` copy** in the plugin folder can still be stale (we copy the binary, not always every `.c`).

**Fix:** not a protocol change. Add `scripts/sync-live.sh` (or the install script) that copies `helper/omaq` + QML from repo and fails if `cmp` binary differs. After every helper change, announce and copy. Add a helper-IPC test: `history` reply contains `"conversation":`.

**Do not:** remove `conversation` from the event.

---

### Issue 6 — surfaces not restored in QML — **CONFIRMED** — bug

**Still true:** `openCards` starts `[]`. No `surface.get` / list-all from QML. `ensureCard` always writes `x:40,y:80,pinned:true`.

**Fix (exact):**

1. Helper: `surface.list` (or snapshot field) returning all rows from `surfaces.jsonl` (real name, see #30).
2. `ChatSurface` onCompleted: request list; for each `pinned:true` call `ensureCard` **using stored x,y,monitor**, do not overwrite with 40/80 unless missing.
3. `dismissCard`: `surface.set` with `pinned:false` (or delete row).
4. Do not break always-float on open (`float-omaq.sh`, titles `OmaQ chat` / `OmaQ chat — name`).

---

### Issue 7 — no Unpin / no terminalLook — **PARTIAL / product**

**Still true:** `ensureCard` always pins. Close dismisses. No unpin-to-card. ChatPage has no `terminalLook: true` on the toplevel.

**Product fork — ask owner if not already decided:** current owner UX is “Demo/Chat always floating windows”. Contract still has pin=toplevel / unpin=card.

**If owner keeps always-float:** document in STAND/OmaQ.md that overlay cards are deferred; remove “unpin restores card” from live claims. Close stays dismiss. **Do not** reintroduce overlay-only chat as default.

**If owner wants the contract:** add Unpin on the chat window → `pin(conv,false)` + overlay card; set `terminalLook: true` only on the Hyprland toplevel. Keep float rules for the toplevel.

Default for the implementing session: **document as deferred** unless the owner says “unpin now”.

---

### Issue 8 — `inviteToGroup` uses `atoi("g0")` → friend 0 — **CONFIRMED** — bug

**Still true:** `group.changed` sets `lastConversation` to the group id (`g…`). `inviteToGroup()` sends `id: lastConversation`. Helper `atoi` of `g0` is 0.

**Fix (exact):**

1. Service: keep `lastDirectId` (numeric friend) separate from `lastGroup` / `lastConversation`.
2. `inviteToGroup` sends `id: lastDirectId` only if it matches `^[0-9]+$`.
3. Helper: reject `invite.create` group if `id` is not a decimal friend number (`emit_error("unsupported")`).
4. Test: gold or IPC: `id":"g0"` must not invite friend 0.

---

### Issue 9 — file accept/preview not scoped — **CONFIRMED** — bug

**Still true:** `fileForThis` gates the buttons; `acceptFile`/`cancelFile` use global `lastFileId`. `lastFileName`/`lastFilePath`/`imageFile` are global.

**Fix (exact):**

1. Service: store offer as `{ id, name, path, conversation, pending }` (or a small map keyed by conv).
2. `acceptFile(conv)` / `cancelFile(conv)` / preview bindings use that conv only.
3. ChatPage already passes conversation into send; do the same for accept/cancel.

---

### Issue 10 — Panel Answer/Hangup uses `lastConversation` — **CONFIRMED** — bug

**Still true:** `Panel.qml` `omaq.answerCall()` / `stopCall()` with no args. Service falls back to `lastConversation`. `lastCallConv` exists but Panel ignores it.

**Fix (exact):** Panel incoming-call buttons: `omaq.answerCall(omaq.lastCallConv)` and `omaq.stopCall(omaq.lastCallConv)`. If `lastCallConv` empty, no-op. ChatPage already passes `root.conversation`.

---

### Issue 11 — history only onCompleted, skip if `lines.count > 0` — **CONFIRMED** — bug

**Still true:** `pages/ChatPage.qml` history in `Component.onCompleted`; `onHistoryTickChanged` returns if `lines.count > 0`. Right-dock ChatPage is reused.

**Fix (exact):**

1. `onConversationChanged`: `lines.clear(); service.requestHistory(conversation)`.
2. Merge history: apply items for matching conv even if some live lines exist (dedupe by text+dir+order, or replace if still empty of **history**, keep live-only lines after).
3. Never drop a just-sent out bubble because history arrived late (match by last out text).

---

### Issue 12 — out bubble before send succeeds — **CONFIRMED** — bug

**Still true:** `send()` appends `out` then `sendOp`. Helper can return snapshot without sending plaintext (OQB1-only) or `error`.

**Fix (exact):**

1. After Issue 1, emit a distinct success event e.g. `{"event":"message","conversation":"...","text":"...","dir":"out"}` when the ciphertext (or group text) is actually on the wire and appended to JSONL.
2. QML: append `out` only on that event, **or** append as pending and set failed on `error` for that conv. Demo path can stay immediate.

---

### Issue 13 — ratchet sessions RAM-only — **CONFIRMED** — bug

**Still true:** `sess[]` in `ratchet_adapt.c` is in-memory. Identity/SPK persist; session records do not. Incoming `OQR1` with no session is dropped with no event.

**Fix (exact):**

1. Persist session records next to other ratchet files under `$OMAQ_HOME` (0600), load in `sess_load` / save in `sess_store` (already hooked — write the buffers to disk).
2. Decrypt miss: `emit_error` or `{"event":"message","dir":"sys",...}` so the receiver sees failure; do not silent `return`.

---

### Issue 14 — no contact list — **STALE**

**Then:** no roster. **Now:** panel **FRIENDS** from `event:friends`, click opens that 1:1.

**Remaining:** Search/Safety/Remove still use `lastConversation`. After opening a friend, `openFriend` sets `lastConversation`. Verify that; if More→Remove still uses the wrong id, bind those buttons to the selected friend id.

**Do not:** rebuild a second roster.

---

### Issue 15 — group role UI missing — **CONFIRMED** — bug / product gap

Helper has `group.member.setRole` / `remove` / `leave`. Panel only create / invite / dissolve.

**Fix:** add a small Members block when `lastGroup` is set: list is optional if helper has no list op — then at least Leave, and Kick/SetRole with member id field. Or document “group admin UI not in 0.6” in STAND and stop claiming it in README. Prefer UI if small; otherwise docs.

Ask owner only if the block would be large. Default: **Leave + Kick member id** using existing ops; skip a full member roster unless there is already an event.

---

### Issue 16 — Admin cannot promote to admin on Tox — **CONFIRMED** — docs/product

`omaq_role_may` allows admin `setRole` to `ROLE_ADMIN`. `tox_group_set_role` follows toxcore (founder/moderator rules). Gold tests the **pure** policy, not the wire.

**Fix:** STAND + `OmaQ.md` + `03-toxcore.md`: “OmaQ policy allows admin→admin; **on Tox NGC only the founder can promote to moderator**. UI must surface `forbidden`.” Do not change gold policy to lie; optionally have helper map tox error to a specific code `tox_role`.

**Do not:** fake founder on the wire.

---

### Issue 17 — no Import UI — **CONFIRMED** — bug / gap

`exportIdentity` in Panel; `importIdentity` only in Service.

**Fix:** More → Import path field + checkbox replace + confirm if `identity_exists`. Reuse zenity or path TextField like file send. Call `omaq.importIdentity(path, replace)`.

---

### Issue 18 — helper restart / no op queue — **CONFIRMED** — bug

**Still true:** `onExited`: code 2 → socket; `code !== 0` restart; **code 0 does not restart**. `sendOp` writes `sock` or `proc` with no queue.

**Fix (exact):**

1. Queue outbound lines until `sock.connected` or proc stdin ready; flush on connect.
2. If helper exits 0 unexpectedly while the plugin still wants it (not user-disabled), `scheduleRestart` as well (or treat stdin-EOF as down).
3. Do not loop-restart if exec path missing — surface `helper_down` / “run make helper”.

---

### Issue 19 — Panel not scrollable — **CONFIRMED** — ux

Column is unbounded. Friends+Theme+More overflow.

**Fix:** wrap the inner `column` in `Flickable` + `contentHeight: column.implicitHeight`, `clip: true`, max height `min(implicit, screen - 2*gapsOut)`. Keep Escape-to-close.

---

### Issue 20 — raw error codes — **CONFIRMED** — ux

`errorText` maps four codes. Add: `invite_expired`, `forbidden`, `identity_exists`, `rate_limited`, `no_ratchet`, `file_failed` (exists), `helper_down` (exists). Short English + next step.

---

### Issue 21 — Call looks like audio — **CONFIRMED** — ux

**Fix:** tooltip/label “Call (signaling)” / “No audio yet”. Do not implement PCM in this pass.

---

### Issue 22 — lockup not keyboard accessible — **CONFIRMED** — ux

**Fix:** replace lockup `MouseArea` with `Button`/`focusable: true` (or `Keys.onReturnPressed`) + `Accessible.name: "OmaQ repository"`. Composer icon buttons already have `tooltipText`; set `Accessible.name` to the same string. Do not use placeholder as the only name if you add a visible “Message” label — optional, keep compact.

---

### Issue 23 — empty live chat — **CONFIRMED** — ux

**Fix:** if `!demo && lines.count===0`, show caption “No messages yet. Type below.” Hide when history or live lines arrive.

---

### Issue 24 — RSS cap measurement — **CONFIRMED** as docs — docs-overclaim

**Fix STAND:** “Helper RSS, two-home tests, not Quickshell. Product cap 50 MB is the helper budget.” Do not claim UI+helper sum until measured. Optional later: one line in phase8 measuring helper only, labeled honestly.

---

### Issue 25 — verify-4 does not test QML — **CONFIRMED** as docs — docs-overclaim

**Fix STAND/PLAN:** Phase 4 **helper** done; **UI rewrite unverified by `verify-4`**. Keep “cold open float” under Next. Do not mark UI as verify-4 done.

---

### Issue 26 — mutate corpus missing — **FALSE evidence; OVERSTATED process**

**Evidence is false:** `tests/gold/invite/mutate/{bad-escapes,long,nul-like}.txt` **exists in `25bcd05` and `HEAD`**. `test_mutate()` runs those files.

**Still worth doing:** if `opendir` fails, **fail the test**, do not `return`. Do not add a fake corpus.

---

### Issue 27 — omaq_test does not compile omaq.c history event — **CONFIRMED** as docs — docs-overclaim

**Fix:** add `tests/history-ipc.sh` (or extend two-clients): send `history`, assert `"conversation"` in the event. STAND must cite that test, not `omaq_test`.

---

### Issue 28 — live docs vs repo — **PARTIAL / process**

Live plugin dir can hold old STAND/PLAN from an earlier copy. Binary may be current.

**Fix:** sync script copies QML+helper **only** (not stale docs), or copies docs from repo too. STAND in **repo** is the snapshot. After copy, `cmp` helper binary.

---

### Issue 29 — `parseOmarchyNews` dead — **CONFIRMED** — nit

**Fix:** delete `parseOmarchyNews` (and helpers only used by it) from `Model.js`. Confirm no Panel reference (already none).

---

### Issue 30 — surfaces.json vs jsonl — **CONFIRMED** — nit

**Fix:** PLAN says `surfaces.json`; helper uses `surfaces.jsonl`. Change PLAN (and any review notes) to **`surfaces.jsonl`**. Do not rename the file on disk (would break existing state).

---

### Issue 31 — README “open the card” — **CONFIRMED** — nit

**Fix:** README How to chat: “Accept, then open the chat window” (floating). Not “card”.

---

## Already done after the report (do not regress)

- Friends roster under Demo/Theme; click opens that conversation (`6a9503c`).
- Avatars + fallback + online dots.
- Chat Close: no `visible: pinned` fight.
- Friend window titles `OmaQ chat — name`; `float-omaq.sh` matches `^OmaQ chat`.
- History event includes `conversation` in repo helper.
- Recents without hover; lockup click → GitHub; news scrape removed from Panel.
- README “no user search” (`9b52f80`).

---

## Verify before claiming done

```text
make helper test arch
# plus any new IPC scripts
cmp helper/omaq ~/.config/omarchy/plugins/hancore.omaq/helper/omaq
```

Announce live copy. No AUR. No silent shell restart.

Commit message style: short area prefix, noreply author. Push private `HANCORE-linux/OmaQ` only. Update `docs/STAND.md` **after** the feature commit SHA is known (pin commit).
