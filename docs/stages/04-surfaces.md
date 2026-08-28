# Phase 4 — how mail appears

**Status:** done. Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- Manifest schema: badge, right panel, desktop notify, `surfaceMode`, sound, theme, unread motion
- `surface.c` owns `$OMAQ_STATE/surfaces.jsonl` (`surface.set` / `surface.get`). Protocol 11 persists Direct entries only as canonical `d:<public-key>` IDs; ambiguous numeric records are privately archived and discarded before restore.
- `SurfaceCoordinator.qml` selects one process-wide owner for chat, demo, notification, and rule-watcher surfaces across monitor instances
- `ChatSurface.qml`: overlay cards (drag + pin), `FloatingWindow` pin (stock Hyprland keys), unpin; every first mapping floats without a window animation, while focus, reopen, and config reload preserve manual tiling. Direct cards and Auto-open preferences retain the expected stable key so a reused friend number cannot reactivate another contact's window.
- `Panel.qml`: only the visible card is in the Wayland input mask; Escape, the OmaQ bar action, or click-away closes it without a redundant header Close action or a desktop-sized pointer catcher
- `pages/ChatPage.qml` shared by card, pin, and right dock
- Themes: System, Paper, Ink, Moss, Dusk, Ember
- Sounds: off, click, pop, bell, soft, knock, custom (`paplay`); desktop notify via `notify-send`
- Unread motion is a border pulse on the open card
- OmaQ does not bind `SUPER+T` or `SUPER+SHIFT+arrows`
- `make verify-4`; `.phase` is 4

## How to check

```text
make verify-4
```

## Stays out

- No live plugin copy
- Export/import and search (phase 5)
