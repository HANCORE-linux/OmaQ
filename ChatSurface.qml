import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui
import qs.Commons
import "pages" as Pages

Item {
  id: root
  required property var service
  property var bar: null
  property var settings: ({})

  readonly property bool notifyRight: setting("notifyRightPanel", false)
  readonly property bool notifyDesk: setting("notifyDesktop", false)
  readonly property bool animateUnread: setting("animateUnread", true)
  readonly property string surfaceMode: String(setting("surfaceMode", "separate"))
  readonly property string soundName: String(setting("sound", "off"))
  readonly property string soundCustom: String(setting("soundCustomPath", ""))
  readonly property string chatTheme: String(setting("chatTheme", "system"))

  property var openCards: []
  property string pulseConv: ""

  function setting(name, fallback) {
    var s = settings || (service ? service.settings : {})
    var v = s ? s[name] : undefined
    return v === undefined || v === null ? fallback : v
  }

  function theme() {
    var m = {
      paper: { bg: "#f4efe4", fg: "#2a241c", accent: "#8a6a3a", unread: "#c45c26" },
      ink: { bg: "#12141a", fg: "#e8e6e1", accent: "#8aa0b8", unread: "#d4a017" },
      moss: { bg: "#1b241c", fg: "#dce8d8", accent: "#6b8f71", unread: "#c4b14a" },
      dusk: { bg: "#1a1524", fg: "#efe6f4", accent: "#a78bce", unread: "#e07a5f" },
      ember: { bg: "#1c1410", fg: "#f3e6d8", accent: "#d4764e", unread: "#e8b86d" }
    }
    return m[chatTheme] || { bg: "", fg: "", accent: "", unread: "" }
  }

  function ensureCard(conv) {
    if (!conv)
      return
    var i
    for (i = 0; i < openCards.length; i++) {
      if (openCards[i].conversation === conv)
        return
    }
    var next = openCards.slice()
    if (surfaceMode === "bundled") {
      next = [{ conversation: conv, monitor: "", x: 40, y: 80, pinned: false }]
    } else {
      next.push({ conversation: conv, monitor: "", x: 40 + next.length * 16, y: 80 + next.length * 16, pinned: false })
    }
    openCards = next
    service.sendOp({ op: "surface.set", conversation: conv, monitor: "", x: 40, y: 80, pinned: false })
  }

  function pin(conv, on) {
    var i, next = []
    for (i = 0; i < openCards.length; i++) {
      var c = openCards[i]
      if (c.conversation === conv)
        c = { conversation: c.conversation, monitor: c.monitor, x: c.x, y: c.y, pinned: !!on }
      next.push(c)
    }
    openCards = next
    service.sendOp({ op: "surface.set", conversation: conv, monitor: "", x: 0, y: 0, pinned: !!on })
  }

  function savePos(conv, mon, x, y, pinned) {
    service.sendOp({ op: "surface.set", conversation: conv, monitor: mon || "", x: x, y: y, pinned: !!pinned })
  }

  function playSound() {
    if (soundName === "off" || soundName === "")
      return
    var path = soundCustom
    if (soundName !== "custom")
      path = String(Qt.resolvedUrl("sounds/" + soundName + ".wav")).replace(/^file:\/\//, "")
    if (!path)
      return
    sndProc.command = ["paplay", path]
    sndProc.running = true
  }

  function desktopNotify() {
    if (!notifyDesk)
      return
    noteProc.command = ["notify-send", "-a", "OmaQ", "OmaQ", "New message"]
    noteProc.running = true
  }

  function onIncoming(conv) {
    ensureCard(conv)
    if (animateUnread)
      pulseConv = conv
    playSound()
    desktopNotify()
  }

  Connections {
    target: service
    function onLastChatTextChanged() {
      if (service.lastChatDir === "in")
        root.onIncoming(service.lastConversation)
    }
  }

  Process { id: sndProc }
  Process { id: noteProc }

  Variants {
    model: Quickshell.screens
    PanelWindow {
      id: overlay
      required property var modelData
      screen: modelData
      visible: true
      color: "transparent"
      exclusionMode: ExclusionMode.Ignore
      WlrLayershell.namespace: "omaq-card"
      WlrLayershell.layer: WlrLayer.Overlay
      WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
      anchors { top: true; bottom: true; left: true; right: true }
      mask: Region { item: cardColumn }

      Column {
        id: cardColumn
        x: 24
        y: 80
        spacing: Style.space(10)

        Repeater {
          model: root.openCards
          delegate: Item {
            id: card
            required property var modelData
            visible: !modelData.pinned && (modelData.monitor === "" || modelData.monitor === overlay.modelData.name)
            width: Style.space(280)
            height: Style.space(220)

            Pages.ChatPage {
              anchors.fill: parent
              service: root.service
              theme: root.theme()
              conversation: card.modelData.conversation
              pulseUnread: root.animateUnread && root.pulseConv === card.modelData.conversation
            }

            Row {
              anchors.top: parent.top
              anchors.right: parent.right
              spacing: 4
              Button { text: "Pin"; onClicked: root.pin(card.modelData.conversation, true) }
            }

            MouseArea {
              anchors.bottom: parent.bottom
              width: parent.width
              height: 16
              drag.target: card
              cursorShape: Qt.SizeAllCursor
              onReleased: root.savePos(card.modelData.conversation, overlay.modelData.name, card.x, card.y, false)
            }
          }
        }
      }
    }
  }

  PanelWindow {
    id: rightDock
    visible: root.notifyRight && service && service.lastChatText !== ""
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "omaq-dock"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    anchors { top: true; bottom: true; right: true }
    implicitWidth: Style.space(300)

    Pages.ChatPage {
      anchors.fill: parent
      anchors.margins: Style.space(8)
      service: root.service
      theme: root.theme()
      conversation: service ? service.lastConversation : ""
    }
  }

  Repeater {
    model: root.openCards
    FloatingWindow {
      id: pinWin
      required property var modelData
      visible: modelData.pinned
      title: "OmaQ"
      implicitWidth: 420
      implicitHeight: 360
      color: root.theme().bg || Color.background

      Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Button { text: "Unpin"; onClicked: root.pin(pinWin.modelData.conversation, false) }
        Pages.ChatPage {
          width: parent.width
          height: parent.height - 36
          service: root.service
          theme: root.theme()
          conversation: pinWin.modelData.conversation
          terminalLook: true
        }
      }
    }
  }
}
