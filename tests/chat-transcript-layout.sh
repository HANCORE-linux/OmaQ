#!/bin/sh
# Message metadata and group code headers stay inside their transcript rows.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-chat-transcript-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/pages"
cp "$root/pages/ChatPage.qml" "$tmp/pages/ChatPage.qml"
ln -s "$root/assets" "$tmp/assets"
ln -s "$root/scripts" "$tmp/scripts"
ln -s "$root/CallTone.qml" "$tmp/CallTone.qml"
ln -s "$root/Emoji.js" "$tmp/Emoji.js"
ln -s "$root/MessageLayout.js" "$tmp/MessageLayout.js"
ln -s "$root/SurfaceCoordinator.qml" "$tmp/SurfaceCoordinator.qml"
ln -s "$root/SafeText.qml" "$tmp/SafeText.qml"
ln -s "$root/qmldir" "$tmp/qmldir"
ln -s /usr/share/omarchy/shell/Ui "$tmp/Ui"
ln -s /usr/share/omarchy/shell/Commons "$tmp/Commons"
python3 - "$tmp/pages/ChatPage.qml" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
root_needle = "  id: root\n"
root_alias = "  property alias testList: list\n"
delegate_needle = "        delegate: FocusScope {\n          id: line\n"
delegate_aliases = """        delegate: FocusScope {
          id: line
          property alias testBubble: bubble
          property alias testSender: groupSenderLabel
          property alias testCodeCopy: codeCopyButton
          property alias testTimestamp: messageTimestamp
          property alias testReceipt: groupReceiptStatus
          property alias testReaction: reactionBadge
"""
if source.count(root_needle) != 1 or source.count(delegate_needle) != 1:
    raise SystemExit("chat-transcript-layout: QML test seam changed")
source = source.replace(root_needle, root_needle + root_alias)
source = source.replace(delegate_needle, delegate_aliases)
path.write_text(source, encoding="utf-8")
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import qs.Commons
import "pages" as Pages

