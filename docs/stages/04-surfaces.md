# Phase 4 — how mail appears

**Status:** done. Live plugin copy not performed.  
**Date:** 2026-08-18

## Landed

- Manifest schema: badge, right panel, desktop notify, `surfaceMode`, sound, theme, unread motion
- `surface.c` owns `$OMAQ_STATE/surfaces.jsonl` (`surface.set` / `surface.get`)
- `ChatSurface.qml`: overlay cards (drag + pin), `FloatingWindow` pin (stock Hyprland keys), unpin
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
