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
      var preview = page.pendingImagePath === "/tmp/canonical.png" &&
        page.pendingImageStageRequest === "stage-1" && page.isSmileOnly("🥳") &&
        !page.isSmileOnly("⌘") && page.smilePx === 56
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
