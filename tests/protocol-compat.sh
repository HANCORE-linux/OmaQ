#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-protocol-compat-XXXXXX)
cleanup() {
	if [ -f "$tmp/state/omaq.pid" ]; then
		pid=$(cat "$tmp/state/omaq.pid" 2>/dev/null || true)
		case "$pid" in
			''|*[!0-9]*) ;;
			*)
				exe=$(readlink "/proc/$pid/exe" 2>/dev/null || true)
				[ "$exe" != "$tmp/helper/omaq" ] || kill "$pid" 2>/dev/null || true
				;;
		esac
	fi
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$tmp/home" "$tmp/state" "$tmp/helper"
cp "$root/Service.qml" "$tmp/Service.qml"

make -s -C "$root" \
	BIN_HELP="$tmp/helper/omaq-protocol14" \
	HARDEN_CFLAGS="-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fPIE -DOMAQ_PROTOCOL_VERSION=14" \
	helper
make -s -C "$root" \
	BIN_HELP="$tmp/helper/omaq-protocol15" \
	HARDEN_CFLAGS="-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fPIE -DOMAQ_PROTOCOL_VERSION=15" \
	helper
make -s -C "$root" \
	BIN_IPC_TEST_HELPER="$tmp/helper/omaq" \
	SANFLAGS="-DOMAQ_PROTOCOL_VERSION=7" \
	"$tmp/helper/omaq"
make -s -C "$root" \
	BIN_IPC_TEST_HELPER="$tmp/helper/omaq-protocol14-ipc" \
	SANFLAGS="-DOMAQ_PROTOCOL_VERSION=14" \
	"$tmp/helper/omaq-protocol14-ipc"

python3 - "$tmp/helper/omaq-protocol14-ipc" "$tmp/home" "$tmp/state" <<'PY'
import json
import os
import select
import subprocess
import sys
import time

helper, home, state = sys.argv[1:]
env = os.environ.copy()
env.update({"OMAQ_HOME": home, "OMAQ_STATE": state})
proc = subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, env=env)
commands = [
    {"op": "status"},
    {"op": "history.clear", "conversation": "0", "id": "legacy-clear"},
]
proc.stdin.write(b"".join((json.dumps(value, separators=(",", ":")) + "\n").encode()
                         for value in commands))
proc.stdin.flush()
buffer = b""
events = []
deadline = time.monotonic() + 5
while time.monotonic() < deadline and not any(event.get("event") == "history"
                                               for event in events):
    ready, _, _ = select.select([proc.stdout], [], [],
                                max(0, deadline - time.monotonic()))
    if not ready:
        break
    chunk = os.read(proc.stdout.fileno(), 65536)
    if not chunk:
        break
    buffer += chunk
    while b"\n" in buffer:
        line, buffer = buffer.split(b"\n", 1)
        if line:
            events.append(json.loads(line))
snapshot = next((event for event in events if event.get("event") == "snapshot"), None)
cleared = next((event for event in events if event.get("event") == "history"), None)
if not snapshot or snapshot.get("protocol") != 14 or snapshot.get("historyClear") != 1:
    raise SystemExit("protocol-compat: Protocol-14 clear capability changed")
if (not cleared or cleared.get("conversation") != "0" or
        cleared.get("cleared") is not True or "request" in cleared):
    raise SystemExit("protocol-compat: Protocol-14 legacy clear changed")
proc.terminate()
proc.wait(timeout=5)
PY

cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "."

