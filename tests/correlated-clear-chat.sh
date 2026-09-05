#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
helper=${1:-"$root/tests/omaq_ipc_test_helper"}

python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
service = (root / "Service.qml").read_text(encoding="utf-8")
page = (root / "pages/ChatPage.qml").read_text(encoding="utf-8")
helper = (root / "helper/omaq.c").read_text(encoding="utf-8")

contracts = {
    "Service.qml": [
        "property bool supportsCorrelatedHistoryClear: false",
        'Number(ev.historyClear) === 2',
        'id: request }, bindingKey, true)',
        'ev.event === "history.clear.succeeded"',
        'ev.event === "history.clear.failed"',
        "function historyClearEventMatches(event)",
        "function failPendingHistoryClears(code)",
        "historyClearReloadRequiredByConversation",
    ],
    "pages/ChatPage.qml": [
        "readonly property bool historyClearPending:",
        "readonly property bool historyClearReloadRequired:",
        'root.reactionStatus = "Clearing chat…"',
        'root.reactionStatus = "Chat cleared"',
        "Reload chat before clearing again",
        "root.applyHistory(root.service.lastHistoryItems, root.service.lastHistoryCleared)",
        'root.reactionStatus = "An item could not be added to chat history"',
    ],
    "helper/omaq.c": [
        "#define OMAQ_PROTOCOL_VERSION 15",
        "#define OMAQ_HISTORY_CLEAR_VERSION 2",
        "#define OMAQ_HISTORY_CLEAR_VERSION 1",
        "history.clear.succeeded",
        "history.clear.failed",
        "reject_correlated_history_clear",
        "unread_persist_failed",
        "receipt_state_failed",
        "result_unknown",
    ],
}
sources = {
    "Service.qml": service,
    "pages/ChatPage.qml": page,
    "helper/omaq.c": helper,
}
for name, needles in contracts.items():
    for needle in needles:
        if needle not in sources[name]:
            raise SystemExit(f"correlated-clear-chat: missing {name} contract: {needle}")

if "lastFilePath" in page[page.index("function onLastErrorTickChanged()"):
                          page.index("function onLastFileTickChanged()")]:
    raise SystemExit("correlated-clear-chat: generic history errors reuse a stale file path")
if 'op: "history.clear", conversation: c },\n      expectedKey, false' in service:
    raise SystemExit("correlated-clear-chat: destructive clear can still be queued")
clear_history = service[service.index("function clearHistory("):
                        service.index("function markConversationRead(")]
send_accepted = clear_history.index("if (!root.sendConversationOp")
for marker in ("root.pendingHistoryUnread, c", "root.historyRequestByConversation, c",
               "root.historyKeyByConversation, c"):
    if clear_history.rfind(marker) <= send_accepted:
        raise SystemExit(f"correlated-clear-chat: accepted clear keeps stale read {marker!r}")
apply_history = page[page.index("function applyHistory("):
                     page.index("function bubbleWidth(")]
for marker in ("if (cleared)", "lines.clear()", "if (!cleared && existing"):
    if marker not in apply_history:
        raise SystemExit(f"correlated-clear-chat: confirmed clear lost {marker!r}")
PY

