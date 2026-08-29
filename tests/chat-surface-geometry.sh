#!/bin/sh
# Offscreen regression: a keyed ListModel removal preserves other window delegates.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-chat-geometry-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$root/ChatSurface.qml" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
required = (
    "ListModel {\n    id: openCardModel",
    "openCardModel.remove(index)",
    "model: root.isSurfaceOwner && root.floatRulesReady ? root.openCards : null",
    "required property string conversation",
    "property var pendingSurfaceOpens: []",
    "function queueSurfaceOpen(conversation, directKey, name, monitor, focus)",
    "queueSurfaceOpen(conv, expectedKey, name, monitor, true)",
    "queueSurfaceOpen(conversation, directKey, name, preferredMonitor, false)",
    "if (pending[i].focus && !value.focus)",
    "value.monitor = pending[i].monitor",
    "if (pending[i].focus)",
    "function flushPendingSurfaceOpens()",
    "!root.surfacesHydrated || root.floatRuleReloadBlocked",
    "root.flushPendingSurfaceOpens()",
    "var persistedOrder = []",
    'if (String(persisted[i].monitor || "") !== "")',
    "function cardMonitorCollides(cardMonitor, targetMonitor)",
    "function surfaceRectanglesOverlap(x, y, width, height, card)",
    "function initialGeometry(monitor, preferredWidth, preferredHeight)",
    'placeWindow.command = [root.floatScriptPath, "place-title", pinWin.title,',
    'command: [root.floatScriptPath, "list-geometry"]',
    'function applyGeometrySnapshot(raw)',
    "if (pinWin.placementAttempts < 12)",
    'service.setSurface(String(current.conversation || ""),',
    "pinned: keepExplicitlyOpen ? true : !!saved.pinned",
    "explicitOpen: true",
    'var removedKey = String(removed.directKey || "")',
    'service.setSurface(conversation, removedMonitor, removedX,',
)
for value in required:
    if value not in text:
        raise SystemExit(f"chat-surface-geometry: missing keyed geometry guard: {value}")
for forbidden in ("openCards = next", "openCards = filtered", "root.openCards = []"):
    if forbidden in text:
        raise SystemExit(f"chat-surface-geometry: array model reset returned: {forbidden}")
remove = text[text.index("function dismissCard"):text.index("function pin(")]
if remove.index("var removedKey") > remove.index("openCardModel.remove(index)"):
    raise SystemExit("chat-surface-geometry: geometry snapshot occurs after model removal")
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell

ShellRoot {
  ListModel {
    id: cards
    ListElement { conversation: "a"; monitor: ""; surfaceX: 40 }
    ListElement { conversation: "b"; monitor: "DP-1"; surfaceX: 488 }
  }
  property var survivor: null
  property real nextX: 40
  property bool hydrated: false
  property bool ruleBlocked: true
  property bool queuedOpen: true
  property bool flushedEarly: false
  property var mergedOpen: ({ conversation: "b", directKey: "key",
    name: "Explicit", monitor: "DP-2", focus: true })
  ListModel { id: restoredCards }
  function restoredInitialX(targetMonitor) {
    var x = 40
    for (var attempt = 0; attempt < 8; attempt++) {
      var occupied = false
      for (var i = 0; i < restoredCards.count; i++) {
        var card = restoredCards.get(i)
        if ((card.monitor === "" || card.monitor === targetMonitor) &&
            x < card.surfaceX + 432 && x + 432 > card.surfaceX) {
          occupied = true
          break
        }
      }
      if (!occupied)
        return x
      x += 448
    }
    return x
  }
  function restoreLegacyFirst() {
    var persisted = [
      { conversation: "legacy", monitor: "", surfaceX: 40 },
      { conversation: "explicit", monitor: "DP-1", surfaceX: 40 }
    ]
    for (var pass = 0; pass < 2; pass++)
      for (var i = 0; i < persisted.length; i++) {
        var legacy = persisted[i].monitor === ""
        if ((pass === 0 && legacy) || (pass === 1 && !legacy))
          continue
        var x = legacy ? restoredInitialX("DP-1") : persisted[i].surfaceX
        restoredCards.append({ conversation: persisted[i].conversation,
          monitor: "DP-1", surfaceX: x })
      }
  }
  function initialX(targetMonitor) {
    var x = 40
    for (var attempt = 0; attempt < 8; attempt++) {
      var occupied = false
      for (var i = 0; i < cards.count; i++) {
        var card = cards.get(i)
        if ((card.monitor === "" || card.monitor === targetMonitor) &&
            x < card.surfaceX + 432 && x + 432 > card.surfaceX) {
          occupied = true
          break
        }
      }
      if (!occupied)
        return x
      x += 448
    }
    return x
  }
  function mergeAutoOpen() {
    var value = { conversation: "b", directKey: "key", name: "Auto",
      monitor: "DP-1", focus: false }
    if (mergedOpen.focus && !value.focus) {
      value.name = mergedOpen.name
      value.monitor = mergedOpen.monitor
    }
    value.focus = value.focus || mergedOpen.focus
    mergedOpen = value
  }
  function flushPending() {
    if (!hydrated || ruleBlocked || !queuedOpen)
      return
    cards.append({ conversation: "c", monitor: "DP-1", surfaceX: nextX })
    queuedOpen = false
  }
  Instantiator {
    id: windows
    model: cards
    delegate: QtObject {
      required property string conversation
      required property real surfaceX
      readonly property string identity: conversation
    }
  }
  Timer {
    interval: 30
    running: true
    onTriggered: {
      survivor = windows.objectAt(1)
      mergeAutoOpen()
      restoreLegacyFirst()
      nextX = initialX("DP-1")
      cards.remove(0)
      hydrated = true
      flushPending()
      flushedEarly = cards.count !== 1
    }
  }
  Timer {
    interval: 80
    running: true
    onTriggered: {
      ruleBlocked = false
      flushPending()
      var preserved = survivor && survivor === windows.objectAt(0) &&
        windows.objectAt(0).identity === "b" && windows.objectAt(0).surfaceX === 488
      var restoreOrdered = restoredCards.count === 2 &&
        restoredCards.get(0).conversation === "explicit" &&
        restoredCards.get(0).surfaceX === 40 &&
        restoredCards.get(1).conversation === "legacy" &&
        restoredCards.get(1).surfaceX === 488
      var explicitPreserved = mergedOpen.focus && mergedOpen.monitor === "DP-2" &&
        mergedOpen.name === "Explicit"
      if (!flushedEarly && !queuedOpen && cards.count === 2 && preserved &&
          explicitPreserved && restoreOrdered && nextX === 936 &&
          windows.objectAt(1).surfaceX - windows.objectAt(0).surfaceX >= 448)
        console.log("OMAQ_GEOMETRY_OK")
      else
        console.log("OMAQ_GEOMETRY_BAD")
      Qt.quit()
    }
  }
}
QML
out="$tmp/out"
if ! QT_QPA_PLATFORM=offscreen timeout 5 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
  cat "$out" >&2
  echo "chat-surface-geometry: Quickshell fixture failed" >&2
  exit 1
fi
if ! grep -q 'OMAQ_GEOMETRY_OK' "$out"; then
  cat "$out" >&2
  echo "chat-surface-geometry: unaffected delegate was recreated" >&2
  exit 1
fi
echo "chat-surface-geometry: ok"
