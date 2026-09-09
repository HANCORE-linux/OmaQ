#!/bin/sh
# Actual Qt wheel/key/pointer events, real delegates, and a private offscreen window.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-chat-input-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/pages" "$tmp/home" "$tmp/config" "$tmp/state" "$tmp/data" "$tmp/cache" "$tmp/bin"
mkdir -m 700 "$tmp/runtime"
cp "$root/pages/ChatPage.qml" "$tmp/pages/ChatPage.qml"
for path in assets scripts CallTone.qml Emoji.js MessageLayout.js SurfaceCoordinator.qml SafeText.qml MaterialSymbols.qml qmldir; do
  ln -s "$root/$path" "$tmp/$path"
done
ln -s /usr/share/omarchy/shell/Ui "$tmp/Ui"
ln -s /usr/share/omarchy/shell/Commons "$tmp/Commons"
# The real composer MIME probe may only see this inert fake, never the host clipboard.
cat >"$tmp/bin/wl-paste" <<'SH'
#!/bin/sh
[ "$#" -eq 1 ] && [ "$1" = --list-types ] || exit 1
printf 'text/plain\n'
SH
chmod 700 "$tmp/bin/wl-paste"
python3 - "$tmp/pages/ChatPage.qml" "$tmp/signals" <<'PY'
from pathlib import Path
import re
import sys
path = Path(sys.argv[1])
text = path.read_text()
replacements = {
    '  id: root\n': '''  id: root
  property alias testInput: input
  property alias testInputBox: inputBox
  property alias testInputFlick: inputFlick
  property alias testComposerMenu: composerMenu
  property alias testList: list
  property alias testLines: lines
''',
    '    id: formatButton\n': '''    id: formatButton
    property alias testGlyph: formatTestGlyph
''',
    '      text: OmaQ.MaterialSymbols.glyph(formatButton.materialIcon)\n': '''      id: formatTestGlyph
      text: OmaQ.MaterialSymbols.glyph(formatButton.materialIcon)
''',
    '          id: line\n': '''          id: line
          property alias testPicker: reactionPicker
          property alias testMore: moreReactionAction
          property alias testChoices: reactionPickerRepeater
          property alias testPrevious: previousReactionPage
          property alias testNext: nextReactionPage
''',
}
for needle, replacement in replacements.items():
    assert text.count(needle) == 1, needle
    text = text.replace(needle, replacement)
path.write_text(text)
# Declare inert signals for Connections; no real Service or helper is loaded.
names = set(re.findall(r'function on([A-Z]\w*)\(', text))
names -= {'GroupsTickChanged', 'FriendsTickChanged', 'TypingTickChanged',
          'HelperCompatibilityChanged'}
Path(sys.argv[2]).write_text('\n'.join(
    '    signal ' + name[0].lower() + name[1:] + '()' for name in sorted(names)))
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import QtTest
import Quickshell
import qs.Commons
import "pages" as Pages