ShellRoot {
  id: shell
  property bool failed: false
  property int phase: 0
  property int attempts: 0
  readonly property string datedTimestamp: "2023-11-14 · 22:13"

  QtObject {
    id: fake
    property bool supportsGroupAttachments: true
    property bool supportsGroupTyping: true
    property bool incomingCall: false
    property string connectionState: "online"
    property string lastCallState: ""
    property string lastCallConv: ""
    property int callDurationSeconds: 0
    property int groupsTick: 0
    property int friendsTick: 0
    property bool friendsReady: true
    property int typingTick: 0
    function directBindingMatches(conversation, key) { return true }
    function requestHistory(conversation, key) { return true }
    function filePathFor(conversation) { return "" }
    function fileNameFor(conversation) { return "" }
    function isPeerTyping(conversation) { return false }
    function filePending(conversation) { return false }
    function groupInviteCandidateMatches(conversation, friendId, key) { return false }
    function groupInviteCandidates(conversation) { return [] }
    function groupTypingActors(conversation) { return [] }
    function groupMembers(conversation) {
      if (conversation !== "g:layout")
        return []
      return [
        { peer: "long-peer", key: "long-peer",
          name: "Alexandria-Cassandra-Bartholomaeus-Konstantin-Nakamura",
          role: "member", online: true },
        { peer: "short-peer", key: "short-peer", name: "Alex",
          role: "member", online: true }
      ]
    }
    function fileNotice(conversation) { return ({}) }
    function fileSendingFor(conversation) { return false }
    function outgoingFile(conversation) { return ({}) }
    function unreadFor(conversation) { return 0 }
  }

  function check(value, message) {
    if (value)
      return
    failed = true
    console.error("OMAQ_TRANSCRIPT_FAIL " + message)
  }

  function overlaps(first, second) {
    if (!first.visible || !second.visible)
      return false
    return first.x < second.x + second.width - 0.5 &&
      first.x + first.width > second.x + 0.5 &&
      first.y < second.y + second.height - 0.5 &&
      first.y + first.height > second.y + 0.5
  }

  function insideLine(item, line) {
    return !item.visible || (item.x >= -0.5 && item.y >= -0.5 &&
      item.x + item.width <= line.width + 0.5 &&
      item.y + item.height <= line.height + 0.5)
  }

  Item {
    Pages.ChatPage {
      id: narrowPage
      width: 280
      height: 620
      service: fake
      demo: false
      messageScale: 1.4
      conversation: "7"
      peerName: "Direct"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }

  Item {
    Pages.ChatPage {
      id: directPage
      width: 420
      height: 620
      service: fake
      demo: false
      conversation: "8"
      peerName: "Direct"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }

  Item {
    Pages.ChatPage {
      id: widePage
      width: 640
      height: 620
      service: fake
      demo: false
      conversation: "9"
      peerName: "Direct"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }

  Item {
    Pages.ChatPage {
      id: groupPage
      width: 420
      height: 620
      service: fake
      demo: false
      conversation: "g:layout"
      peerName: "Group"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }

  Timer {
    interval: 40
    repeat: true
    running: true
    onTriggered: {
      attempts++
      if (phase === 0) {
        narrowPage.appendLine({ id: "narrow-in", dir: "in", text: "x",
          ts: 1700000000, ack: -1 })
        directPage.appendLine({ id: "direct-in", dir: "in", text: "x",
          ts: 1700000000, ack: -1 })
        directPage.appendLine({ id: "direct-out-reaction", dir: "out", text: "x",
          ts: 1700000000, ack: 1, reactionMe: "👍" })
        widePage.appendLine({ id: "wide-in", dir: "in", text: "x",
          ts: 1700000000, ack: -1 })
        widePage.appendLine({ id: "wide-out-reaction", dir: "out", text: "x",
          ts: 1700000000, ack: 1, reactionMe: "👍", reactionPeer: "😂" })
        groupPage.appendLine({ id: "group-out-reaction", dir: "out", text: "x",
          ts: 1700000000, ack: 1, reactionMe: "👍", reactionPeer: "😂",
          groupReactions: [{ actor: "a", emoji: "❤️" },
            { actor: "b", emoji: "🎉" }],
          groupReceipts: [{ actor: "a", state: "read" },
            { actor: "b", state: "delivered" }] })
        groupPage.appendLine({ id: "group-code-long", dir: "in",
          sender: "long-peer", text: "```c\nvalue\n```", ts: 1700000000,
          ack: -1 })
        groupPage.appendLine({ id: "group-code-short", dir: "in",
          sender: "short-peer", text: "```c\nx\n```", ts: 1700000000,
          ack: -1 })
        narrowPage.testList.positionViewAtEnd()
        directPage.testList.positionViewAtEnd()
        widePage.testList.positionViewAtEnd()
        groupPage.testList.positionViewAtEnd()
        phase = 1
        attempts = 0
        return
      }

      var narrow = narrowPage.testList.count > 0
        ? narrowPage.testList.itemAtIndex(narrowPage.testList.count - 1) : null
      var directCount = directPage.testList.count
      var directIncoming = directCount >= 2
        ? directPage.testList.itemAtIndex(directCount - 2) : null
      var directReaction = directCount >= 1
        ? directPage.testList.itemAtIndex(directCount - 1) : null
      var wideCount = widePage.testList.count
      var wideIncoming = wideCount >= 2
        ? widePage.testList.itemAtIndex(wideCount - 2) : null
      var wideReaction = wideCount >= 1
        ? widePage.testList.itemAtIndex(wideCount - 1) : null
      var groupCount = groupPage.testList.count
      var groupReaction = groupCount >= 3
        ? groupPage.testList.itemAtIndex(groupCount - 3) : null
      var longCode = groupCount >= 2
        ? groupPage.testList.itemAtIndex(groupCount - 2) : null
      var shortCode = groupCount >= 1
        ? groupPage.testList.itemAtIndex(groupCount - 1) : null
      if ((!narrow || !directIncoming || !directReaction || !wideIncoming ||
           !wideReaction || !groupReaction || !longCode || !shortCode) &&
          attempts < 80) {
        narrowPage.testList.positionViewAtEnd()
        directPage.testList.positionViewAtEnd()
        widePage.testList.positionViewAtEnd()
        groupPage.testList.positionViewAtEnd()
        return
      }
      if (!narrow || !directIncoming || !directReaction || !wideIncoming ||
          !wideReaction || !groupReaction || !longCode || !shortCode) {
        console.error("OMAQ_TRANSCRIPT_FAIL delegates unavailable")
        Qt.quit()
        return
      }

      check(narrow.testTimestamp.text === datedTimestamp &&
        directIncoming.testTimestamp.text === datedTimestamp &&
        wideIncoming.testTimestamp.text === datedTimestamp &&
        groupReaction.testTimestamp.text === datedTimestamp,
        "timestamp bytes changed")
      check(insideLine(narrow.testTimestamp, narrow),
        "minimum-width incoming timestamp escaped its row")
      check(insideLine(directIncoming.testTimestamp, directIncoming),
        "incoming timestamp escaped its row")
      check(insideLine(directReaction.testTimestamp, directReaction) &&
        insideLine(directReaction.testReaction, directReaction) &&
        !overlaps(directReaction.testTimestamp, directReaction.testReaction),
        "outgoing reaction overlaps or clips its timestamp")
      check(insideLine(wideIncoming.testTimestamp, wideIncoming) &&
        insideLine(wideReaction.testTimestamp, wideReaction) &&
        insideLine(wideReaction.testReaction, wideReaction) &&
        !overlaps(wideReaction.testTimestamp, wideReaction.testReaction),
        "wide message metadata escaped or overlapped")
      check(insideLine(groupReaction.testTimestamp, groupReaction) &&
        insideLine(groupReaction.testReceipt, groupReaction) &&
        insideLine(groupReaction.testReaction, groupReaction) &&
        !overlaps(groupReaction.testTimestamp, groupReaction.testReceipt) &&
        !overlaps(groupReaction.testTimestamp, groupReaction.testReaction) &&
        !overlaps(groupReaction.testReceipt, groupReaction.testReaction),
        "group reaction, receipt, or timestamp metadata overlaps")
      check(longCode.testSender.visible && longCode.testCodeCopy.visible &&
        longCode.testSender.x + longCode.testSender.width <=
          longCode.testCodeCopy.x + 0.5 &&
        longCode.testSender.y + longCode.testSender.height <=
          longCode.testBubble.height + 0.5,
        "long group sender overlaps the code-copy action")
      check(shortCode.testSender.visible && shortCode.testCodeCopy.visible &&
        shortCode.testSender.implicitHeight <= shortCode.testSender.font.pixelSize * 1.6 &&
        shortCode.testSender.x + shortCode.testSender.width <=
          shortCode.testCodeCopy.x + 0.5,
        "short group sender wraps or overlaps unnecessarily")
      if (phase === 1) {
        Style.fontBaseSize = 16
        phase = 2
        attempts = 0
        return
      }
      console.log(failed ? "OMAQ_TRANSCRIPT_RESULT fail" :
        "OMAQ_TRANSCRIPT_RESULT ok")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! TZ=UTC QT_QPA_PLATFORM=offscreen timeout 12 quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1; then
  cat "$out" >&2
  echo "chat-transcript-layout: Quickshell fixture failed" >&2
  exit 1
fi
if grep -Eq 'ReferenceError|TypeError|Binding loop|Cannot anchor|Unable to assign|OMAQ_TRANSCRIPT_FAIL|OMAQ_TRANSCRIPT_RESULT fail' "$out" ||
   ! grep -q 'OMAQ_TRANSCRIPT_RESULT ok' "$out"; then
  cat "$out" >&2
  echo "chat-transcript-layout: transcript assertions failed" >&2
  exit 1
fi
echo "chat-transcript-layout: ok"
