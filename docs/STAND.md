# Current stand — 2026-08-22

This file is the snapshot for a new session. Product contract (German): [`../../Prompt-Uebergabe/OmaQ.md`](../../Prompt-Uebergabe/OmaQ.md). How we build: [`PLAN.md`](PLAN.md).

**Last pushed commit:** `6a9503c` on `main` (private `HANCORE-linux/OmaQ`).
**`.phase`:** 8  
**Plugin id:** `hancore.omaq`  
**Manifest version:** `0.6.0`  
**Live bar:** **on this machine.** Real copy at `~/.config/omarchy/plugins/hancore.omaq` (not a symlink). Develop only in `/home/hancore/Projects/omaq`, then copy. No silent `omarchy restart shell`.  
**AUR:** registration off — no `verify-7`, no upload.

Chat with the owner is German. Repo, UI, and this file are English.

## Phases

| Phase | What | Verify | Status |
|---|---|---|---|
| 0 Harness | Makefile, gold driver, licenses | `verify-0` | done |
| 1 1:1 chat | one helper, Tox, invite | `verify-1` | done |
| 2 Invite safety | QR, revoke, nospam, safety, rate | `verify-2` | done |
| 3 Groups | owner > admin > member | `verify-3` | done |
| 4 Surfaces | cards, pin, sounds, themes | `verify-4` | done (UI since rewritten) |
| 5 Daily | export/import IPC, search | `verify-5` | done |
| 6 File + 1:1 audio | Tox file 8 MiB, ToxAV audio | `verify-6` | done |
| 7 AUR package | PKGBUILD, namcap, enable path | `verify-7` | **halted** |
| 8 Double Ratchet | Signal payload on 1:1, 50 MB cap | `verify-8` | **done** |

## Live UI

The bar widget does not use Omarchy `Panel` / `KeyboardPanel`. `Panel.qml` is a `BarWidget` plus its own layer-shell popup (Shibumi bar is screen-sized; KeyboardPanel capped the card at 120 px). Demo and Chat share `pages/ChatPage.qml`.

**Panel**

- Hero: transparent trimmed panel render `assets/OmaQ_Final-panel.png` derived from `assets/OmaQ_Final.png`, with a subtle pulse animation. Click opens https://github.com/HANCORE-linux/OmaQ. Omarchy.org news is gone.
- Actions: Invite, Join, Chat, Demo, Theme — icon + label, same size, `Style.cornerRadius`, selected fill while active. Chat opens a contact picker before opening a conversation.
- More exposes the compact daily controls (chat/search, group create/invite/dissolve, group member roles/removal/leave, identity protection/export/import, and danger actions). Contact removal and personal-ID rotation require explicit confirmation.
- Invite is a **toggle** (`inviteOpen`): first click creates/shows QR, second click hides it. Revoke also closes the QR so the next Invite click mints a new token. Safety codes are only shown on demand under More → Chat and can be copied or hidden.
- Theme icon opens the list (not a permanent color bar). Palettes: **System** (live Omarchy `color0`–`color7` from `colors.toml` with `onFileChanged: reload()`, name from `~/.local/state/omarchy/current/theme.name`) then Traffic-Board **gruvbox**, **rose pine**, **everforest**, **gruvbox light**, **catppuccin latte**, **tokyo night light**. Default `chatTheme`: `system`.
- **You** and **Friends** sit under Demo/Theme. Click a friend to open that 1:1. You can set the local Tox nickname and self avatar under **You**; avatars use zenity (`op: avatar.set`, 512 KiB, png/jpeg/webp). Friends send `TOX_FILE_KIND_AVATAR`. The fallback uses the Material Symbols Rounded `person` glyph. Green/gray dot = Tox online/offline (`friend_connection_status`). Names from Tox; empty name → `Friend <id>`.

**Demo / Chat windows**

- Demo and Chat are Quickshell `FloatingWindow` xdg-toplevels (`OmaQ demo` / `OmaQ chat` / `OmaQ chat — <name>`), not layer-shell cards.
- Always start floating, never tiled, never fullscreen. `scripts/float-omaq.sh` matches `^OmaQ demo$` and `^OmaQ chat` (covers friend titles). Lua `window_rule` + `window.float({ action = "on" })`; classic `fullscreenstate` + `setfloating`.
- In-window Close calls `dismissCard` (no `visible: pinned` binding that blocked close). Compositor-close also dismisses. Demo close clears `demoOpen`.

**Chat chrome**

