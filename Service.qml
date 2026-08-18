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
  property string inviteUrl: ""
  property string qrPath: ""
  property string safetyCode: ""
  property string safetyConv: ""
  property bool pending: false
  property string lastConversation: "0"
  property string lastAddr: ""
  property string lastGroup: ""
  property bool pendingGroup: false
  property string lastChatText: ""
  property string lastChatDir: ""
  property var lastSurface: ({})

  readonly property string helperPath: String(Qt.resolvedUrl("helper/omaq")).replace(/^file:\/\//, "")
  readonly property string homeDir: Quickshell.env("OMAQ_HOME") || (Quickshell.env("HOME") + "/.local/share/omaq")
  readonly property string stateDir: Quickshell.env("OMAQ_STATE") || (Quickshell.env("HOME") + "/.local/state/omaq")
  readonly property string sockPath: stateDir + "/omaq.sock"
  readonly property string defaultQrPath: {
    var d = Quickshell.env("XDG_DOWNLOAD_DIR")
    if (!d || d === "")
      d = (Quickshell.env("HOME") || "") + "/Downloads"
    return d + "/omaq-invite.png"
  }

  function handleLine(line) {
    var ev
    try { ev = JSON.parse(line) } catch (e) { return }
    if (ev.event === "snapshot") {
      if (ev.unread !== undefined)
        root.unreadCount = ev.unread
      if (ev.addr)
        root.lastAddr = ev.addr
    }
    if (ev.event === "error")
      root.lastError = ev.code || "error"
    if (ev.event === "message") {
      root.unreadCount = root.unreadCount + 1
      if (ev.conversation)
        root.lastConversation = ev.conversation
      root.lastChatText = ev.text || ""
      root.lastChatDir = "in"
    }
    if (ev.event === "surface")
      root.lastSurface = ev
    if (ev.event === "invite") {
      if (ev.url)
        root.inviteUrl = ev.url
      if (ev.qr)
        root.qrPath = ev.qr
    }
    if (ev.event === "request") {
      root.pending = true
      root.pendingGroup = ev.kind === "group"
    }
    if (ev.event === "group.changed") {
      if (ev.group)
        root.lastGroup = ev.group
      if (ev.action === "create" || ev.action === "join")
        root.lastConversation = ev.group || root.lastConversation
    }
    if (ev.event === "safety") {
      root.safetyCode = ev.code || ""
      root.safetyConv = ev.conversation || root.lastConversation
      if (ev.conversation)
        root.lastConversation = ev.conversation
    }
  }

  function sendOp(obj) {
    var line = JSON.stringify(obj) + "\n"
    if (sock.connected)
      sock.write(line)
    else
      proc.write(line)
  }

  function createInvite() { sendOp({ op: "invite.create", kind: "direct", ttlSec: 86400 }) }
  function revokeInvite() { sendOp({ op: "invite.revoke" }); root.inviteUrl = "" }
  function saveQr() {
    var u = root.inviteUrl
    if (!u)
      return
    sendOp({ op: "invite.qr", payload: u, path: root.defaultQrPath })
  }
  function redeem(url) { sendOp({ op: "invite.redeem", payload: url }) }
  function decide(ok) {
    sendOp({ op: "contact.decide", id: "x", accept: !!ok })
    root.pending = false
  }
  function removeContact() { sendOp({ op: "contact.remove", id: root.lastConversation }) }
  function rotateNospam() { sendOp({ op: "nospam.rotate" }); root.inviteUrl = "" }
  function getSafety() { sendOp({ op: "safety.get", conversation: root.lastConversation }) }
  function createGroup() { sendOp({ op: "group.create", title: "group" }) }
  function inviteToGroup() {
    if (!root.lastGroup)
      return
    sendOp({ op: "invite.create", kind: "group", group: root.lastGroup, role: "member", id: root.lastConversation, ttlSec: 86400 })
  }
  function dissolveGroup() {
    if (!root.lastGroup)
      return
    sendOp({ op: "group.dissolve", group: root.lastGroup })
  }
  function openCard() {
    sendOp({ op: "surface.set", conversation: root.lastConversation, monitor: "", x: 40, y: 80, pinned: false })
  }
  function setSurface(conv, mon, x, y, pinned) {
    sendOp({ op: "surface.set", conversation: conv, monitor: mon || "", x: x, y: y, pinned: !!pinned })
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
