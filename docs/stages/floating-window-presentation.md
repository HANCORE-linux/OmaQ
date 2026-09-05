# Floating-window presentation

The per-conversation automatic-opening control is labelled **Pop up: On** or **Pop up: Off**. The visible drag glyph is removed so the compact title row remains quiet.

This is a presentation-only change. The shared DirectChat and GroupChat delegate retains the complete drag item and MouseArea, `startSystemMove()`, geometry observation and persistence, pin and close actions, sizing, placement, and automatic-opening behavior.

A focused source check binds the new label to the existing per-conversation setting and verifies that the invisible drag surface and geometry callback remain. The existing chat-surface geometry regression and `qmllint ChatSurface.qml` remain green. These checks do not replace native Wayland or multi-monitor acceptance. The independent review agent was unavailable because the local pi harness referenced a missing executable; that limitation is not being presented as successful validation.
