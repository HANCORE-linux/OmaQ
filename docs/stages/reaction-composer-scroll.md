# Reaction paging and composer scrolling

## Behavior

- DirectChat and GroupChat show five reaction choices at a time. The first page keeps the existing usage ranking and includes the message's selected reaction. Arrows, the mouse wheel, and Left/Right keys reach every remaining emoji in the existing set.
- Choices are snapshotted when the popup opens, so incoming reactions cannot reorder a button under the pointer. Reopening resets pagination. The last page keeps the same width. Disabled icon buttons are dimmed and lose their accent highlight, including with retained cursor state. Wheel events stay inside the picker. Diagonal wheel events count once; partial angle deltas accumulate.
- Selection still calls the existing helper-bound reaction operation. Selecting the current reaction requests its removal; the UI does not invent an acknowledged reaction. Escape, outside clicks, and delegate destruction do not send reactions.
- The composer keeps its bounded frame and uses Qt's `TextArea.flickable` attachment for vertical scrolling and cursor visibility. Qt owns content sizing and cursor tracking; the surrounding layout uses the capped viewport height, not the full text height. Text stays PlainText.
- Enter, modified Enter, selection, clipboard handling, formatting, editing, emoji insertion, and attachment paths retain their existing handlers. Helper code, Service, protocol, and message timestamps are unchanged.

## Verification

`tests/chat-input-scroll.sh` runs the real ChatPage in a visible offscreen Qt window with an inert service, private HOME/XDG directories, unavailable private audio/D-Bus endpoints, and a fake clipboard MIME probe. Qt Test injects actual key, pointer, and wheel events; mutations and assertions run on separate event-loop turns.

The fixture covers both chat types: all reaction pages, disabled-arrow paint and re-enabling, narrow-window placement, selected-reaction removal, frozen ordering, focus, dismissal and owner teardown; long wrapped and unbroken input, all five message scales, window resizing, scrolling followed by typing, Home/End, modified Enter, mouse and keyboard selection, copy, reconnect-time plain-text paste without group attachments, editing, emoji insertion, the styled context menu, and exact send/reset behavior. It is part of local `make test`, not headless `make test-ci`.

The original composer failed the isolated cursor-visibility assertion. An early reused-page fixture also produced a `messageLaneWidth` transition warning on the unchanged base. The final fixture separates owner teardown from conversation replacement and rejects binding-loop and QML runtime errors; it does not claim to fix the pre-existing transcript warning.

Focused composer, message-action, transcript-layout, and exact-source PlainText gates pass. Full `make test`, local `make test-ci`, helper hardening, architecture, plugin validation, and ShellCheck also pass. `qmllint` exits 0 with import/type warnings; it is not warning-free. Only the ChatPage source hashes change in the PlainText gate; its policy, reviewed model-mutation allowlist, and adversarial checks remain unchanged.

## Limits

Offscreen input events and synthetic captures are not native Wayland acceptance. Physical mouse/touchpad behavior, pixel-only scroll gestures, IME preedit, themes, and tiled/floating interaction still need native checks. No live plugin update or new release is included.
