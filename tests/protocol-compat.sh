#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-protocol-compat-XXXXXX)
cleanup() {
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$tmp/home" "$tmp/state" "$tmp/helper"
cp "$root/Service.qml" "$tmp/Service.qml"

make -s -C "$root" \
	BIN_IPC_TEST_HELPER="$tmp/helper/omaq" \
	SANFLAGS="-DOMAQ_PROTOCOL_VERSION=7" \
	"$tmp/helper/omaq"

cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "."

ShellRoot {
  Service { id: service }
  Timer {
    interval: 50
    repeat: true
    running: true
    property int attempts: 0
    onTriggered: {
      attempts++
      if (service.helperCompatibility === "compatible") {
        var directKey = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        var replacementKey = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        service.friends = [{ id: "0", key: directKey }]
        var bindingChecks = service.directBindingMatches("0", directKey) &&
          service.operationBindingValid({ op: "msg.send", conversation: "0",
            key: directKey }) &&
          !service.operationBindingValid({ op: "msg.send", conversation: "0" }) &&
          !service.operationBindingValid({ op: "msg.send", key: directKey }) &&
          !service.operationBindingValid({ op: "file.accept", conversation:
            "g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }) &&
          !service.operationBindingValid({ op: "msg.send", conversation: "0",
            key: replacementKey })
        service.pendingOps = [JSON.stringify({ op: "msg.send", conversation: "0",
          key: directKey, text: "must-not-send", id: "stale-queued-message" }) + "\n"]
        service.applyFriendSnapshot([{ id: "0", key: replacementKey }])
        var reusePurged = service.pendingOps.length === 0 &&
          service.lastMessageFailedRequest === "stale-queued-message" &&
          service.lastMessageFailedCode === "identity_changed"
        service.activeHelperProtocol = 11
        service.friends = []
        service.friendsReady = false
        service.pendingCallSnapshot = { conversation: "0", key: directKey,
          state: "incoming" }
        service.pendingCallSnapshotSet = true
        var callDeferred = service.lastCallConv === ""
        var messageTickBefore = service.messageTick
        service.handleLine(JSON.stringify({ event: "message", conversation: "0",
          key: directKey, id: "buffered-message", text: "buffered", dir: "in" }))
        var bufferedUntilFriends = service.messageTick === messageTickBefore &&
          service.pendingDirectEvents.length === 1
        service.handleLine(JSON.stringify({ event: "friend.list.begin", generation: "ready-1" }))
        service.handleLine(JSON.stringify({ event: "friend.info", generation: "ready-1",
          id: "0", key: directKey, name: "Ready", online: true }))
        service.handleLine(JSON.stringify({ event: "friend.list.end", generation: "ready-1" }))
        var replayedAfterFriends = service.friendsReady &&
          service.pendingDirectEvents.length === 0 && service.lastChatId === "buffered-message" &&
          callDeferred && service.lastCallConv === "0" && service.lastCallKey === directKey
        service.searchConversation = "0"
        service.searchRequest = "old-search"
        service.searchItems = [{ text: "old" }]
        service.applyFriendSnapshot([{ id: "0", key: replacementKey }])
        var reboundContentPurged = service.lastChatText === "" &&
          service.lastChatKey === "" && service.lastChatConv === "" &&
          service.searchConversation === "" && service.searchItems.length === 0
        var searchTickBefore = service.searchTick
        service.searchConversation = "0"
        service.searchRequest = "delayed-search"
        service.handleLine(JSON.stringify({ event: "search", conversation: "0",
          key: directKey, request: "delayed-search", items: [{ text: "stale" }] }))
        var delayedSearchRejected = service.searchTick === searchTickBefore
        service.activeHelperProtocol = 7
        service.friends = [{ id: "0", key: directKey }]
        if (service.activeHelperProtocol === 7 &&
            !service.supportsIdentityActions &&
            !service.supportsAttachments &&
            !service.supportsDirectRecovery &&
            !service.supportsRedeemResults &&
            !service.supportsStableDirectState &&
            !service.exportIdentity("/tmp/blocked", "blocked-export") &&
            !service.inspectIdentity("/tmp/blocked", "", "blocked-inspect") &&
            !service.importIdentity("/tmp/blocked", true, "", "blocked-import") &&
            !service.protectIdentity("blocked-pass", "blocked-protect") &&
            !service.unprotectIdentity("blocked-pass", "blocked-unprotect") &&
            !service.setNickname("blocked", "blocked-nickname") &&
            bindingChecks && reusePurged && bufferedUntilFriends &&
            replayedAfterFriends && reboundContentPurged && delayedSearchRejected &&
            service.redeem("legacy-invite") === "legacy") {
          console.log("OMAQ_PROTOCOL_COMPAT_OK")
        } else {
          console.log("OMAQ_PROTOCOL_COMPAT_BAD_CAPABILITIES")
        }
        Qt.quit()
      } else if (attempts >= 160) {
        console.log("OMAQ_PROTOCOL_COMPAT_TIMEOUT", service.helperCompatibility,
                    service.lastError)
        Qt.quit()
      }
    }
  }
}
QML

out="$tmp/quickshell.out"
if ! OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" \
	QT_QPA_PLATFORM=offscreen timeout 12 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
	cat "$out" >&2
	echo "protocol-compat: Quickshell fixture failed" >&2
	exit 1
fi
if ! grep -q 'OMAQ_PROTOCOL_COMPAT_OK' "$out"; then
	cat "$out" >&2
	echo "protocol-compat: Protocol-7 helper was not accepted with newer capabilities disabled" >&2
	exit 1
fi

echo "protocol-compat: ok"