tmp=$(mktemp -d /tmp/omaq-correlated-clear-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/home" "$tmp/state"
chmod 700 "$tmp/home" "$tmp/state"
python3 - "$helper" "$tmp/home" "$tmp/state" <<'PY'
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time

helper, home, state = sys.argv[1:]
env = os.environ.copy()
env.update({"OMAQ_HOME": home, "OMAQ_STATE": state})
proc = subprocess.Popen([helper], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, env=env)
read_buffer = b""
pending_events = []

def send(value):
    proc.stdin.write((json.dumps(value, separators=(",", ":")) + "\n").encode())
    proc.stdin.flush()

def receive(predicate, timeout=5):
    global read_buffer
    deadline = time.monotonic() + timeout
    seen = []
    while time.monotonic() < deadline:
        while pending_events:
            event = pending_events.pop(0)
            seen.append(event)
            if predicate(event):
                return event
        ready, _, _ = select.select([proc.stdout], [], [], max(0, deadline - time.monotonic()))
        if not ready:
            break
        chunk = os.read(proc.stdout.fileno(), 65536)
        if not chunk:
            break
        read_buffer += chunk
        while b"\n" in read_buffer:
            line, read_buffer = read_buffer.split(b"\n", 1)
            if line:
                pending_events.append(json.loads(line))
    raise SystemExit(f"correlated-clear-chat: helper event missing; saw {seen!r}")

key = "a" * 64
send({"op": "status", "id": "clear-status"})
snapshot = receive(lambda ev: ev.get("event") == "snapshot" and
                    ev.get("request") == "clear-status")
if snapshot.get("protocol") != 15 or snapshot.get("historyClear") != 2:
    raise SystemExit("correlated-clear-chat: clear capability was not advertised")

send({"op": "history.clear", "conversation": "0", "key": key,
      "id": "clear-direct-1"})
result = receive(lambda ev: ev.get("event", "").startswith("history.clear."))
expected = {"event": "history.clear.succeeded", "conversation": "0",
            "key": key, "request": "clear-direct-1",
            "instance": snapshot["instance"]}
if result != expected:
    raise SystemExit(f"correlated-clear-chat: wrong clear result: {result!r}")

os.chmod(state, 0o500)
try:
    send({"op": "history.clear", "conversation": "1", "key": "b" * 64,
          "id": "clear-unread-failure"})
    failed = receive(lambda ev: ev.get("event") == "history.clear.failed")
finally:
    os.chmod(state, 0o700)
if (failed.get("request") != "clear-unread-failure" or
        failed.get("code") != "unread_persist_failed"):
    raise SystemExit("correlated-clear-chat: pre-delete state failure was not correlated")
history_dir = Path(home) / "history" / "1"
history_dir.mkdir(parents=True)
(history_dir / "messages.jsonl").write_text('{"id":"old"}\n', encoding="utf-8")
(history_dir / "messages.jsonl.1").mkdir()
send({"op": "history.clear", "conversation": "1", "key": "b" * 64,
      "id": "partial-clear"})
partial = receive(lambda ev: ev.get("event") == "history.clear.failed")
if (partial.get("request") != "partial-clear" or
        partial.get("code") != "result_unknown" or
        (history_dir / "messages.jsonl").exists() or
        not (history_dir / "messages.jsonl.1").is_dir()):
    raise SystemExit("correlated-clear-chat: partial deletion was not reported unknown")

send({"op": "history.clear", "conversation": "1", "key": "b" * 64})
legacy = receive(lambda ev: ev.get("event") == "error")
if legacy.get("conversation") != "1" or legacy.get("code") != "request_required":
    raise SystemExit("correlated-clear-chat: uncorrelated Protocol-15 clear did not fail closed")
send({"op": "history.clear", "conversation": "1", "key": "b" * 64,
      "id": "invalid-clear-shape", "limit": 1})
invalid = receive(lambda ev: ev.get("event") == "history.clear.failed")
if invalid.get("request") != "invalid-clear-shape" or invalid.get("code") != "forbidden":
    raise SystemExit("correlated-clear-chat: correlated clear accepted extra fields")

proc.terminate()
proc.wait(timeout=5)
PY

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
    raise SystemExit("correlated-clear-chat: Service launch seam changed")
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
      var keyA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      var keyB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
      var instance = "11111111111111111111111111111111"
      var groupA = "g:" + keyA
      var groupB = "g:" + keyB
      service.helperCompatibility = "compatible"
      service.activeHelperProtocol = 15
      service.helperInstance = instance
      service.friendsReady = true
      service.friends = [{ id: "0", key: keyA }, { id: "1", key: keyB }]
      service.groupsReady = true
      service.groups = [{ id: groupA, title: "A", members: [] },
                        { id: groupB, title: "B", members: [] }]
      service.supportsCorrelatedHistoryClear = true

      service.pendingOps = []
      var offlineClearRejected = !service.clearHistory("0", keyA) &&
        !service.historyClearPending("0") && service.pendingOps.length === 0

      service.pendingHistoryClears = ({ "0": { request: "clear-a", key: keyA,
        instance: instance, expiresAt: Date.now() + 10000 } })
      service.historyRequestByConversation = ({ "0": "old-history" })
      service.historyKeyByConversation = ({ "0": keyA })
      service.pendingHistoryUnread = ({ "0": [4] })
      service.lastChatConv = "0"
      service.lastChatText = "old message"
      var clearTick = service.historyClearTick
      var historyTick = service.historyTick
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: "1", key: keyB, request: "clear-a", instance: instance }))
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: "0", key: keyA, request: "foreign", instance: instance }))
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: "0", key: keyB, request: "clear-a", instance: instance }))
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: groupA, request: "clear-a", instance: instance }))
      var foreignClearRejected = service.historyClearTick === clearTick &&
        service.historyClearPending("0") && service.historyTick === historyTick
      service.handleLine(JSON.stringify({ event: "history", conversation: "0",
        key: keyA, cleared: true, items: [] }))
      var legacyClearRejected = service.historyTick === historyTick &&
        service.historyClearPending("0")
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: "0", key: keyA, request: "clear-a", instance: instance }))
      var directClearAccepted = !service.historyClearPending("0") &&
        service.lastHistoryClearSucceeded && service.lastHistoryClearConv === "0" &&
        service.historyClearTick === clearTick + 1 &&
        service.historyTick === historyTick + 1 &&
        service.lastHistoryItems.length === 0 && service.lastHistoryCleared &&
        !service.historyRequestByConversation["0"] &&
        service.lastChatText === "" && service.lastChatConv === ""
      historyTick = service.historyTick
      service.handleLine(JSON.stringify({ event: "history", conversation: "0",
        key: keyA, request: "old-history", items: [{ text: "stale" }] }))
      var staleHistoryRejected = service.historyTick === historyTick &&
        service.lastHistoryItems.length === 0

      service.pendingHistoryClears = ({})
      service.pendingHistoryClears[groupA] = { request: "clear-group-a", key: "",
        instance: instance, expiresAt: Date.now() + 10000 }
      clearTick = service.historyClearTick
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: groupB, request: "clear-group-a", instance: instance }))
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: "0", key: keyA, request: "clear-group-a", instance: instance }))
      var foreignGroupRejected = service.historyClearTick === clearTick &&
        service.historyClearPending(groupA)
      service.handleLine(JSON.stringify({ event: "history.clear.succeeded",
        conversation: groupA, request: "clear-group-a", instance: instance }))
      var groupClearAccepted = service.historyClearTick === clearTick + 1 &&
        service.lastHistoryClearSucceeded && !service.historyClearPending(groupA)

      service.pendingHistoryClears = ({ "1": { request: "clear-fail", key: keyB,
        instance: instance, expiresAt: Date.now() + 10000 } })
      historyTick = service.historyTick
      service.handleLine(JSON.stringify({ event: "history.clear.failed",
        conversation: "1", key: keyB, request: "clear-fail", instance: instance,
        code: "receipt_state_failed" }))
      var clearFailureCorrelated = !service.historyClearPending("1") &&
        !service.lastHistoryClearSucceeded &&
        service.lastHistoryClearCode === "receipt_state_failed" &&
        service.historyTick === historyTick &&
        !service.historyClearReloadRequired("1")
      service.pendingHistoryClears = ({ "1": { request: "unknown-clear", key: keyB,
        instance: instance, expiresAt: Date.now() + 10000 } })
      service.failPendingHistoryClears("result_unknown")
      var unknownClearBlocksRetry = service.historyClearReloadRequired("1")
      service.historyRequestByConversation = ({ "1": "reload-history" })
      service.historyKeyByConversation = ({ "1": keyB })
      service.handleLine(JSON.stringify({ event: "history", conversation: "1",
        key: keyB, request: "reload-history", unread: 0, items: [] }))
      var reloadAllowsRetry = !service.historyClearReloadRequired("1")

      var valid = offlineClearRejected && foreignClearRejected &&
        legacyClearRejected && directClearAccepted && staleHistoryRejected &&
        foreignGroupRejected && groupClearAccepted && clearFailureCorrelated &&
        unknownClearBlocksRetry && reloadAllowsRetry
      if (!valid)
        console.log("OMAQ_CORRELATED_CLEAR_BAD " + JSON.stringify({
          offlineClearRejected: offlineClearRejected,
          foreignClearRejected: foreignClearRejected,
          legacyClearRejected: legacyClearRejected,
          directClearAccepted: directClearAccepted,
          staleHistoryRejected: staleHistoryRejected,
          foreignGroupRejected: foreignGroupRejected,
          groupClearAccepted: groupClearAccepted,
          clearFailureCorrelated: clearFailureCorrelated,
          unknownClearBlocksRetry: unknownClearBlocksRetry,
          reloadAllowsRetry: reloadAllowsRetry
        }))
      else
        console.log("OMAQ_CORRELATED_CLEAR_OK")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 8 quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1 ||
   ! grep -q 'OMAQ_CORRELATED_CLEAR_OK' "$out"; then
  cat "$out" >&2
  echo "correlated-clear-chat: QML fixture failed" >&2
  exit 1
fi

echo "correlated-clear-chat: ok"