ShellRoot {
  QtObject {
    id: fake
    // SIGNALS
    property bool supportsAttachments: true
    property bool supportsGroupAttachments: true
    property bool supportsGroupTyping: true
    property string connectionState: "online"
    property string helperCompatibility: "compatible"
    property int groupsTick: 0
    property int friendsTick: 0
    property int typingTick: 0
    property int sent: 0
    property var lastSend: ({})
    property int reacted: 0
    property var lastReaction: ({})
    function directBindingMatches(conv, key) { return true }
    function requestHistory(conv, key) { return true }
    function filePathFor(conv) { return "" }
    function fileNameFor(conv) { return "" }
    function fileNotice(conv) { return ({}) }
    function filePending(conv) { return false }
    function fileSendingFor(conv) { return false }
    function outgoingFile(conv) { return ({}) }
    function groupMembers(conv) { return [] }
    function groupInviteCandidateMatches(conv, friend, key) { return false }
    function groupInviteCandidates(conv) { return [] }
    function groupTypingActors(conv) { return [] }
    function isPeerTyping(conv) { return false }
    function unreadFor(conv) { return 0 }
    function setTyping(conv, typing, key) { return true }
    function sendConversationOp(op, key, queue) {
      sent++
      lastSend = { op: op, key: key, queue: queue }
      return true
    }
    function reactMessage(conv, id, emoji, key) {
      reacted++
      lastReaction = { conversation: conv, id: id, emoji: emoji, key: key }
      return true
    }
  }
  Window {
    id: window
    width: 420
    height: 620
    visible: true
    Item {
      id: fixture
      anchors.fill: parent
      TestEvent { id: events }
      property var steps: []
      property int step: 0
      property int pass: 0
      property var line: null
      property real scrollBefore: 0
      property real historyBefore: 0
      property real pickerWidth: 0
      property string choiceSnapshot: ""
      property string chosen: ""
      property var seen: []
      readonly property string longText: Array(35).join("Long input with emoji 🙂 and wrapping words\n")
      readonly property string pasted: "Clipboard 🙂\n" + longText
      Pages.ChatPage {
        id: page
        anchors.fill: parent
        service: fake
        conversation: "7"
        peerKey: "a".repeat(64)
        peerName: "Fixture"
        theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
      }
      function check(value, message) {
        if (!value)
          throw new Error((page.groupConversation ? "group" : "direct") +
            " step " + step + ": " + message)
      }
      function key(code, modifiers) {
        check(events.keyClick(code, modifiers || Qt.NoModifier, 0), "key injection")
      }
      function click(item) {
        check(item && item.visible && item.enabled && item.width > 0, "click target unavailable")
        check(events.mouseClick(item, item.width / 2, item.height / 2,
          Qt.LeftButton, Qt.NoModifier, 0), "pointer injection")
      }
      function wheel(item, delta, horizontal) {
        check(events.mouseWheel(item, item.width / 2, item.height / 2,
          Qt.NoButton, Qt.NoModifier, horizontal ? delta : 0,
          horizontal ? 0 : delta, 0), "wheel injection")
      }
      function caretVisible() {
        var input = page.testInput
        var caret = input.cursorRectangle
        var point = input.mapToItem(page.testInputBox, caret.x, caret.y)
        check(caret.height > 0 && point.x >= -1 && point.y >= -1 &&
          point.x + caret.width <= page.testInputBox.width + 1 &&
          point.y + caret.height <= page.testInputBox.height + 1,
          "caret outside editor viewport")
        check(page.testInputBox.height >= Style.space(30) - 1 &&
          page.testInputBox.height <= Style.space(84) + 1, "composer height escaped cap")
        check(input.activeFocus, "composer lost focus")
      }
      function checkPageButtons() {
        ;[line.testPrevious, line.testNext].forEach(function(button) {
          if (!button.enabled) {
            check(button.opacity > 0 && button.opacity <= 0.4 &&
              String(button.testGlyph.color) === String(button.foreground),
              "disabled reaction arrow remains bright or accent-highlighted")
          } else {
            check(button.opacity === 1 && (!button.hot ||
              String(button.testGlyph.color) === String(button.accent)),
              "enabled reaction arrow did not restore normal paint")
          }
        })
      }
      function rememberPage() {
        checkPageButtons()
        check(line.testPicker.visible && line.testChoices.count > 0 &&
          line.testChoices.count <= 5, "picker not realized or oversized")
        check(line.testPicker.width === pickerWidth, "partial page resized picker")
        for (var i = 0; i < line.testChoices.count; i++) {
          var emoji = line.testChoices.itemAt(i).emojiValue
          check(page.emojiSet.indexOf(emoji) >= 0 && seen.indexOf(emoji) < 0,
            "unsupported or repeated reaction")
          seen.push(emoji)
        }
      }
      function prepare() {
        var input = page.testInput
        steps = [
          function() {
            window.width = 420
            page.messageScale = 1
            page.conversation = pass === 0 ? "7" : "g:" + "b".repeat(64)
            page.groupMembersOpen = false
            window.requestActivate()
            input.forceActiveFocus()
            input.text = longText
            input.cursorPosition = input.length
          },
          function() {
            caretVisible()
            scrollBefore = page.testInputFlick.contentY
            check(scrollBefore > 0, "long input did not scroll")
            wheel(page.testInputBox, 480, false)
          },
          function() {
            check(page.testInputFlick.contentY < scrollBefore, "editor wheel did not scroll")
            check(events.keyClickChar("x", Qt.NoModifier, 0), "typing injection")
          },
          function() {
            check(input.text === longText + "x", "typed character lost")
            caretVisible()
            key(Qt.Key_Home, Qt.ControlModifier)
          },
          function() {
            check(input.cursorPosition === 0, "Ctrl+Home failed")
            caretVisible()
            key(Qt.Key_Return, Qt.ShiftModifier)
          },
          function() {
            check(input.text === "\n" + longText + "x" && fake.sent === pass,
              "modified Enter sent or lost its newline")
            key(Qt.Key_End, Qt.ControlModifier)
          }
        ]
        ;[0.9, 1, 1.1, 1.2, 1.4].forEach(function(scale) {
          steps.push(function() { window.width = 280; page.messageScale = scale })
          steps.push(function() { caretVisible(); window.width = 500 })
          steps.push(function() { caretVisible() })
        })
        steps = steps.concat([
          function() { input.text = "X".repeat(800); input.cursorPosition = input.length },
          function() {
            caretVisible()
            check(page.testInputFlick.contentWidth <= page.testInputBox.width + 1,
              "unbroken text introduced horizontal overflow")
            page.beginEdit("synthetic-edit", longText)
            key(Qt.Key_End, Qt.ControlModifier)
          },
          function() {
            caretVisible()
            page.insertEmoji("🙂")
          },
          function() {
            caretVisible()
            check(input.text === longText + "🙂" && page.editingId === "synthetic-edit",
              "editing/emoji insertion changed bytes or mode")
            page.clearEdit()
          },
          function() { input.text = "short"; input.cursorPosition = input.length },
          function() {
            caretVisible()
            check(Math.abs(page.testInputFlick.contentY) < 1, "short text retained scroll offset")
            var start = input.positionToRectangle(0)
            var end = input.positionToRectangle(input.length)
            var first = input.mapToItem(page.testInputBox, start.x, start.y + start.height / 2)
            var last = input.mapToItem(page.testInputBox, end.x, end.y + end.height / 2)
            check(events.mousePress(page.testInputBox, first.x, first.y,
              Qt.LeftButton, Qt.NoModifier, 0), "selection press")
            check(events.mouseMove(page.testInputBox, last.x, last.y,
              0, Qt.LeftButton, Qt.NoModifier), "selection drag")
            check(events.mouseRelease(page.testInputBox, last.x, last.y,
              Qt.LeftButton, Qt.NoModifier, 0), "selection release")
          },
          function() {
            check(input.selectedText === "short", "pointer selection changed")
            key(Qt.Key_A, Qt.ControlModifier)
            key(Qt.Key_C, Qt.ControlModifier)
          },
          function() {
            check(Quickshell.clipboardText === "short" && input.selectedText === "short",
              "select/copy changed")
            Quickshell.clipboardText = pasted
            // Exercise the real plain-text paste branch without a host helper or clipboard.
            fake.supportsGroupAttachments = false
            fake.connectionState = "reconnecting"
            key(Qt.Key_V, Qt.ControlModifier)
          },
          function() {
            check(input.text === pasted, "long paste/replacement changed bytes")
            caretVisible()
            fake.supportsGroupAttachments = true
            fake.connectionState = "online"
            check(events.mouseClick(page.testInputBox, 20, 20,
              Qt.RightButton, Qt.NoModifier, 0), "context-menu injection")
          },
          function() {
            check(page.testComposerMenu.visible && page.testComposerMenu.width >= Style.space(200),
              "OmaQ context menu unavailable")
            key(Qt.Key_Escape)
          },
          function() { input.forceActiveFocus(); key(Qt.Key_Return) },
          function() {
            check(fake.sent === pass + 1 && fake.lastSend.op.text === pasted &&
              fake.lastSend.op.conversation === page.conversation &&
              fake.lastSend.key === page.peerKey && fake.lastSend.queue === false &&
              input.text === "", "Enter send/reset or correlation changed")
            caretVisible()
            check(Math.abs(page.testInputFlick.contentY) < 1, "send retained scroll offset")
            window.width = 280
            page.messageScale = 1
            page.testLines.clear()
            // Keep enough history to detect wheel leakage behind the popup.
            for (var i = 0; i < 35; i++)
              page.appendLine({ id: "message-" + i, dir: "in", text: "Fixture message " + i,
                reactionMe: i === 34 ? "💯" : page.emojiSet[i % 5], ts: 1700000000 })
            page.testList.positionViewAtEnd()
          },
          function() {
            line = page.testList.itemAtIndex(34)
            check(line !== null, "real message delegate unavailable")
            line.forceActiveFocus()
          },
          function() {
            click(line.testMore)
            // Retained cursor state must not make a disabled control look active.
            line.testPrevious.hasCursor = true
            line.testNext.hasCursor = true
          },
          function() {
            check(line.testPicker.visible && line.testPicker.pageIndex === 0 &&
              !line.testPrevious.enabled && line.testNext.enabled, "initial page/arrow state")
            check(line.reactionChoices.length === page.emojiSet.length &&
              JSON.stringify(line.reactionChoices.slice(0, 5)) ===
                JSON.stringify(page.reactionChoicesFor("💯", 5)), "first-page preference changed")
            check(line.testChoices.itemAt(4).selected, "selected reaction not on first page")
            pickerWidth = line.testPicker.width
            var point = line.testPicker.contentItem.mapToItem(fixture, 0, 0)
            check(point.x >= 0 && point.x + pickerWidth - 8 <= window.width,
              "picker escapes narrow window")
            choiceSnapshot = JSON.stringify(line.reactionChoices)
            historyBefore = page.testList.contentY
            seen = []
            rememberPage()
            wheel(line.testChoices.itemAt(0), 120, false)
          },
          function() {
            check(line.testPicker.pageIndex === 0 && page.testList.contentY === historyBefore,
              "first-page wheel escaped into history")
            // High-resolution mouse deltas accumulate; they must not skip a page per event.
            check(events.mouseWheel(line.testChoices.itemAt(0), 15, 15,
              Qt.NoButton, Qt.NoModifier, -60, -60, 0), "diagonal wheel injection")
          },
          function() {
            check(line.testPicker.pageIndex === 0, "partial/diagonal wheel advanced early")
            wheel(line.testChoices.itemAt(0), -60, false)
          },
          function() {
            check(line.testPicker.pageIndex === 1, "wheel did not reach next reactions")
            rememberPage()
            // A live peer reaction must not reorder an already open picker.
            page.testLines.setProperty(34, "reactionPeer", "👀")
            click(line.testNext)
          },
          function() {
            check(line.testPicker.pageIndex === 2 && JSON.stringify(line.reactionChoices) ===
              choiceSnapshot, "arrow or open snapshot changed")
            rememberPage()
            key(Qt.Key_Right)
          },
          function() {
            check(line.testPicker.pageIndex === 3, "keyboard paging failed")
            rememberPage()
            wheel(line.testChoices.itemAt(0), -120, true)
          },
          function() {
            check(line.testPicker.pageIndex === line.testPicker.pageCount - 1 &&
              !line.testNext.enabled && line.testPrevious.enabled, "last-page arrow state")
            rememberPage()
            check(seen.length === page.emojiSet.length && page.emojiSet.every(function(emoji) {
              return seen.indexOf(emoji) >= 0
            }), "not every supported reaction was reachable")
            wheel(line.testChoices.itemAt(0), -120, false)
          },
          function() {
            check(line.testPicker.pageIndex === 4 && page.testList.contentY === historyBefore,
              "last-page wheel escaped into history")
            click(line.testPrevious)
          },
          function() {
            check(line.testPicker.pageIndex === 3, "previous arrow failed")
            checkPageButtons()
            chosen = line.testChoices.itemAt(1).emojiValue
            click(line.testChoices.itemAt(1))
          },
          function() {
            check(fake.reacted === pass * 2 + 1 && fake.lastReaction.emoji === chosen &&
              fake.lastReaction.id === "message-34" &&
              fake.lastReaction.conversation === page.conversation &&
              fake.lastReaction.key === page.peerKey && line.reactionMe === "💯" &&
              !line.testPicker.visible, "reaction changed helper-authoritative state or target")
            check(line.testMore.activeFocus, "closing picker lost trigger focus")
            click(line.testMore)
          },
          function() {
            check(line.testPicker.pageIndex === 0 && line.testPicker.wheelRemainder === 0,
              "reopening retained pagination")
            click(line.testChoices.itemAt(4))
          },
          function() {
            check(fake.reacted === pass * 2 + 2 && fake.lastReaction.emoji === "" &&
              line.reactionMe === "💯", "selected reaction did not request helper removal")
            click(line.testMore)
          },
          function() { key(Qt.Key_Escape) },
          function() {
            check(!line.testPicker.visible && fake.reacted === pass * 2 + 2,
              "Escape changed a reaction or failed to close")
            click(line.testMore)
          },
          function() { click(page.testInputBox) },
          function() {
            check(!line.testPicker.visible && fake.reacted === pass * 2 + 2,
              "outside click changed a reaction or failed to close")
            line.forceActiveFocus()
          },
          function() { click(line.testMore) },
          function() {
            // Removal destroys the owning delegate, including deferred focus work.
            click(line.testNext)
            page.testLines.clear()
            line = null
          },
          function() {
            check(page.testLines.count === 0 && fake.reacted === pass * 2 + 2,
              "delegate removal sent a stale reaction")
          }
        ])
      }
      Timer {
        interval: 25
        repeat: true
        running: true
        property int settle: 3
        onTriggered: {
          // Real window polish/event-loop turns separate mutations from assertions.
          if (settle-- > 0)
            return
          settle = 3
          try {
            if (fixture.steps.length === 0)
              fixture.prepare()
            fixture.steps[fixture.step]()
            fixture.step++
            if (fixture.step === fixture.steps.length) {
              if (fixture.pass === 0) {
                fixture.pass = 1
                fixture.step = 0
                fixture.prepare()
              } else {
                console.log("OMAQ_INPUT_SCROLL_OK direct/group editor and all reaction pages")
                Qt.quit()
              }
            }
          } catch (error) {
            console.error("OMAQ_INPUT_SCROLL_FAIL " + error)
            Qt.quit()
          }
        }
      }
    }
  }
}
QML
python3 - "$tmp/shell.qml" "$tmp/signals" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
path.write_text(path.read_text().replace('// SIGNALS', Path(sys.argv[2]).read_text()))
PY
out="$tmp/out"
if ! env -u WAYLAND_DISPLAY -u DISPLAY -u BASH_ENV HOME="$tmp/home" \
  PATH="$tmp/bin:$PATH" XDG_CONFIG_HOME="$tmp/config" XDG_STATE_HOME="$tmp/state" \
  XDG_DATA_HOME="$tmp/data" XDG_CACHE_HOME="$tmp/cache" XDG_RUNTIME_DIR="$tmp/runtime" \
  PIPEWIRE_RUNTIME_DIR="$tmp/runtime" PULSE_SERVER="unix:$tmp/runtime/no-pulse" \
  DBUS_SESSION_BUS_ADDRESS="unix:path=$tmp/runtime/no-bus" QT_QPA_PLATFORM=offscreen \
  QT_QPA_PLATFORMTHEME=generic QT_QUICK_CONTROLS_STYLE=Basic \
  timeout 25 quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1; then
  cat "$out" >&2
  exit 1
fi
if ! grep -q 'OMAQ_INPUT_SCROLL_OK' "$out" ||
  grep -Eq 'OMAQ_INPUT_SCROLL_FAIL|Binding loop|ReferenceError|TypeError|Unable to assign' "$out"; then
  cat "$out" >&2
  exit 1
fi
printf 'chat-input-scroll: ok\n'
