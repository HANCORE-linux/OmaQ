#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-chat-composer-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
ln -s "$root/pages" "$tmp/pages"
ln -s "$root/assets" "$tmp/assets"
ln -s "$root/scripts" "$tmp/scripts"
ln -s "$root/CallTone.qml" "$tmp/CallTone.qml"
ln -s "$root/Emoji.js" "$tmp/Emoji.js"
ln -s "$root/MessageLayout.js" "$tmp/MessageLayout.js"
ln -s "$root/SurfaceCoordinator.qml" "$tmp/SurfaceCoordinator.qml"
ln -s "$root/qmldir" "$tmp/qmldir"
ln -s /usr/share/omarchy/shell/Ui "$tmp/Ui"
ln -s /usr/share/omarchy/shell/Commons "$tmp/Commons"
mkdir -m 700 "$tmp/bin"
cat >"$tmp/bin/wl-paste" <<'SH'
#!/bin/sh
[ "${1:-}" = "--list-types" ] || exit 2
printf '%s\n' 'text/plain;charset=utf-8' 'text/plain'
SH
chmod 755 "$tmp/bin/wl-paste"
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "pages" as Pages

ShellRoot {
  QtObject {
    id: fake
    property bool supportsGroupAttachments: true
    property bool supportsGroupTyping: true
    property bool awaitingHelperInstance: false
    property string helperCompatibility: "compatible"
    property string connectionState: "online"
    property int groupsTick: 0
    property int friendsTick: 0
    property int typingTick: 0
    property int helperInstanceGeneration: 1
    property string lastError: ""
    property int sent: 0
    property string lastSentText: ""
    function filePathFor(conv) { return "" }
    function groupMembers(conv) { return [] }
    function groupInviteCandidates(conv) { return [] }
    function groupTypingActors(conv) { return [] }
    function isPeerTyping(conv) { return false }
    function filePending(conv) { return false }
    function fileSendingFor(conv) { return false }
    function outgoingFile(conv) { return ({ request: "image-request-1", path: "/tmp/canonical.png" }) }
    function sendConversationOp(operation, key, immediate) {
      lastSentText = String(operation.text || "")
      return true
    }
    function setTyping(conv, typing, key) { return false }
    function sendFile(path, conv, kind, key) {
      if (path !== "/tmp/canonical.png" || kind !== "image" ||
          String(conv).slice(0, 2) !== "g:")
        return false
      sent++
      return true
    }
    function discardAttachmentStage(path, request) { return true }
  }
  Item {
    TextEdit {
      id: selectionFixture
      text: ""
      textFormat: TextEdit.RichText
      readOnly: true
    }
    Pages.ChatPage {
      id: page
      width: 420
      height: 520
      service: fake
      demo: false
      conversation: "g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      peerName: "Group"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
    Pages.ChatPage {
      id: directPage
      visible: false
      width: 420
      height: 520
      demo: true
      conversation: "7"
      peerName: "Direct"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }
  Timer {
    interval: 20
    repeat: true
    running: true
    property int phase: 0
    property int attempts: 0
    onTriggered: {
      attempts++
      if (phase === 1) {
        page.send()
        if (fake.lastSentText === "plain clipboard") {
          console.log("OMAQ_CHAT_COMPOSER_OK")
          Qt.quit()
        } else if (attempts > 100) {
          console.log("OMAQ_CHAT_COMPOSER_BAD_TEXT", fake.lastSentText)
          Qt.quit()
        }
        return
      }
      page.attachmentInspectionPath = "/tmp/canonical.png"
      page.attachmentInspectionRequest = "stage-1"
      page.clipboardStageRequest = "stage-1"
      page.finishAttachmentInspection(true)
      var selectionSource = "line one\nliteral \u2028 separator\nliteral \u2029 paragraph " +
        "marker \u2063 and \u2064 repeated \u2063\u2063 plus \u2064\u2064"
      selectionFixture.text = page.messageMarkup(selectionSource, "", false)
      var renderedSelection = selectionFixture.getText(0, selectionFixture.length)
      var markerPosition = renderedSelection.indexOf("\u2063\u2063")
      selectionFixture.select(markerPosition, markerPosition + 1)
      var exactPartialSelection = markerPosition >= 0 &&
        page.clipboardSelectionText(selectionFixture, selectionSource, "") === "\u2063"
      selectionFixture.select(markerPosition + 1, markerPosition + 2)
      exactPartialSelection = exactPartialSelection &&
        page.clipboardSelectionText(selectionFixture, selectionSource, "") === "\u2063"
      selectionFixture.select(markerPosition, markerPosition + 2)
      exactPartialSelection = exactPartialSelection &&
        page.clipboardSelectionText(selectionFixture, selectionSource, "") === "\u2063"
      var repeatedPosition = renderedSelection.indexOf("\u2063\u2063\u2063\u2063")
      selectionFixture.select(repeatedPosition + 1, repeatedPosition + 3)
      exactPartialSelection = exactPartialSelection && repeatedPosition >= 0 &&
        page.clipboardSelectionText(selectionFixture, selectionSource, "") === "\u2063\u2063"
      var repeatedParagraph = renderedSelection.indexOf("\u2064\u2064\u2064\u2064")
      selectionFixture.select(repeatedParagraph + 1, repeatedParagraph + 3)
      exactPartialSelection = exactPartialSelection && repeatedParagraph >= 0 &&
        page.clipboardSelectionText(selectionFixture, selectionSource, "") === "\u2064\u2064"
      selectionFixture.selectAll()
      var copiedSelection = page.clipboardSelectionText(selectionFixture,
        selectionSource, "")
      var exactSelection = copiedSelection === selectionSource && exactPartialSelection
      if (!exactSelection)
        console.log("OMAQ_SELECTION_BAD", JSON.stringify(selectionFixture.selectedText),
          JSON.stringify(copiedSelection), JSON.stringify(selectionSource))

      var longReply = Array(30).join("quoted line\n") +
        "👨‍👩‍👧‍👦 👍🏽 🇩🇪 é tail"
      page.appendLine({ id: "group-reply-source", dir: "in", text: longReply,
        ack: -1 })
      directPage.appendLine({ id: "direct-reply-source", dir: "in",
        text: longReply, ack: -1 })
      var expectedReply = "↩ " + page.compactReplyPreview(longReply) + "\nOK"
      selectionFixture.text = page.messageMarkup("OK", "group-reply-source", false)
      var groupRendered = selectionFixture.getText(0, selectionFixture.length)
      var groupBreak = Math.max(groupRendered.indexOf("\u2028"),
        groupRendered.indexOf("\u2029"))
      selectionFixture.select(groupBreak - 2, groupBreak + 3)
      var groupPartial = page.clipboardSelectionText(selectionFixture, "OK",
        "group-reply-source") ===
        groupRendered.slice(groupBreak - 2, groupBreak + 3)
          .replace(/[\u2028\u2029]/g, "\n")
      selectionFixture.selectAll()
      var groupReplyCopy = page.clipboardSelectionText(selectionFixture, "OK",
        "group-reply-source")
      selectionFixture.text = directPage.messageMarkup("OK",
        "direct-reply-source", false)
      var directRendered = selectionFixture.getText(0, selectionFixture.length)
      var directBreak = Math.max(directRendered.indexOf("\u2028"),
        directRendered.indexOf("\u2029"))
      selectionFixture.select(directBreak - 2, directBreak + 3)
      var directPartial = directPage.clipboardSelectionText(selectionFixture, "OK",
        "direct-reply-source") ===
        directRendered.slice(directBreak - 2, directBreak + 3)
          .replace(/[\u2028\u2029]/g, "\n")
      selectionFixture.selectAll()
      var directReplyCopy = directPage.clipboardSelectionText(selectionFixture,
        "OK", "direct-reply-source")
      var groupReplyWidth = page.bubbleWidth("OK", "group-reply-source",
        false, false, 360)
      var directReplyWidth = directPage.bubbleWidth("OK", "direct-reply-source",
        false, false, 360)
      var replyParity = groupBreak > 1 && directBreak > 1 && groupPartial &&
        directPartial && groupReplyCopy === expectedReply &&
        directReplyCopy === expectedReply && groupReplyWidth > 250 &&
        directReplyWidth === groupReplyWidth && groupReplyWidth <= 360 * 0.82
      if (!replyParity)
        console.log("OMAQ_REPLY_BAD", JSON.stringify(groupReplyCopy),
          JSON.stringify(directReplyCopy), groupReplyWidth, directReplyWidth)

      page.appendLine({ id: "reaction-rank-1", dir: "in", text: "one", ack: -1,
        reactionMe: "👍", reactionPeer: "😂",
        groupReactions: [{ actor: "a", emoji: "👍" },
          { actor: "b", emoji: "❤️" }] })
      page.appendLine({ id: "reaction-rank-2", dir: "in", text: "two", ack: -1,
        reactionMe: "👍", reactionPeer: "😂",
        groupReactions: [{ actor: "c", emoji: "👍" },
          { actor: "d", emoji: "🎉" }] })
      var rankedReactions = page.mostUsedReactionSet(5)
      var selectedReactions = page.reactionChoicesFor("💯", 5)
      var reactionRanking = rankedReactions.length === 5 &&
        rankedReactions.join(" ") === "👍 😂 ❤️ 🎉 😀" &&
        selectedReactions.length === 5 && selectedReactions[4] === "💯" &&
        selectedReactions.indexOf("😀") < 0
      var preview = page.pendingImagePath === "/tmp/canonical.png" &&
        page.pendingImageStageRequest === "stage-1" && page.isSmileOnly("🥳") &&
        !page.isSmileOnly("⌘") && page.smilePx === 56 && exactSelection &&
        replyParity && reactionRanking
      var sent = page.sendPendingImage() && fake.sent === 1 &&
        page.pendingImageSendRequest === "image-request-1" &&
        page.pendingImagePath === "/tmp/canonical.png"
      if (!preview || !sent) {
        console.log("OMAQ_CHAT_COMPOSER_BAD", page.fileStatus, fake.sent)
        Qt.quit()
        return
      }
      page.releasePendingImageAfterSend()
      fake.supportsGroupAttachments = false
      fake.awaitingHelperInstance = true
      Quickshell.clipboardText = "plain clipboard"
      phase = 1
      attempts = 0
      page.pasteComposer()
    }
  }
}
QML
out="$tmp/out"
if ! PATH="$tmp/bin:$PATH" QT_QPA_PLATFORM=offscreen timeout 10 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
	cat "$out" >&2
	echo "chat-composer-parity: Quickshell fixture failed" >&2
	exit 1
fi
if ! grep -q 'OMAQ_CHAT_COMPOSER_OK' "$out"; then
	cat "$out" >&2
	echo "chat-composer-parity: GroupChat image preview/send state failed" >&2
	exit 1
fi
echo "chat-composer-parity: ok"
