import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  property var settings: ({})
  property int unreadCount: 0
  property string statusText: "OmaQ"
  property string lastError: ""
  property bool attached: false
  property int backoffMs: 200

  readonly property string helperPath: String(Qt.resolvedUrl("helper/omaq")).replace(/^file:\/\//, "")
  readonly property string homeDir: Quickshell.env("OMAQ_HOME") || (Quickshell.env("HOME") + "/.local/share/omaq")
  readonly property string stateDir: Quickshell.env("OMAQ_STATE") || (Quickshell.env("HOME") + "/.local/state/omaq")
  readonly property string sockPath: stateDir + "/omaq.sock"

  function handleLine(line) {
    var ev
    try { ev = JSON.parse(line) } catch (e) { return }
    if (ev.event === "snapshot" && ev.unread !== undefined)
      root.unreadCount = ev.unread
    if (ev.event === "error")
      root.lastError = ev.code || "error"
    if (ev.event === "message")
      root.unreadCount = root.unreadCount + 1
  }

  function sendOp(obj) {
    var line = JSON.stringify(obj) + "\n"
    if (sock.connected)
      sock.write(line)
    else
      proc.write(line)
  }

  function resetBackoff() {
    root.backoffMs = 200
  }

  function scheduleRestart() {
    root.lastError = "helper_down"
    restartTimer.interval = root.backoffMs
    if (root.backoffMs < 1000)
      root.backoffMs = 1000
    else if (root.backoffMs < 5000)
      root.backoffMs = 5000
    else
      root.backoffMs = Math.min(30000, root.backoffMs * 2)
    restartTimer.restart()
  }

  function attachSocket() {
    root.attached = true
    sock.path = root.sockPath
    sock.connected = true
  }

  Timer {
    id: restartTimer
    repeat: false
    onTriggered: {
      root.attached = false
      sock.connected = false
      proc.running = true
    }
  }

  Socket {
    id: sock
    path: root.sockPath
    connected: false
    parser: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onConnectionStateChanged: {
      if (connected)
        root.resetBackoff()
      else if (root.attached)
        root.scheduleRestart()
    }
    onError: function() {
      if (root.attached)
        root.scheduleRestart()
    }
  }

  Process {
    id: proc
    command: [root.helperPath]
    environment: ({
      "OMAQ_HOME": root.homeDir,
      "OMAQ_STATE": root.stateDir
    })
    running: true
    stdinEnabled: true
    stdout: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onStarted: root.resetBackoff()
    onExited: function(code) {
      if (code === 2) {
        root.attachSocket()
        return
      }
      if (code !== 0)
        root.scheduleRestart()
    }
  }
}
