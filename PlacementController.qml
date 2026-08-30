import QtQuick
import Quickshell.Io

QtObject {
  id: root

  readonly property int waitingPhase: 0
  readonly property int placingPhase: 1
  readonly property int settledPhase: 2
  property int phase: waitingPhase
  readonly property bool busy: phase === placingPhase
  readonly property bool settled: phase === settledPhase

  property string scriptPath: ""
  property string windowTitle: ""
  property bool placementRequested: false
  property bool windowReady: false
  property int requestedX: 0
  property int requestedY: 0
  property int requestedWidth: 420
  property int requestedHeight: 420
  property int targetX: 0
  property int targetY: 0
  property int targetWidth: 420
  property int targetHeight: 420
  property string targetTitle: ""
  property var observedGeometry: null
  property bool cancelRequested: false
  property bool processStarted: false
  property bool processHandled: false

  signal placementFinished(bool success, var geometry)
  signal placementCanceled()

  function settleWithoutPlacement() {
    if (root.phase === root.waitingPhase && !root.placementRequested)
      root.phase = root.settledPhase
  }

  function begin() {
    if (root.phase !== root.waitingPhase)
      return
    if (!root.placementRequested) {
      root.settleWithoutPlacement()
      return
    }
    if (!root.windowReady || !root.scriptPath || !root.windowTitle ||
        root.windowTitle === "OmaQ chat")
      return
    root.phase = root.placingPhase
    root.targetTitle = root.windowTitle
    root.targetX = root.requestedX
    root.targetY = root.requestedY
    root.targetWidth = root.requestedWidth
    root.targetHeight = root.requestedHeight
    root.observedGeometry = null
    root.cancelRequested = false
    root.processStarted = false
    root.processHandled = false
    placementProcess.command = ["/usr/bin/timeout", "--kill-after=1s", "5s",
      root.scriptPath, "place-title", root.targetTitle,
      String(root.targetX), String(root.targetY),
      String(root.targetWidth), String(root.targetHeight)]
    placementProcess.running = true
  }

  function recordGeometry(raw) {
    if (root.phase !== root.placingPhase)
      return
    var text = String(raw || "")
    var value
    if (text.length === 0 || text.length > 2048)
      return
    try { value = JSON.parse(text) } catch (error) { return }
    var monitor = String(value.monitor || "")
    if (String(value.title || "") !== root.targetTitle ||
        value.floating !== true || monitor.length === 0 || monitor.length > 63 ||
        /[\u0000-\u001f\u007f]/.test(monitor) ||
        value.x !== root.targetX || value.y !== root.targetY ||
        value.width !== root.targetWidth || value.height !== root.targetHeight)
      return
    root.observedGeometry = { monitor: monitor, x: value.x, y: value.y,
      width: value.width, height: value.height, floating: true }
  }

  function finish(code) {
    if (root.phase !== root.placingPhase)
      return
    var geometry = root.observedGeometry
    var canceled = root.cancelRequested
    var success = !canceled && code === 0 && geometry !== null
    root.phase = root.settledPhase
    root.observedGeometry = null
    if (canceled)
      root.placementCanceled()
    else
      root.placementFinished(success, success ? geometry : null)
  }

  function cancel() {
    if (root.phase !== root.placingPhase) {
      root.phase = root.settledPhase
      return
    }
    root.cancelRequested = true
    if (placementProcess.running && root.processStarted)
      placementProcess.signal(15)
  }

  property Process placementProcess: Process {
    running: false
    stdout: SplitParser {
      onRead: function(line) { root.recordGeometry(line) }
    }
    onStarted: {
      root.processStarted = true
      if (root.cancelRequested)
        placementProcess.signal(15)
    }
    onExited: function(code) {
      root.processHandled = true
      root.finish(code)
    }
    onRunningChanged: {
      if (!running && root.phase === root.placingPhase &&
          !root.processHandled && !root.processStarted) {
        root.processHandled = true
        root.finish(127)
      }
    }
  }
}
