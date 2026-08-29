#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-emoji-parity-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cp "$root/Emoji.js" "$tmp/Emoji.js"
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "Emoji.js" as Emoji

ShellRoot {
  Timer {
    interval: 1
    running: true
    onTriggered: {
      var positives = [
        "🥳", "🫠", "👩🏽‍💻", "👨‍👩‍👧‍👦", "🇩🇪", "1️⃣",
        "  🥳\n🫠  ", "❤️", "✅"
      ]
      var negatives = [
        "hello🥳", "🥳!", "plain text", "1", "#", "⌀", "⌘", "⬀", "🀀",
        "©", "©🏽", String.fromCodePoint(0x1fa54), "😀‍A", "😀‍1️",
        "1️⃣‍😀", "🇩🇪‍😀",
        String.fromCodePoint(0x1f600, 0xe0061, 0xe007f),
        String.fromCharCode(0xd83d)
      ]
      var ok = true
      for (var i = 0; i < positives.length; i++)
        if (Emoji.splitEmojiOnly(positives[i]).length === 0)
          ok = false
      for (var j = 0; j < negatives.length; j++)
        if (Emoji.splitEmojiOnly(negatives[j]).length !== 0)
          ok = false
      var spaced = "  🥳\n🫠  "
      var layout = Emoji.splitEmojiLayout(spaced)
      if (layout.length !== 2 || layout[0].start !== 2 ||
          layout[0].glyph !== "🥳" || layout[1].glyph !== "🫠" ||
          spaced.slice(layout[1].start, layout[1].end) !== layout[1].glyph)
        ok = false
      console.log(ok ? "OMAQ_EMOJI_PARITY_OK" : "OMAQ_EMOJI_PARITY_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 8 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
	cat "$out" >&2
	echo "emoji-parity: Quickshell fixture failed" >&2
	exit 1
fi
if ! grep -q 'OMAQ_EMOJI_PARITY_OK' "$out"; then
	cat "$out" >&2
	echo "emoji-parity: Unicode emoji sequence classification failed" >&2
	exit 1
fi
echo "emoji-parity: ok"
