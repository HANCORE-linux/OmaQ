import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui
import qs.Commons
import "pages" as Pages
import "Model.js" as Model

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
  property bool demoOpen: false

  function setting(name, fallback) {
    var s = settings || (service ? service.settings : {})
    var v = s ? s[name] : undefined
    return v === undefined || v === null ? fallback : v
  }

  function theme() {
    if (chatTheme === "system" || chatTheme === "")
      return { bg: "", fg: "", accent: "", unread: "", name: "System", colors: [] }
    return Model.themeFor(chatTheme)
  }

  function openDemo() {
    if (!root.demoOpen) {
      root.demoOpen = true
      Qt.callLater(function() {
        if (demoPage)
          demoPage.resetDemo()
      })
    }
  }

  function closeDemo() {
    root.demoOpen = false
  }

  function floatOmaQWindows() {
    floatOmaQTimer.restart()
  }

  Component.onCompleted: root.floatOmaQWindows()

  function ensureCard(conv) {
    if (!conv)
      return
    var i
    for (i = 0; i < openCards.length; i++) {
      if (openCards[i].conversation === conv) {
        if (!openCards[i].pinned)
          root.pin(conv, true)
        root.floatOmaQWindows()
        return
      }
    }
    var next = openCards.slice()
    if (surfaceMode === "bundled") {
      next = [{ conversation: conv, monitor: "", x: 40, y: 80, pinned: true }]
    } else {
      next.push({ conversation: conv, monitor: "", x: 40 + next.length * 16, y: 80 + next.length * 16, pinned: true })
    }
    openCards = next
    service.sendOp({ op: "surface.set", conversation: conv, monitor: "", x: 40, y: 80, pinned: true })
    root.floatOmaQWindows()
  }

  function dismissCard(conv) {
    var i, next = []
    for (i = 0; i < openCards.length; i++) {
      if (openCards[i].conversation !== conv)
        next.push(openCards[i])
    }
    openCards = next
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
  Process {
    id: floatOmaQProc
    command: [String(Qt.resolvedUrl("scripts/float-omaq.sh")).replace(/^file:\/\//, "")]
    running: false
  }
  Timer {
    id: floatOmaQTimer
    interval: 80
    repeat: false
    onTriggered: {
      floatOmaQProc.running = false
      floatOmaQProc.running = true
    }
  }

  function overlayVisibleOn(screenName) {
    var i, c
    for (i = 0; i < openCards.length; i++) {
      c = openCards[i]
      if (c.pinned)
        continue
      if (c.monitor === "" || c.monitor === screenName)
        return true
    }
    return false
  }

  Variants {
    model: Quickshell.screens
    PanelWindow {
      id: overlay
      required property var modelData
      screen: modelData
      visible: root.overlayVisibleOn(modelData.name)
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
      title: "OmaQ chat"
      implicitWidth: 420
      implicitHeight: 360
      color: root.theme().bg || Color.background
      property bool everShown: false

      onVisibleChanged: {
        if (visible) {
          pinWin.everShown = true
          root.floatOmaQWindows()
          return
        }
        if (pinWin.everShown && pinWin.modelData && pinWin.modelData.conversation)
          root.dismissCard(pinWin.modelData.conversation)
      }

      Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Button { text: "Close"; onClicked: root.dismissCard(pinWin.modelData.conversation) }
        Pages.ChatPage {
          width: parent.width
          height: parent.height - 36
          service: root.service
          theme: root.theme()
          conversation: pinWin.modelData.conversation
        }
      }
    }
  }

  FloatingWindow {
    id: demoWin
    visible: root.demoOpen
    title: "OmaQ demo"
    implicitWidth: 420
    implicitHeight: 480
    minimumSize: Qt.size(320, 360)
    color: root.theme().bg || Color.background

    onVisibleChanged: {
      if (visible) {
        root.floatOmaQWindows()
        Qt.callLater(function() {
          if (demoPage)
            demoPage.resetDemo()
        })
      } else if (root.demoOpen) {
        root.demoOpen = false
      }
    }

    Pages.ChatPage {
      id: demoPage
      anchors.fill: parent
      anchors.margins: Style.space(8)
      demo: true
      service: root.service
      theme: root.theme()
      conversation: "demo"
    }
  }
}
