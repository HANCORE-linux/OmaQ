#!/bin/sh
# Selection, exact-copy, inline reply, and composer scaling stay in one chat page.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-chat-actions-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$root/pages/ChatPage.qml" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
menu_start = text.index("id: messageMenu")
menu_end = text.index("id: smileRow", menu_start)
menu = text[menu_start:menu_end]
actions = [menu.index(f'text: "{label}"') for label in ("Resend", "Copy", "Reply", "Delete")]
if actions != sorted(actions) or menu.count('\n                text: "') != 4:
    raise SystemExit("chat-message-actions: message context menu changed")
required = (
    'objectName: "replyAction"',
    'objectName: "copySelectionAction"',
    'objectName: "emojiMessageText"',
    "function smileLayout(t)",
    "Emoji.splitEmojiLayout",
    "positionToRectangle(",
    'readonly property bool replyable: line.contextId !== "" && !line.deleted',
    "readonly property bool messageReactions: line.replyable && !line.fileMessage",
    "? String(smileSelection.selectedText || \"\")",
    "visible: line.hasTextSelection",
    "onClicked: root.copyText(line.selectedMessageText)",
    "function compactReplyPreview(value)",
    "MessageLayout.compactReplyPreview(value, 120)",
    "function replyPreviewFor(id)",
    "var replyPreview = root.replyPreviewFor(replyId)",
    "function bubbleWidth(value, replyId, hasCode, withReceipt, availableWidth)",
    "MessageLayout.replySizingText(value,",
    "root.bubbleWidth(model.text, model.reply, line.hasCode,",
    "function messageBreakKinds(text, replyId)",
    "root.clipboardSelectionText(label, model.text, model.reply)",
    "visible: line.replyable",
    "onClicked: root.beginReply(line.contextId, line.contextText)",
    "selectByMouse: true",
    "selectByKeyboard: true",
    "persistentSelection: true",
    "font.pixelSize: root.messageTextPx",
)
for value in required:
    if value not in text:
        raise SystemExit(f"chat-message-actions: missing selection/reply guard: {value}")
if text.count("font.pixelSize: root.messageTextPx") < 2:
    raise SystemExit("chat-message-actions: composer and message text do not share the scale")
PY
cp "$root/Emoji.js" "$tmp/Emoji.js"
cp "$root/MessageLayout.js" "$tmp/MessageLayout.js"
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "Emoji.js" as Emoji
import "MessageLayout.js" as MessageLayout

ShellRoot {
  id: shell
  property string quoted: Array(30).join("quoted line\n") + "😀 trailing"
  readonly property string preview: MessageLayout.compactReplyPreview(quoted, 120)
  readonly property real replyBubbleWidth: bubbleWidth("OK", preview, 360)

  function bubbleWidth(value, replyPreview, availableWidth) {
    var sourceLines = MessageLayout.replySizingText(value, replyPreview).split("\n")
    var longest = 0
    for (var i = 0; i < sourceLines.length; i++)
      longest = Math.max(longest, sourceLines[i].length)
    var estimated = longest * 20 * 0.72 + 16
    return Math.min(Math.max(52, estimated), availableWidth * 0.82)
  }

  function codePointCount(value) {
    var source = String(value || "")
    var offset = 0
    var count = 0
    while (offset < source.length) {
      var codePoint = source.codePointAt(offset)
      offset += codePoint > 0xffff ? 2 : 1
      count++
    }
    return count
  }
  TextEdit {
    id: message
    text: "select<br/>exact"
    textFormat: TextEdit.RichText
    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    persistentSelection: true
  }
  TextEdit {
    id: emoji
    text: "  👨‍👩‍👧‍👦\n👍🏽  "
    textFormat: TextEdit.PlainText
    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    persistentSelection: true
  }
  TextEdit {
    id: directReply
    width: shell.replyBubbleWidth - 16
    text: "<b>↩ " + shell.preview + "</b><br/>OK"
    textFormat: TextEdit.RichText
    readOnly: true
    font.pixelSize: 20
    wrapMode: TextEdit.Wrap
  }
  TextEdit {
    id: groupReply
    width: shell.replyBubbleWidth - 16
    text: "<b>↩ " + shell.preview + "</b><br/>OK"
    textFormat: TextEdit.RichText
    readOnly: true
    font.pixelSize: 20
    wrapMode: TextEdit.Wrap
  }
  Timer {
    interval: 20
    running: true
    onTriggered: {
      message.selectAll()
      emoji.selectAll()
      directReply.selectAll()
      groupReply.selectAll()
      var normalized = message.selectedText.replace(/[\u2028\u2029]/g, "\n")
      var emojiNormalized = emoji.selectedText.replace(/[\u2028\u2029]/g, "\n")
      var directSelection = directReply.selectedText.replace(/[\u2028\u2029]/g, "\n")
      var groupSelection = groupReply.selectedText.replace(/[\u2028\u2029]/g, "\n")
      var layout = Emoji.splitEmojiLayout(emoji.text)
      Quickshell.clipboardText = normalized
      var mapped = layout.length === 2
      for (var i = 0; mapped && i < layout.length; i++)
        mapped = emoji.text.slice(layout[i].start, layout[i].end) === layout[i].glyph
      var boundaryPrefix = Array(120).join("a")
      var boundaryPreview = MessageLayout.compactReplyPreview(
        boundaryPrefix + "😀tail", 120)
      var skinPreview = MessageLayout.compactReplyPreview(
        boundaryPrefix + "👍🏽tail", 120)
      var flagPreview = MessageLayout.compactReplyPreview(
        boundaryPrefix + "🇩🇪tail", 120)
      var familyPreview = MessageLayout.compactReplyPreview(
        boundaryPrefix + "👨‍👩‍👧‍👦tail", 120)
      var combiningPreview = MessageLayout.compactReplyPreview(
        boundaryPrefix + "étail", 120)
      var replyExpected = "↩ " + shell.preview + "\nOK"
      var compact = shell.preview.indexOf("\n") === -1 &&
        shell.preview.indexOf("\r") === -1 && shell.preview.endsWith("…") &&
        shell.codePointCount(shell.preview) === 121 &&
        boundaryPreview === boundaryPrefix + "😀…" &&
        skinPreview === boundaryPrefix + "👍🏽…" &&
        flagPreview === boundaryPrefix + "🇩🇪…" &&
        familyPreview === boundaryPrefix + "👨‍👩‍👧‍👦…" &&
        combiningPreview === boundaryPrefix + "é…"
      var boundedReplies = shell.replyBubbleWidth > 250 &&
        shell.replyBubbleWidth <= 360 * 0.82 && directReply.implicitHeight < 200 &&
        groupReply.implicitHeight < 200 && directSelection === replyExpected &&
        groupSelection === replyExpected
      var ok = normalized === "select\nexact" &&
        Quickshell.clipboardText === "select\nexact" && message.selectByMouse &&
        message.selectByKeyboard && message.persistentSelection && mapped && compact &&
        boundedReplies && emojiNormalized === "  👨‍👩‍👧‍👦\n👍🏽  "
      console.log(ok ? "OMAQ_CHAT_ACTIONS_OK" : "OMAQ_CHAT_ACTIONS_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 5 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
  cat "$out" >&2
  echo "chat-message-actions: Quickshell fixture failed" >&2
  exit 1
fi
if ! grep -q 'OMAQ_CHAT_ACTIONS_OK' "$out"; then
  cat "$out" >&2
  echo "chat-message-actions: exact text selection/copy failed" >&2
  exit 1
fi
echo "chat-message-actions: ok"
