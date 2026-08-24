# Repository Guidelines

These rules apply to OmaQ and are the default context for future work. Follow a direct maintainer instruction when it conflicts with this file. Communicate with the maintainer in German unless another language is requested. Keep source code, UI copy, comments, and public documentation in English.

## Product intent

OmaQ is a minimal, invite-only Omarchy chat plugin. Preserve these qualities:

- quiet, technical, compact, and trustworthy
- Signal Double Ratchet for direct messages; never fall back to plaintext
- helper authoritative for transport, protocol, persistence, permissions, and domain rules
- Quickshell/QML responsible for presentation and interaction only
- no accounts, telemetry, third-party backend, or unnecessary dependencies
- private identities, Tox saves, Ratchet state, avatars, and local history never enter Git or machine-to-machine source synchronization

## Sources of truth

- `Panel.qml`: bar widget, panel controls, panel state, profile, friends, badges, invite and settings surfaces
- `ChatSurface.qml`: floating/pinned chat windows, demo window, notifications, sounds, per-conversation surface behavior
- `pages/ChatPage.qml`: chat header, history, message bubbles, composer, formatting tools, context menus
- `Service.qml`: QML client state and helper IPC; it must not become a second protocol implementation
- `Model.js`: theme and presentation model helpers
- `helper/`: C protocol, Tox, Ratchet, storage, avatar, invite, group, and validation logic
- `tests/`: focused C/unit, build, protocol, and two-instance integration checks
- `docs/PLAN.md` and `docs/CURRENT.md`: living product and implementation contracts; update them when durable behavior changes
- `manifest.json`: plugin metadata and static settings schema

Do not manually edit generated binaries as source. Build `helper/omaq` with the repository Makefile. Do not copy `~/.local/share/omaq/tox.save`, `ratchet/`, identity files, or local histories to another machine.

## Language and copy

- Conversation with the maintainer: German by default.
- Code, comments, documentation, helper events, and UI labels: English.
- UI labels are short and functional. Use established wording rather than synonyms.
- Messages containing only emoji render as an intentionally large, high-quality emoji row. Keep normal text size for mixed text/emoji messages and retain a readable fallback when an emoji asset is unavailable.
- The per-contact floating-window toggle is labelled `Auto-off` while enabled and `Auto-open` while disabled.
- Sound control is labelled `Mute` / `Unmute` and means notification sound only.
- Destructive confirmations must state the affected scope, e.g. `Clear this chat?`.

## OmaQ visual system

Reuse existing `qs.Ui`, `qs.Commons`, `Style`, `BorderSurface`, `Button`, `PanelSectionHeader`, and project token components. Do not introduce ad-hoc CSS-like palettes or unrelated control libraries.

- Use Shibumi `bar.visualTokens` when available; retain normal Omarchy fallbacks.
- Use `Material Symbols Rounded` for new semantic icons. Do not use emoji or arbitrary glyphs for new controls.
- Friend names in a chat header use palette `color03`.
- `online`, `typing…`, and `offline` remain directly beside the friend name on the same title line and use palette `color02`.
- Do not place the status at the far edge as a separate title block. Preserve the adjacent name/status relationship when changing the header.
- Keep controls aligned to the existing window edges and token spacing.
- In a floating chat window the order is: call controls, spacer, the `Auto-off`/`Auto-open` action, `Mute`, `Close`.
- In the Demo window, `Mute` is immediately left of `Close`.
- In the bar panel, the global `Mute` action is below the `Demo` action. It must show its current `Mute`/`Unmute` state.
- The per-contact `Auto-open` action appears once in the floating chat title row. Do not duplicate it beside the Clear/Delete action in the page header.
- The Clear chat action is a right-aligned `delete` icon. It requires an explicit confirmation and affects only the current conversation.

## Composer and context-menu UX

- The formatting toolbar above the composer is horizontal and never wraps into a second row.
- It starts exactly at the left edge of the message input, not at the left edge of the whole composer row.
- If tools do not fit, keep them in a horizontal scroller and show a right `chevron_right`; after scrolling, show a left `chevron_left`.
- The composer right-click menu must be OmaQ-styled, not the native platform menu. It must render a fixed usable width, visible labels, Material Symbols Rounded icons, token colors, project font, hover/focus states, and enabled/disabled states.
- A context menu must never degrade to an empty panel or a narrow icon-only strip.
- The message-bubble context menu follows the same styling rules.
- A received file path is clickable. Its menu offers `Open containing folder` and `Copy full path`; use the actual stored path, not a guessed state path.
- Copy/cut/paste/select actions must provide visible feedback where applicable and retain keyboard accessibility.
- The file chooser row closes after starting a send and when an incoming or completed transfer is handled. The file icon can reopen it.

## Panel and notification UX