ShellRoot {
  property string searchSignalConversation: ""
  property string searchSignalKey: ""
  property string searchSignalRequest: ""
  property var searchSignalItems: []
  Service { id: service }
  Connections {
    target: service
    function onChatSearchResult(conversation, key, request, items) {
      searchSignalConversation = String(conversation || "")
      searchSignalKey = String(key || "")
      searchSignalRequest = String(request || "")
      searchSignalItems = items || []
    }
  }
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
          service.operationBindingValid({ op: "file.accept", conversation:
            "g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }) &&
          !service.operationBindingValid({ op: "msg.send", conversation: "0",
            key: replacementKey })
        service.pendingOps = [JSON.stringify({ op: "msg.send", conversation: "0",
          key: directKey, text: "must-not-send", id: "stale-queued-message" }) + "\n"]
        service.applyFriendSnapshot([{ id: "0", key: replacementKey }])
        var reusePurged = service.pendingOps.length === 0 &&
          service.lastMessageFailedRequest === "stale-queued-message" &&
          service.lastMessageFailedCode === "identity_changed"
        service.activeHelperProtocol = 13
        var groupAttachmentGate = service.supportsGroupAttachments &&
          service.supportsCorrelatedGroupProjection && service.supportsGroupTyping
        service.activeHelperProtocol = 11
        groupAttachmentGate = groupAttachmentGate && !service.supportsGroupAttachments &&
          !service.supportsCorrelatedGroupProjection && !service.supportsGroupTyping
        service.friends = []
        service.friendsReady = false
        service.pendingCallSnapshot = { conversation: "0", key: directKey,
          state: "incoming" }
        service.pendingCallSnapshotSet = true
        var callDeferred = service.lastCallConv === ""
        var messageTickBefore = service.messageTick
        service.handleLine(JSON.stringify({ event: "message", conversation: "0",
          key: directKey, id: "buffered-message", text: "buffered", dir: "in",
          ts: 1700000000 }))
        var bufferedUntilFriends = service.messageTick === messageTickBefore &&
          service.pendingDirectEvents.length === 1
        service.handleLine(JSON.stringify({ event: "friend.list.begin", generation: "ready-1" }))
        service.handleLine(JSON.stringify({ event: "friend.info", generation: "ready-1",
          id: "0", key: directKey, name: "Ready", online: true }))
        service.handleLine(JSON.stringify({ event: "friend.list.end", generation: "ready-1" }))
        var replayedAfterFriends = service.friendsReady &&
          service.pendingDirectEvents.length === 0 && service.lastChatId === "buffered-message" &&
          service.lastChatTimestamp === 1700000000 &&
          callDeferred && service.lastCallConv === "0" && service.lastCallKey === directKey
        service.handleLine(JSON.stringify({ event: "search", conversation: "0",
          key: directKey, request: "chat-search", items: [{ text: "found",
            ts: 1700000000 }] }))
        var chatSearchSignaled = searchSignalConversation === "0" &&
          searchSignalKey === directKey && searchSignalRequest === "chat-search" &&
          searchSignalItems.length === 1 && searchSignalItems[0].text === "found"
        service.applyFriendSnapshot([{ id: "0", key: replacementKey }])
        var reboundContentPurged = service.lastChatText === "" &&
          service.lastChatKey === "" && service.lastChatConv === ""
        var searchSignalBefore = searchSignalRequest
        service.handleLine(JSON.stringify({ event: "search", conversation: "0",
          key: directKey, request: "delayed-search", items: [{ text: "stale" }] }))
        var delayedSearchRejected = searchSignalRequest === searchSignalBefore &&
          searchSignalItems.length === 1 && searchSignalItems[0].text === "found"
        var groupId = "g:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
        service.groups = [{ id: groupId, memberCount: 1, limit: 10, members: [] }]
        service.friends = [{ id: "0", key: directKey, name: "Invitee" }]
        service.pendingOps = []
        service.awaitingHelperInstance = true
        var inviteRequest = service.nextGroupInviteRequest()
        var groupInviteAccepted = service.inviteToGroup("0", directKey,
          groupId, inviteRequest)
        var queuedInvite = service.pendingOps.length === 1
          ? JSON.parse(service.pendingOps[0]) : ({})
        var groupInviteWired = groupInviteAccepted && queuedInvite.op === "invite.create" &&
          queuedInvite.kind === "group" && queuedInvite.group === groupId &&
          queuedInvite.id === "0" && queuedInvite.key === directKey &&
          queuedInvite.request === inviteRequest
        service.pendingOps = []
        service.awaitingHelperInstance = false
        service.activeHelperProtocol = 13
        service.helperInstance = "11111111111111111111111111111111"
        service.expectedGroupRequest = "group-snapshot-1"
        service.handleLine(JSON.stringify({ event: "group.list.begin", generation: "9",
          instance: service.helperInstance, request: "group-snapshot-1", groups: 1, members: 1 }))
        service.handleLine(JSON.stringify({ event: "group.info", generation: "9",
          instance: service.helperInstance, request: "group-snapshot-1", group: groupId,
          title: "Restored", members: 1, limit: 10 }))
        service.handleLine(JSON.stringify({ event: "group.member", generation: "9",
          instance: service.helperInstance, request: "group-snapshot-1", group: groupId,
          peer: "0", key: replacementKey, friendKey: "", name: "You",
          role: "owner", online: true, self: true }))
        service.handleLine(JSON.stringify({ event: "group.list.end", generation: "9",
          instance: service.helperInstance, request: "group-snapshot-1", groups: 1, members: 1 }))
        var correlatedGroups = service.groupsReady && service.groups.length === 1 &&
          service.groups[0].id === groupId && service.groups[0].members.length === 1 &&
          service.operationBindingValid({ op: "typing.set", conversation: groupId })
        service.handleLine(JSON.stringify({ event: "typing", conversation: groupId,
          actor: replacementKey, typing: true }))
        var groupTypingProjected = service.groupTypingActors(groupId).length === 1
        service.handleLine(JSON.stringify({ event: "typing", conversation: groupId,
          actor: replacementKey, typing: false }))
        groupTypingProjected = groupTypingProjected &&
          service.groupTypingActors(groupId).length === 0
        service.expectedGroupRequest = "group-snapshot-2"
        service.handleLine(JSON.stringify({ event: "group.list.begin", generation: "10",
          instance: service.helperInstance, request: "wrong-request", groups: 0, members: 0 }))
        var wrongGroupRequestIgnored = service.pendingGroupGeneration === "" &&
          service.groups.length === 1
        service.handleLine(JSON.stringify({ event: "group.list.begin", generation: "10",
          instance: service.helperInstance, request: "group-snapshot-2", groups: 1, members: 1 }))
        service.handleLine(JSON.stringify({ event: "group.info", generation: "10",
          instance: service.helperInstance, request: "group-snapshot-2", group: groupId,
          title: "Incomplete", members: 1, limit: 10 }))
        service.handleLine(JSON.stringify({ event: "group.list.end", generation: "10",
          instance: service.helperInstance, request: "group-snapshot-2", groups: 1, members: 1 }))
        var incompleteGroupsPreserved = !service.groupsReady && service.groups.length === 1 &&
          service.groups[0].title === "Restored"
        service.pendingOps = []
        service.awaitingHelperInstance = false
        service.activeHelperProtocol = 7
        var legacySurface = service.surfaceOperation(groupId, "DP-1", 10, 20,
          true, 500, 600)
        var legacySurfaceCompatible = legacySurface.width === undefined &&
          legacySurface.height === undefined && legacySurface.x === 10 && legacySurface.y === 20
        service.pendingOps = []
        service.awaitingHelperInstance = true
        var handshakeSurfaceSent = service.setSurface(groupId, "DP-1", 10, 20,
          true, "", 500, 600)
        var handshakeSurface = service.pendingOps.length === 1
          ? JSON.parse(service.pendingOps[0]) : ({})
        var handshakeSurfaceQueued = handshakeSurfaceSent &&
          handshakeSurface.width === 500 && handshakeSurface.height === 600
        service.pendingOps = []
        service.activeHelperProtocol = 14
        service.awaitingHelperInstance = false
        var handshake14Line = ""
        service.writeQueuedOperations([JSON.stringify(handshakeSurface) + "\n"],
          function(line) { handshake14Line = line })
        var handshake14Surface = JSON.parse(handshake14Line)
        var handshake14Geometry = handshake14Surface.width === 500 &&
          handshake14Surface.height === 600
        var modernSurface = service.surfaceOperation(groupId, "DP-1", 10, 20,
          true, 500, 600)
        var modernSurfaceGeometry = modernSurface.width === 500 &&
          modernSurface.height === 600
        var downgradedLine = ""
        service.activeHelperProtocol = 7
        service.writeQueuedOperations([JSON.stringify(handshakeSurface) + "\n"],
          function(line) { downgradedLine = line })
        var downgradedSurface = JSON.parse(downgradedLine)
        var downgradeQueueCompatible = downgradedSurface.width === undefined &&
          downgradedSurface.height === undefined && downgradedSurface.pinned === true
        service.activeHelperProtocol = 14
        service.customSounds = [{ id: "11111111111111111111111111111111",
          label: "Valid", path: service.homeDir +
            "/custom-sounds/11111111111111111111111111111111.audio", size: 48 }]
        service.pendingSoundRequests = ({
          "sound-malformed": { operation: "list", command: { op: "sound.list",
            request: "sound-malformed" } }
        })
        service.handleLine(JSON.stringify({ event: "sound.list", op: "list",
          request: "sound-malformed", selected: "" }))
        var malformedSoundFailedClosed = service.customSounds.length === 0 &&
          service.pendingSoundRequests["sound-malformed"] === undefined &&
          service.lastSoundCode === "sound_state_failed"
        service.customSounds = [{ id: "22222222222222222222222222222222",
          label: "Stale", path: service.homeDir +
            "/custom-sounds/22222222222222222222222222222222.audio", size: 48 }]
        service.pendingSoundRequests = ({
          "sound-late": { operation: "import", command: { op: "sound.import",
            request: "sound-late", path: "/tmp/source.wav" } }
        })
        service.activeHelperProtocol = 13
        service.handleLine(JSON.stringify({ event: "sound.list", op: "import",
          request: "sound-late", selected: "22222222222222222222222222222222",
          items: [{ id: "22222222222222222222222222222222", label: "Stale",
            path: service.homeDir +
              "/custom-sounds/22222222222222222222222222222222.audio", size: 48 }] }))
        var staleSoundRejected = service.customSounds.length === 0 &&
          !service.pendingSoundRequests["sound-late"]
        service.activeHelperProtocol = 14
        var confirmedHangupGate = !service.supportsConfirmedHangup
        service.activeHelperProtocol = 15
        confirmedHangupGate = confirmedHangupGate && service.supportsConfirmedHangup
        service.clearPendingRequest()
        service.handleLine(JSON.stringify({ event: "request", kind: "direct" }))
        var protocol15InviteCompatible = service.pending && !service.pendingGroup &&
          !service.supportsInviteRequestSafety && service.pendingRequestKey === "" &&
          service.pendingRequestSafety === ""
        var protocol15RedeemTick = service.redeemTick
        service.handleLine(JSON.stringify({ event: "invite.redeemed", kind: "direct",
          request: "protocol15-redeem" }))
        var protocol15RedeemCompatible =
          service.lastRedeemRequest === "protocol15-redeem" &&
          service.lastRedeemKind === "direct" && service.lastRedeemSafety === "" &&
          service.redeemTick === protocol15RedeemTick + 1
        service.clearPendingRequest()
        var safetyPartsA = []
        var safetyPartsB = []
        for (var safetyIndex = 0; safetyIndex < 16; safetyIndex++) {
          safetyPartsA.push("aaaa")
          safetyPartsB.push("bbbb")
        }
        var requestSafety = safetyPartsA.join(" ") + " / " + safetyPartsB.join(" ")
        service.activeHelperProtocol = 16
        service.handleLine(JSON.stringify({ event: "request", kind: "direct",
          key: directKey, safety: requestSafety }))
        service.handleLine(JSON.stringify({ event: "request.conflict", kind: "direct",
          key: replacementKey }))
        var protocol16InviteSafety = service.pending && !service.pendingGroup &&
          service.supportsInviteRequestSafety && service.pendingRequestKey === directKey &&
          service.pendingRequestSafety === requestSafety &&
          service.pendingRequestConflictKey === replacementKey
        var protocol16RedeemTick = service.redeemTick
        service.handleLine(JSON.stringify({ event: "invite.redeemed", kind: "direct",
          request: "protocol16-redeem", key: directKey, safety: requestSafety }))
        var protocol16RedeemSafety =
          service.lastRedeemRequest === "protocol16-redeem" &&
          service.lastRedeemKind === "direct" &&
          service.lastRedeemSafety === requestSafety &&
          service.redeemTick === protocol16RedeemTick + 1
        service.handleLine(JSON.stringify({ event: "invite.redeemed", kind: "direct",
          request: "protocol16-malformed", key: replacementKey, safety: "bad" }))
        var protocol16MalformedRedeemRejected =
          service.lastRedeemRequest === "protocol16-redeem" &&
          service.lastRedeemKind === "direct" && service.lastRedeemSafety === "" &&
          service.redeemTick === protocol16RedeemTick + 1 &&
          service.lastError === "helper_event_invalid" &&
          service.lastErrorRequest === "protocol16-malformed"
        service.activeHelperProtocol = 7
        service.friends = [{ id: "0", key: directKey }]
        if (service.activeHelperProtocol === 7 &&
            !service.supportsIdentityActions &&
            !service.supportsAttachments &&
            !service.supportsDirectRecovery &&
            !service.supportsRedeemResults &&
            !service.supportsStableDirectState &&
            !service.supportsGroupAttachments &&
            !service.exportIdentity("/tmp/blocked", "blocked-export") &&
            !service.inspectIdentity("/tmp/blocked", "", "blocked-inspect") &&
            !service.importIdentity("/tmp/blocked", true, "", "blocked-import") &&
            !service.protectIdentity("blocked-pass", "blocked-protect") &&
            !service.unprotectIdentity("blocked-pass", "blocked-unprotect") &&
            !service.setNickname("blocked", "blocked-nickname") &&
            bindingChecks && groupAttachmentGate && groupInviteWired &&
            legacySurfaceCompatible && handshakeSurfaceQueued && handshake14Geometry &&
            modernSurfaceGeometry && downgradeQueueCompatible && malformedSoundFailedClosed &&
            confirmedHangupGate && protocol15InviteCompatible &&
            protocol15RedeemCompatible && protocol16InviteSafety &&
            protocol16RedeemSafety && protocol16MalformedRedeemRejected &&
            correlatedGroups && groupTypingProjected &&
            wrongGroupRequestIgnored &&
            incompleteGroupsPreserved && reusePurged &&
            bufferedUntilFriends && replayedAfterFriends && chatSearchSignaled &&
            reboundContentPurged && delayedSearchRejected &&
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
