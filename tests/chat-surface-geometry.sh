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
    "property int geometryGeneration: 0",
    "property int geometrySnapshotGeneration: -1",
    "root.geometrySnapshotGeneration !== root.geometryGeneration",
    "root.geometrySnapshotGeneration = root.geometryGeneration",
    "root.geometryGeneration++",
    "(pinWindow.localResizePending || pinWindow.placementStarted)",
    "property int desiredWidth: pinWin.boundedWidth(pinWin.surfaceWidth)",
    "property int pendingWidth: pinWin.boundedWidth(pinWin.surfaceWidth)",
    "property bool localResizePending: false",
    "function captureActualWidth()",
    "function captureActualHeight()",
    "root.updateCard(index, { surfaceWidth: nextWidth })",
    "root.updateCard(index, { surfaceHeight: nextHeight })",
    "onSurfaceWidthChanged: pinWin.syncDesiredWidth()",
    "onWidthChanged: pinWin.captureActualWidth()",
    "onHeightChanged: pinWin.captureActualHeight()",
    "id: placementSettle",
    "pinWin.pendingWidth,",
    "pinWin.localResizePending = false",
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
  ListModel {
    id: resizeCards
    ListElement { conversation: "7"; surfaceWidth: 420; surfaceHeight: 420 }
    ListElement {
      conversation: "g:0000000000000000000000000000000000000000000000000000000000000000"
      surfaceWidth: 420
      surfaceHeight: 420
    }
  }
  property var survivor: null
  property var directResize: null
  property var groupResize: null
  property bool baseGeometryOk: false
  property int geometryGeneration: 0
  property int staleSnapshotGeneration: -1
  property bool pendingSnapshotRejected: false

  function applyDelayedSnapshot(generation, target, width, height) {
    if (generation !== geometryGeneration)
      return false
    var targetWindow = resizeWindows.objectAt(target)
    if (targetWindow.localResizePending || targetWindow.placementStarted)
      return false
    resizeCards.setProperty(target, "surfaceWidth", width)
    resizeCards.setProperty(target, "surfaceHeight", height)
    return true
  }
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
  Instantiator {
    id: resizeWindows
    model: resizeCards
    delegate: QtObject {
      id: resizeDelegate
      required property int index
      required property string conversation
      required property real surfaceWidth
      required property real surfaceHeight
      property int desiredWidth: surfaceWidth
      property int desiredHeight: surfaceHeight
      property int pendingWidth: surfaceWidth
      property int pendingHeight: surfaceHeight
      property bool localResizePending: false
      property bool placementStarted: false

      function syncDesiredWidth() {
        if (localResizePending && surfaceWidth !== pendingWidth)
          return
        desiredWidth = surfaceWidth
        if (!localResizePending)
          pendingWidth = surfaceWidth
      }
      function syncDesiredHeight() {
        if (localResizePending && surfaceHeight !== pendingHeight)
          return
        desiredHeight = surfaceHeight
        if (!localResizePending)
          pendingHeight = surfaceHeight
      }
      function captureActualSize(actualWidth, actualHeight) {
        desiredWidth = actualWidth
        desiredHeight = actualHeight
        pendingWidth = actualWidth
        pendingHeight = actualHeight
        localResizePending = true
        geometryGeneration++
        resizeCards.setProperty(index, "surfaceWidth", actualWidth)
        resizeCards.setProperty(index, "surfaceHeight", actualHeight)
        geometrySave.restart()
      }
      onSurfaceWidthChanged: syncDesiredWidth()
      onSurfaceHeightChanged: syncDesiredHeight()
      property Timer geometrySave: Timer {
        interval: 35
        repeat: false
        onTriggered: {
          resizeCards.setProperty(resizeDelegate.index, "surfaceWidth",
            resizeDelegate.pendingWidth)
          resizeCards.setProperty(resizeDelegate.index, "surfaceHeight",
            resizeDelegate.pendingHeight)
          resizeDelegate.localResizePending = false
        }
      }
    }
  }
  Timer {
    interval: 30
    running: true
    onTriggered: {
      survivor = windows.objectAt(1)
      directResize = resizeWindows.objectAt(0)
      groupResize = resizeWindows.objectAt(1)
      staleSnapshotGeneration = geometryGeneration
      directResize.captureActualSize(700, 760)
      groupResize.captureActualSize(540, 680)
      // A stale model echo during the debounce must not restore the old size.
      resizeCards.setProperty(0, "surfaceWidth", 420)
      resizeCards.setProperty(0, "surfaceHeight", 420)
      resizeCards.setProperty(1, "surfaceWidth", 420)
      resizeCards.setProperty(1, "surfaceHeight", 420)
      pendingSnapshotRejected = !applyDelayedSnapshot(geometryGeneration,
        0, 420, 420)
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
      baseGeometryOk = !flushedEarly && !queuedOpen && cards.count === 2 &&
        preserved && explicitPreserved && restoreOrdered && nextX === 936 &&
        windows.objectAt(1).surfaceX - windows.objectAt(0).surfaceX >= 448 &&
        !directResize.localResizePending && directResize.desiredWidth === 700 &&
        directResize.desiredHeight === 760 && directResize.pendingWidth === 700 &&
        directResize.pendingHeight === 760 &&
        !groupResize.localResizePending && groupResize.desiredWidth === 540 &&
        groupResize.desiredHeight === 680 && groupResize.pendingWidth === 540 &&
        groupResize.pendingHeight === 680 && pendingSnapshotRejected &&
        !applyDelayedSnapshot(staleSnapshotGeneration, 0, 420, 420)
      directResize.placementStarted = true
      baseGeometryOk = baseGeometryOk &&
        !applyDelayedSnapshot(geometryGeneration, 0, 420, 420)
      directResize.placementStarted = false
      // An idle helper/compositor snapshot remains authoritative.
      resizeCards.setProperty(0, "surfaceWidth", 640)
      resizeCards.setProperty(0, "surfaceHeight", 720)
    }
  }
  Timer {
    interval: 120
    running: true
    onTriggered: {
      var externalSync = directResize.desiredWidth === 640 &&
        directResize.desiredHeight === 720 && directResize.pendingWidth === 640 &&
        directResize.pendingHeight === 720 &&
        groupResize.desiredWidth === 540 && groupResize.desiredHeight === 680
      console.log(baseGeometryOk && externalSync
        ? "OMAQ_GEOMETRY_OK" : "OMAQ_GEOMETRY_BAD")
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
