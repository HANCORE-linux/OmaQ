import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  property var settings: ({})
  property int unreadCount: 0
  property string statusText: "OmaQ"
  property string lastError: ""

  readonly property string helperPath: String(Qt.resolvedUrl("helper/omaq")).replace(/^file:\/\//, "")
  readonly property string homeDir: Quickshell.env("OMAQ_HOME") || (Quickshell.env("HOME") + "/.local/share/omaq")
  readonly property string stateDir: Quickshell.env("OMAQ_STATE") || (Quickshell.env("HOME") + "/.local/state/omaq")

  function sendOp(obj) {
    proc.write(JSON.stringify(obj) + "\n")
  }

  Process {
    id: proc
    command: [root.helperPath]
    environment: ({
      "OMAQ_HOME": root.homeDir,
      "OMAQ_STATE": root.stateDir
    })
    running: true
    stdout: SplitParser {
      onRead: function(line) {
        var ev
        try { ev = JSON.parse(line) } catch (e) { return }
        if (ev.event === "snapshot" && ev.unread !== undefined)
          root.unreadCount = ev.unread
        if (ev.event === "error")
          root.lastError = ev.code || "error"
        if (ev.event === "message")
          root.unreadCount = root.unreadCount + 1
      }
    }
    onExited: function(code) {
      if (code === 2)
        root.statusText = "OmaQ"
      else if (code !== 0)
        root.lastError = "helper_down"
    }
  }
}
