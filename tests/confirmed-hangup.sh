#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
service = (root / "Service.qml").read_text(encoding="utf-8")
page = (root / "pages/ChatPage.qml").read_text(encoding="utf-8")
surface = (root / "ChatSurface.qml").read_text(encoding="utf-8")
helper = (root / "helper/omaq.c").read_text(encoding="utf-8")
av = (root / "helper/av.c").read_text(encoding="utf-8")
tox = (root / "helper/tox_adapt.c").read_text(encoding="utf-8")

contracts = {
    "Service.qml": [
        "readonly property bool supportsConfirmedHangup: root.activeHelperProtocol >= 15",
        'return ["call.start", "call.answer", "call.stop", "call.lease"].indexOf(',
        'root.rejectBoundOperation(operation, "call_control_unavailable")',
        'return immediate ? root.sendImmediateOp(op) : root.sendOp(op)',
        'function completeCallStop(conversation, callId, reason, cancelAttempted,',
        'function resetCallAfterHelperRestart(reason)',
        'root.resetCallAfterHelperRestart("helper_incompatible")',
        'root.lastCallState = "ending"',
    ],
    "pages/ChatPage.qml": [
        "readonly property bool callEnding:",
        "root.service.callEndingFor(root.conversation)",
        'root.callFeedback = "Call ended"',
        "function clearCallFeedback()",
        "onIncomingChanged:",
        "onInCallChanged:",
        "function onCallStopTickChanged()",
    ],
    "ChatSurface.qml": [
        '? "Ending…" : (toolbar.page ? toolbar.page.callFeedback : "")',
        "!toolbar.page.callEnding",
    ],
    "helper/omaq.c": [
        "#define OMAQ_PROTOCOL_VERSION 15",
        'strcmp(op->op, "call.lease") == 0',
        'call_control_owner_disconnected(g_clients[i], g_ncli == 1);',
        "g_call_owner_request[0] ? g_call_owner_request : NULL",
        'emit_bound_call_action_failed(op, "call_control_unavailable")',
        'emit_bound_call_action_failed(op, "identity_primary_uncertain")',
        'emit_bound_call_action_failed(op, "direct_state_migration_failed")',
        'emit_bound_call_action_failed(op, "locked")',
        "g_call_end_results[CALL_END_RESULT_MAX]",
        "g_call_end_alias_requests[MAX_CLIENTS][80]",
        "add_call_end_alias_request(g_call_owner_request)",
        "emit_bound_call_action_failed(op, \"identity_changed\")",
        "g_call_key[65]",
        '\\"localStopped\\":true,\\"transportClosed\\":true',
        "omaq_tox_av_destroy(g_tox)",
        "omaq_tox_av_create(g_tox)",
    ],
    "helper/av.c": ["int omaq_av_local_stopped(void)"],
    "helper/tox_adapt.c": [
        "int omaq_tox_av_destroy(struct omaq_tox *t)",
        "int omaq_tox_av_create(struct omaq_tox *t)",
    ],
}
for name, needles in contracts.items():
    source = {
        "Service.qml": service,
        "pages/ChatPage.qml": page,
        "ChatSurface.qml": surface,
        "helper/omaq.c": helper,
        "helper/av.c": av,
        "helper/tox_adapt.c": tox,
    }[name]
    for needle in needles:
        if needle not in source:
            raise SystemExit(f"confirmed-hangup: missing {name} contract: {needle}")

stop = helper.index("static int begin_call_end(")
stop_end = helper.index("static int set_call_owner(", stop)
stop_block = helper[stop:stop_end]
if "omaq_tox_friend_pk_hex" in stop_block:
    raise SystemExit("confirmed-hangup: teardown still depends on a live friend-key lookup")
if stop_block.index("omaq_av_stop(g_tox, friend)") > stop_block.index("queue_call_end("):
    raise SystemExit("confirmed-hangup: local media is not stopped before ending is queued")
reset = helper.index("static void reset_call_transport(", stop_end)
reset_end = helper.index("static void pump_call_audio(", reset)
reset_block = helper[reset:reset_end]
if reset_block.index("omaq_tox_av_destroy(g_tox)") > reset_block.index("finalize_call_end("):
    raise SystemExit("confirmed-hangup: terminal confirmation can precede transport destruction")