- The panel shows unread counts on the relevant friend avatar and on the main badge when `notifyBadge` is enabled.
- Reading/opening a conversation clears that conversation's unread count; other conversations remain unread.
- Avatar badges must not replace the online/offline status dot.
- Avatar images use safe local file URLs, a `person` Material fallback, and a revision/cache refresh when a file is replaced.
- Self and friend avatars are transferred only through the helper's validated avatar protocol. Never fake a friend avatar from a local identity.
- Panel buttons such as Invite, Add, Chat, Theme, More, safety display, and confirmations are transient UI state. Reset them when the panel closes so reopening does not leave stale `selected` states.
- The hero action grid uses stable per-column widths. Changing a label such as `Mute` ↔ `Unmute` must not move or resize neighboring buttons.
- Current functional state such as global Mute may remain selected after reopening; that is not a stale transient selection.
- Every destructive or privacy-impacting action requires an explicit confirmation immediately before execution, including contact removal, personal-ID/nospam rotation, clearing chat history, and equivalent future actions.
- Safety codes are opt-in display information for identity comparison, not setup secrets. Keep them hidden until requested; copying must show confirmation.

## Auto-open and sound behavior

- `Auto-open` is per conversation/user and persisted locally under OmaQ state. Default is enabled unless the user disables it.
- Disabled Auto-open means no floating chat window and no right notification panel for that conversation; the unread badge and notification sound remain.
- Do not use the bell icon for the Auto-open control; use a floating/open semantic icon. Bell icons are reserved for sound mute controls.
- Global Mute is Service-backed so every ChatSurface observes the same state. It suppresses notification sound but does not clear unread badges or delete messages.
- Demo Mute and chat-window Mute use the same global state.
- Load per-user preferences before acting on queued incoming messages. Handle missing, unwritable, and failed preference files visibly and safely.

## Networking and security

- Direct messages stay Signal-Ratchet encrypted and fail closed when no session exists. Never send or accept plaintext as a fallback.
- Tox carries transport; local discovery, UDP, and hole punching may be enabled, with relays as fallback. An `online` state does not guarantee a direct LAN path; do not claim LAN latency is solved without measuring both directions.
- Keep helper protocol and domain validation in C. Validate canonical decimal friend IDs and bounded conversation IDs before conversion.
- Incoming files are paused until acceptance and default to `~/Downloads/omaq/`; explicit destination overrides remain supported. Never use `$OMAQ_HOME` as the user-facing default download directory.
- Local history clear must remove only the requested conversation and its rotated history; never clear all conversations.
- Escape all JSON fields and reject path traversal, invalid IDs, unsupported actions, and malformed payloads.
- Helper event fan-out must not block Tox iteration on a stale or slow IPC client. Prefer non-blocking client sockets and drop clients that cannot accept an event; persisted history remains the recovery source.
- Never commit credentials, private keys, Tox saves, Ratchet state, local chat history, temporary screenshots, or downloaded audit data.

## Change workflow

1. Run `git status --short --branch` before editing.
2. Read the relevant source, tests, and nearby patterns.
3. Make the smallest coherent change; preserve unrelated dirty changes.
4. For UI changes, inspect affected states and screenshots when available. Check alignment, empty, loading, disabled, hover, focus, error, and overflow states.
5. Run relevant tests and report limitations honestly.
6. Request the `code-reviewer` subagent after implementation and before delivery.
7. Do not silently restart or silently synchronize the live plugin. Announce live synchronization and list source/destination paths and files first.

## Delivery approvals

Treat every delivery phase as a separate authorization boundary. Approval for one phase never authorizes a later phase. A bundled request still requires confirmation immediately before each phase.

### Branch or worktree preparation

Show the intended branch and worktree path. Obtain explicit approval before creating, deleting, resetting, rebasing, or otherwise mutating a branch or worktree.

### Commit

Before committing, report:

- the exact files staged or the staged diff summary
- validation and test results run after the final change
- the proposed English commit subject

Then obtain explicit approval immediately before creating the local commit. Do not treat “finish”, “implement”, or a general delivery request as commit approval when this report has not just been confirmed.

### Push

After a local commit, report:

- exact commit hash(es)
- source branch
- destination remote and branch
- validation status and worktree status

Obtain a new explicit approval immediately before pushing. Never force-push `main`. Fetch and inspect the remote before pushing; do not overwrite unrelated remote work.

### Live synchronization

Live synchronization is a separate delivery phase from commit and push. Report the exact source path, destination path, files, and whether a helper restart/reload is expected. Obtain explicit approval immediately before copying or reloading live files. Never copy private identities, keys, Ratchet state, or local history.

### Pull request and merge

For a pull request, report base, head, title, body summary, and issue-closing keywords. Obtain new explicit approval before creating, editing, commenting on, or mutating the PR.

For a merge, wait for required checks and reviews, report their results and intended merge method, then obtain new explicit approval immediately before merging. PR approval or a general request to finish is not merge authorization.

## Verification

Run the commands relevant to the final change; do not claim a check passed unless it was run after the final edit:

```bash
make test
make helper
make arch
./tests/no-signal-build.sh
./tests/phase2.sh
./tests/phase8.sh
qmllint ChatSurface.qml pages/ChatPage.qml Service.qml
omarchy plugin validate .
git diff --check
```

`qmllint Panel.qml` may exit `255` without diagnostics in the installed environment; report that limitation rather than hiding it. For UI changes, a real Quickshell interaction check remains preferable to lint alone.

Before a push:

```bash
git fetch origin main
git status --short --branch
git rev-list --left-right --count origin/main...main
```

Confirm there is no unintended worktree change and that the final commit hash, remote branch, live synchronization status, and remaining limitations are reported accurately.