- One composer row: attach, field, emoji, send. Call in the header. Hang up only while in a call. Call/file chrome is scoped to that window’s conversation. Header shows peer name, avatar, online/offline.
- Live send keeps an outgoing line pending until the helper emits the confirmed `message` event after ciphertext/group text is sent. Incoming lines match `lastChatConv`. On open and conversation change, `op: history` seeds the last 50 lines (`conversation` on the event).
- The smile button opens the full emoji set. PNGs from Noto Color Emoji CBDT (`scripts/extract-emoji.py` → `assets/emoji/`). In the field and in bubbles, smiles use `Style.font.body`; emoji recents are not persisted.
- Demo is local only (no Tox). Theme for demo follows the panel palette.

**README**

Tagline is “no account, no phone, no user search” (not “directory”). Body spells out: no signup/email, no phone number, no people list — invite only.

## Logos (`~/omaq-logo/variants/final/`)

| File | Use |
|---|---|
| `ChatGPT Image Aug 21, 2026, 02_56_18 PM.png` | Bar + panel mark → `assets/mark.png` (black punched to alpha, trimmed) |
| `OmaQ_Final.png` | README wordmark → `assets/OmaQ_Final.png` |
| `OmaQ_Final-panel.png` | Transparent trimmed panel hero render |
| `ChatGPT Image Aug 21, 2026, 02_58_45 PM.png` | Legacy panel lockup retained as an asset only |
| (drawn) | Default avatar → Material Symbols Rounded `person` glyph |

## What works (helper, last `verify-8` plus `omaq_test` on 2026-08-22)

- Invite link + QR token, one-use, 24 h, revoke, nospam voids invites
- 1:1 and group text (same chat page)
- Optional `toxencryptsave` lock on `tox.save`
- File send (paused until accept, default dest `~/Downloads/omaq/`; explicit destination overrides remain supported)
- 1:1 call **signaling** (start/answer/stop). No microphone PCM yet.
- Direct messages: Double Ratchet over Tox (`OQB1` / `OQR1`); invite `rk=` and the peer pin are persisted under `$OMAQ_HOME/ratchet/`
- Two local helpers exchange a ratchet plaintext; `make verify-8` was green on 2026-08-19
- `tests/omaq_test`: ok (includes avatar path/id gold)
- Friend list `event:friends` (id, name, avatar path, online). `avatar.set` copies to `$OMAQ_HOME/avatars/self.png` (512 KiB cap) and sends `TOX_FILE_KIND_AVATAR`. Incoming avatars auto-accept to `avatars/<id>.png`.

## Security (honest)

| Protected | Not this product |
|---|---|
| Tox E2E on the wire; relays do not read content | No SimpleX-style missing user id |
| Direct text: Signal Double Ratchet; invite `rk=` plus the token-authenticated peer pin | Groups / files / calls not ratcheted |
| `tox.save` optional passphrase (`toxencryptsave`) | History JSONL is `0600` plaintext |
| Token + rate limit on a leaked QR | One durable Tox address; friends can see IP |
| 50 MB RSS cap, measured | No Tor child (would blow the cap) |

KCI on the Tox handshake does not decrypt ratchet text once both direct-chat identity pins came from the token-authenticated friend-request exchange and are persisted. Direct setup without those pins is refused. That is not “as metadata-private as SimpleX”.

## Memory (measured 2026-08-19)

| Gate | kB |
|---|---|
| Idle (two-home, with ratchet) | 11216 |
| Call peak | 26032 |
| Ratchet 1:1 (`phase8`) | **11324** |
| Product cap | **51200** (50 MB) |

## Packages (Arch extra, owner-approved)

- `toxcore` 1:0.2.22-2
- `libsignal-protocol-c` 2.3.3-2
- `libcrypto` (OpenSSL 3) already on the box, only in `ratchet_adapt.c`

## Constraints still binding

- Do not develop in `~/.config/omarchy/plugins/`
- No silent Omarchy shell restart
- toxcore / libsignal only after explicit package yes (already yes)
- Live copy only after announced yes (this machine is installed)
- Commit + push only in private `HANCORE-linux/OmaQ`
- Verifier subagent only after explicit yes
- Do not bind SUPER+T / SUPER+SHIFT+arrows
- AUR off

## Next — only after an explicit go

1. Confirm Demo/Chat/friend windows spawn floating on a cold open
2. Two real machines over the internet (avatars + presence)
3. Phase 7 AUR when registration is on
4. Later extras (group ratchet, file/call ratchet, Tor) only if RSS still ≤ 50 MB