poll = helper.index("pr = poll(pf")
hup_drop = helper.index("if (revents & (POLLHUP | POLLERR | POLLNVAL))", poll)
drop = helper.index("drop_client(i);", hup_drop)
transport_reset = helper.index("reset_call_transport();", drop)
iterate = helper.index("omaq_tox_iterate(g_tox);", transport_reset)
pump = helper.index("pump_call_audio();", iterate)
if not poll < hup_drop < drop < transport_reset < iterate < pump:
    raise SystemExit("confirmed-hangup: transport reset can follow a post-IPC backend/audio pump")
PY

tmp=$(mktemp -d /tmp/omaq-confirmed-hangup-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cp "$root/Service.qml" "$tmp/Service.qml"
cat >"$tmp/qmldir" <<'EOF'
module TestOmaq
Service 1.0 Service.qml
EOF
python3 - "$tmp/Service.qml" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = "  Component.onCompleted: root.launchHelperDetached()\n"
if text.count(old) != 1:
    raise SystemExit("confirmed-hangup: Service launch seam changed")
path.write_text(text.replace(old, "  Component.onCompleted: {}\n"), encoding="utf-8")
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "." as TestOmaq

ShellRoot {
  TestOmaq.Service { id: service }
  Timer {
    interval: 0
    running: true
    onTriggered: {
      var key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      var callA = "0123456789abcdef"
      var callB = "fedcba9876543210"
      service.helperCompatibility = "compatible"
      service.friendsReady = true
      service.friends = [{ id: "0", key: key }]

      service.activeHelperProtocol = 14
      var capabilityGate = !service.supportsConfirmedHangup
      service.activeHelperProtocol = 15
      capabilityGate = capabilityGate && service.supportsConfirmedHangup

      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      service.pendingOps = []
      var stopTick = service.callStopTick
      var offlineRejected = !service.stopCall("0", key) &&
        service.pendingOps.length === 0 &&
        service.pendingCallStopRequest === "" &&
        service.lastCallStopCode === "call_control_unavailable" &&
        service.callStopTick === stopTick + 1

      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      service.pendingCallStopRequest = "stop-request-a"
      service.pendingCallStopConv = "0"
      service.pendingCallStopKey = key
      service.pendingCallStopId = callA
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callA, reason: "local", localStopped: true,
        transportClosed: true, cancelAttempted: true, cancelAccepted: true,
        audioAvailable: true }))
      var missingCorrelationRejected = service.pendingCallStopRequest ===
        "stop-request-a" && service.callStopTick === stopTick &&
        service.lastCallState === "active"
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callB, request: "stop-request-a", reason: "local",
        localStopped: true, transportClosed: true, cancelAttempted: true,
        cancelAccepted: true, audioAvailable: true }))
      var staleCallRejected = service.pendingCallStopRequest === "stop-request-a" &&
        service.callStopTick === stopTick
      service.handleLine(JSON.stringify({ event: "call.state", conversation: "0",
        key: key, callId: callA, request: "stop-request-a", state: "ending" }))
      var endingVisible = service.callEndingFor("0") &&
        service.lastCallState === "ending" && service.callStopTick === stopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callA, request: "stop-request-a", reason: "local",
        localStopped: true, transportClosed: true, cancelAttempted: true,
        cancelAccepted: false, audioAvailable: true }))
      var correlatedStopAccepted = service.pendingCallStopRequest === "" &&
        service.lastCallState === "ended" &&
        service.lastCallStopCode === "cancel_unconfirmed" &&
        service.lastCallStopCancelAttempted &&
        !service.lastCallStopCancelAccepted &&
        service.callStopTick === stopTick + 1
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callA, request: "stop-request-a", reason: "local",
        localStopped: true, transportClosed: true, cancelAttempted: true,
        cancelAccepted: false, audioAvailable: true }))
      var duplicateTerminalIgnored = service.callStopTick === stopTick

      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callB
      service.pendingCallStopRequest = "stop-request-b"
      service.pendingCallStopConv = "0"
      service.pendingCallStopKey = key
      service.pendingCallStopId = callB
      service.callOwnerRequest = "answer-request-b"
      service.callOwnerCallId = callB
      service.callOwnerConnectionLost = true
      service.applyCallSnapshot(null)
      var emptySnapshotWaitsForTerminal = service.lastCallState === "ending" &&
        service.lastCallConv === "0" && service.lastCallId === callB &&
        service.pendingCallStopRequest === "stop-request-b" &&
        service.callEndingFor("0")
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.state", conversation: "0",
        key: key, callId: callB, request: "stop-request-b", state: "ended",
        reason: "local" }))
      var bareEndedRejected = service.lastCallState === "ending" &&
        service.pendingCallStopRequest === "stop-request-b" &&
        service.callStopTick === stopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callB, reason: "control_lost", localStopped: true,
        transportClosed: true, cancelAttempted: true, cancelAccepted: true,
        audioAvailable: true }))
      var reconnectTerminalAccepted = service.lastCallState === "ended" &&
        service.lastCallStopCode === "control_lost" &&
        service.pendingCallStopRequest === ""

      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      service.callOwnerRequest = "answer-request-c"
      service.callOwnerCallId = callA
      service.callOwnerConnectionLost = false
      service.pendingCallStopRequest = ""
      service.pendingOps = []
      var leaseFailedClosed = !service.renewCallLease() &&
        service.callOwnerConnectionLost && service.callEndingFor("0") &&
        service.pendingOps.length === 0

      service.callOwnerRequest = "pending-answer"
      service.callOwnerCallId = ""
      var requestSequence = service.callRequestSequence
      var duplicateActionsBlocked = service.callActionPending &&
        !service.startCall("0", key) && !service.answerCall("0", key) &&
        service.callOwnerRequest === "pending-answer" &&
        service.callRequestSequence === requestSequence

      var replacementKey = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"
      service.friends = [{ id: "0", key: replacementKey }]
      service.callOwnerRequest = "stale-binding-start"
      service.callOwnerOperation = "start"
      service.callOwnerConv = "0"
      service.callOwnerKey = key
      service.callOwnerCallId = ""
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.action.failed", op: "start",
        conversation: "0", key: key, request: "stale-binding-start",
        code: "identity_changed" }))
      var staleBindingFailureAccepted = service.callOwnerRequest === "" &&
        service.lastCallStopCode === "call_start_failed" &&
        service.callStopTick === stopTick + 1
      service.friends = [{ id: "0", key: key }]

      service.lastCallState = ""
      service.lastCallConv = ""
      service.lastCallKey = ""
      service.lastCallId = ""
      service.callOwnerRequest = "start-before-disconnect"
      service.callOwnerOperation = "start"
      service.callOwnerConv = "0"
      service.callOwnerKey = key
      service.callOwnerCallId = ""
      service.scheduleRestart()
      service.applyCallSnapshot(null)
      var unresolvedStartPreserved = service.callOwnerRequest ===
        "start-before-disconnect" && service.callOwnerConnectionLost &&
        service.callOwnerConv === "0" && service.callOwnerKey === key &&
        service.callOwnerCallId === ""
      service.friendsReady = true
      service.friends = [{ id: "0", key: key }]
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callB, request: "other-client-stop", reason: "local",
        localStopped: true, transportClosed: true, cancelAttempted: true,
        cancelAccepted: true, audioAvailable: true }))
      var foreignStopWaitsForOwnerResult = service.callOwnerRequest ===
        "start-before-disconnect" && service.callStopTick === stopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callB, request: "start-before-disconnect",
        reason: "local", localStopped: true, transportClosed: true,
        cancelAttempted: true, cancelAccepted: true, audioAvailable: true }))
      var unresolvedStartTerminalAccepted = service.callOwnerRequest === "" &&
        service.lastCallStopConv === "0" &&
        service.lastCallStopCode === "local" &&
        service.callStopTick === stopTick + 1

      service.lastCallState = "incoming"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      service.callOwnerRequest = "answer-before-disconnect"
      service.callOwnerOperation = "answer"
      service.callOwnerConv = "0"
      service.callOwnerKey = key
      service.callOwnerCallId = ""
      service.callOwnerConnectionLost = true
      service.applyCallSnapshot(null)
      var unresolvedAnswerPreserved = service.lastCallState === "ending" &&
        service.lastCallId === callA &&
        service.callOwnerRequest === "answer-before-disconnect"
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callA, request: "answer-before-disconnect",
        reason: "control_lost", localStopped: true, transportClosed: true,
        cancelAttempted: true, cancelAccepted: true, audioAvailable: true }))
      var unresolvedAnswerTerminalAccepted = service.lastCallState === "ended" &&
        service.callOwnerRequest === "" && service.callStopTick === stopTick + 1

      service.handleLine(JSON.stringify({ event: "call.incoming",
        conversation: "0", key: key, callId: callB }))
      service.applyCallSnapshot(null)
      service.applyFriendSnapshot([])
      var incomingSnapshotPreserved = service.lastCallState === "ending" &&
        service.lastCallConv === "0" && service.lastCallKey === key &&
        service.lastCallId === callB
      stopTick = service.callStopTick
      service.handleLine(JSON.stringify({ event: "call.stopped", conversation: "0",
        key: key, callId: callB, reason: "control_lost", localStopped: true,
        transportClosed: true, cancelAttempted: true, cancelAccepted: true,
        audioAvailable: true }))
      var incomingTerminalAccepted = service.lastCallState === "ended" &&
        service.lastCallStopCode === "control_lost" &&
        service.callStopTick === stopTick + 1
      service.friends = [{ id: "0", key: key }]

      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      service.pendingCallStopRequest = "restart-stop"
      service.pendingCallStopConv = "0"
      service.pendingCallStopKey = key
      service.pendingCallStopId = callA
      stopTick = service.callStopTick
      var restartReset = service.resetCallAfterHelperRestart() &&
        service.lastCallState === "" && service.lastCallId === "" &&
        service.pendingCallStopRequest === "" &&
        service.lastCallStopCode === "helper_restarted" &&
        service.callStopTick === stopTick + 1

      var written = []
      service.writeQueuedOperations([
        JSON.stringify({ op: "call.stop", conversation: "0", key: key,
          callId: callA, request: "must-not-flush" }) + "\n"
      ], function(line) { written.push(line) })
      var queuedCallRejected = written.length === 0 && service.pendingOps.length === 0

      service.helperCompatibility = "compatible"
      service.activeHelperProtocol = 15
      service.lastCallState = "active"
      service.lastCallConv = "0"
      service.lastCallKey = key
      service.lastCallId = callA
      stopTick = service.callStopTick
      service.markHelperIncompatible()
      var incompatibleReset = service.lastCallState === "" &&
        service.lastCallId === "" && service.callOwnerRequest === "" &&
        service.pendingCallStopRequest === "" &&
        service.lastCallStopCode === "helper_incompatible" &&
        service.callStopTick === stopTick + 1

      var valid = capabilityGate && offlineRejected && missingCorrelationRejected &&
        staleCallRejected && endingVisible && correlatedStopAccepted &&
        duplicateTerminalIgnored && emptySnapshotWaitsForTerminal &&
        bareEndedRejected && reconnectTerminalAccepted && leaseFailedClosed &&
        duplicateActionsBlocked && staleBindingFailureAccepted &&
        unresolvedStartPreserved && foreignStopWaitsForOwnerResult &&
        unresolvedStartTerminalAccepted &&
        unresolvedAnswerPreserved && unresolvedAnswerTerminalAccepted &&
        incomingSnapshotPreserved && incomingTerminalAccepted && restartReset &&
        queuedCallRejected && incompatibleReset
      console.log(valid ? "OMAQ_CONFIRMED_HANGUP_OK" :
        "OMAQ_CONFIRMED_HANGUP_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 8 quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1 ||
   ! grep -q 'OMAQ_CONFIRMED_HANGUP_OK' "$out"; then
  cat "$out" >&2
  echo "confirmed-hangup: QML fixture failed" >&2
  exit 1
fi

echo "confirmed-hangup: ok"
