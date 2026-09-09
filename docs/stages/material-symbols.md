# Material Symbols loading and glyphs

Unreleased follow-up for [issue #55](https://github.com/HANCORE-linux/OmaQ/issues/55).

## Behavior

- `MaterialSymbols.qml` registers the existing Arch package font with Qt's `FontLoader`. A shell started before the font was installed no longer depends on its earlier font-family lookup.
- Panel, chat, call, and context-menu icons use explicit Material Symbols codepoints instead of icon-name ligatures. Existing colors, variable axes, labels, tooltips, and actions remain unchanged.
- The former `circle_outline` name maps to `circle` with the existing unfilled state; `remove_reaction` maps to the minus symbol. Neither name exists as a single glyph in the tested package font.
- Unavailable fonts and unknown names show a bounded `?`, not an overlapping word. Empty icons stay empty. The family and glyph bindings follow loader readiness, including failure and recovery.
- External button and bar text keep literal call codepoints. Their PlainText-gate exceptions accept only the exact source-bound readiness expressions; dynamic glyph calls, remote values, alternate paths, and inherited tooltips remain rejected.
- No font is bundled or downloaded, no host API is bypassed, and no shell restart is added. The installer still supplies `ttf-material-symbols-variable`.

## Evidence and limits

An isolated Qt process reproduced the late-install failure: `person` remained six glyphs after Fontconfig refreshed, then became one after application-font registration. This establishes one failure mechanism, not the original reporter's ARM root cause.

`tests/material-symbols.py` checks source coverage; its optional `--font` mode compares every mapping with native HarfBuzz across fill, weight, and optical-size settings. It also disables name-shaping features: names split into letters while codepoints remain single nonmissing glyphs. The source-only check runs in `make test-ci` without requiring a system symbol font.

`tests/material-symbols.sh`, in local `make test`, starts a private offscreen Quickshell with a pre-populated font database that lacks the symbol family. It checks registration in that same engine, all glyph widths with name-shaping features disabled, missing-font fallback, and unknown names. Test-only source changes exercise failure and restoration; the production loader does not watch font-file removal or automatically retry a failed load. Chat and panel fixtures include the shared module.

ARM confirmation of issue #55 and native Wayland acceptance remain open. These changes do not address the separate Omarchy 4.0.3 host-API compatibility work.
