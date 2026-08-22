import QtQuick
import QtQuick.Layouts
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
  readonly property string soundName: String(setting("sound", "icq-message"))
  readonly property string soundCustom: String(setting("soundCustomPath", ""))
  readonly property string chatTheme: String(setting("chatTheme", "system"))

  property var openCards: []
  property bool surfacesHydrated: false
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

  function friendName(conv) {
    var list = service ? (service.friends || []) : []
    var i
    for (i = 0; i < list.length; i++)
      if (String(list[i].id) === String(conv))
        return list[i].name || ""
    return ""
  }

  function restoreSurfaces() {
    var persisted = service ? (service.surfaces || []) : []
    var current = openCards.slice()
    var next = []
    var i, j, saved, found
    for (i = 0; i < current.length; i++) {
      saved = null
      for (j = 0; j < persisted.length; j++) {
        if (String(persisted[j].conversation) === String(current[i].conversation)) {
          saved = persisted[j]
          break
        }
      }
      next.push(saved ? {
        conversation: current[i].conversation,
        monitor: saved.monitor || "",
        x: isFinite(Number(saved.x)) ? Number(saved.x) : 40,
        y: isFinite(Number(saved.y)) ? Number(saved.y) : 80,
        pinned: !!saved.pinned,
        name: current[i].name || friendName(current[i].conversation)
      } : current[i])
    }
    for (i = 0; i < persisted.length; i++) {
      if (!persisted[i].pinned || !persisted[i].conversation)
        continue
      found = false
      for (j = 0; j < next.length; j++)
        if (String(next[j].conversation) === String(persisted[i].conversation))
          found = true
      if (!found)
        next.push({
          conversation: String(persisted[i].conversation),
          monitor: persisted[i].monitor || "",
          x: isFinite(Number(persisted[i].x)) ? Number(persisted[i].x) : 40,
          y: isFinite(Number(persisted[i].y)) ? Number(persisted[i].y) : 80,
          pinned: true,
          name: friendName(persisted[i].conversation)
        })
    }
    if (surfaceMode === "bundled" && next.length > 1)
      next = [next[0]]
    openCards = next
    surfacesHydrated = true
    root.floatOmaQWindows()
  }

  Component.onCompleted: {
    root.floatOmaQWindows()
    if (service)
      service.sendOp({ op: "surface.list" })
  }

  component SurfaceBtn: Button {
    foreground: root.theme().fg || Color.foreground
    accent: root.theme().accent || Color.accent
    fontFamily: Style.font.family
    radius: Style.cornerRadius
    iconSize: Style.font.icon
    fontSize: Style.font.body
    horizontalPadding: Style.space(6)
    verticalPadding: Style.space(4)
  }

  component CallToolbar: Row {
    id: toolbar
    required property var page
    spacing: Style.space(4)

    SurfaceBtn {
      visible: toolbar.page && !toolbar.page.inCall && !toolbar.page.incoming
      iconText: "󰏲"
      tooltipText: "Call"
      onClicked: toolbar.page.startCall()
    }
    SurfaceBtn {
      visible: toolbar.page && toolbar.page.incoming && !toolbar.page.inCall
      iconText: "󰏴"
      tooltipText: "Answer"
      bordered: true
      selected: true
      onClicked: toolbar.page.answerCall()
    }
    SurfaceBtn {
      visible: toolbar.page && toolbar.page.inCall
      iconText: "󰖂"
      tooltipText: "Hang up"
      bordered: true
      selected: true
      onClicked: toolbar.page.hangUp()
    }
  }

  function ensureCard(conv, name) {
    if (!conv)
      return
    var i
    var label = name ? String(name) : ""
    for (i = 0; i < openCards.length; i++) {
      if (openCards[i].conversation === conv) {
        if (label && openCards[i].name !== label) {
          var upd = openCards.slice()
          upd[i] = { conversation: conv, monitor: openCards[i].monitor, x: openCards[i].x, y: openCards[i].y, pinned: true, name: label }
          openCards = upd
        }
        if (!openCards[i].pinned)
          root.pin(conv, true)
        root.floatOmaQWindows()
        return
      }
    }
    var next = openCards.slice()
    var card = { conversation: conv, monitor: "", x: 40 + next.length * 16, y: 80 + next.length * 16, pinned: true, name: label }
    if (surfaceMode === "bundled")
      next = [{ conversation: conv, monitor: "", x: 40, y: 80, pinned: true, name: label }]
    else
      next.push(card)
    openCards = next
    if (surfacesHydrated)
      service.sendOp({ op: "surface.set", conversation: conv, monitor: "", x: card.x, y: card.y, pinned: true })
    root.floatOmaQWindows()
  }

  function dismissCard(conv) {
    var i, next = [], removed = null
    for (i = 0; i < openCards.length; i++) {
      if (openCards[i].conversation !== conv)
        next.push(openCards[i])
      else
        removed = openCards[i]
    }
    openCards = next
    if (removed)
      service.setSurface(conv, removed.monitor, removed.x, removed.y, false)
  }

  function pin(conv, on) {
    var i, next = [], saved = null
    for (i = 0; i < openCards.length; i++) {
      var c = openCards[i]
      if (c.conversation === conv) {
        saved = c
        c = { conversation: c.conversation, monitor: c.monitor, x: c.x, y: c.y, pinned: !!on, name: c.name || "" }
      }
      next.push(c)
    }
    openCards = next
    if (saved)
      service.setSurface(conv, saved.monitor, saved.x, saved.y, !!on)
  }

  function savePos(conv, mon, x, y, pinned) {
    service.sendOp({ op: "surface.set", conversation: conv, monitor: mon || "", x: x, y: y, pinned: !!pinned })
  }

  function playSound() {
    if (soundName === "off" || soundName === "")
      return
    var path = soundCustom
    if (soundName === "icq-message")
      path = String(Qt.resolvedUrl("sounds/icq-message.mp3")).replace(/^file:\/\//, "")
    else if (soundName !== "custom")
      path = String(Qt.resolvedUrl("sounds/" + soundName + ".wav")).replace(/^file:\/\//, "")
    if (!path)
      return
    sndProc.command = [
      "bash", "-c",
      "if command -v ffplay >/dev/null 2>&1; then exec ffplay -nodisp -autoexit -loglevel quiet \"$1\"; elif command -v mpv >/dev/null 2>&1; then exec mpv --no-video --really-quiet \"$1\"; elif command -v pw-play >/dev/null 2>&1; then exec pw-play \"$1\"; else exit 127; fi",
      "omaq-message-sound", path
    ]
    sndProc.running = false
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
    function handleIncoming() {
      if (service.lastChatDir === "in")
        root.onIncoming(service.lastChatConv || service.lastConversation)
    }
    function onMessageTickChanged() { handleIncoming() }
    function onSurfacesTickChanged() { root.restoreSurfaces() }
  }

  Process { id: sndProc }
  Process { id: noteProc }
  Process {
    id: floatOmaQProc
    command: [String(Qt.resolvedUrl("scripts/float-omaq.sh")).replace(/^file:\/\//, "")]
    running: true
  }
  Timer {
    id: floatOmaQTimer
    interval: 0
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
              id: cardPage
              anchors.fill: parent
              anchors.topMargin: Style.space(30)
              service: root.service
              theme: root.theme()
              conversation: card.modelData.conversation
              pulseUnread: root.animateUnread && root.pulseConv === card.modelData.conversation
            }

            RowLayout {
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.top: parent.top
              spacing: Style.space(4)
              z: 10
              CallToolbar { page: cardPage }
              Item { Layout.fillWidth: true }
              SurfaceBtn {
                text: "Pin"
                onClicked: root.pin(card.modelData.conversation, true)
              }
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

    ColumnLayout {
      anchors.fill: parent
      anchors.margins: Style.space(8)
      spacing: Style.space(4)

      RowLayout {
        Layout.fillWidth: true
        CallToolbar { page: rightDockPage }
        Item { Layout.fillWidth: true }
      }

      Pages.ChatPage {
        id: rightDockPage
        Layout.fillWidth: true
        Layout.fillHeight: true
        service: root.service
        theme: root.theme()
        conversation: service ? service.lastConversation : ""
      }
    }
  }

  Instantiator {
    model: root.openCards
    delegate: FloatingWindow {
      id: pinWin
      required property var modelData
      // Keep the map-time title stable so Hyprland can apply the floating rule before map.
      title: "OmaQ chat"
      implicitWidth: 420
      implicitHeight: 360
      color: root.theme().bg || Color.background
      property bool everShown: false
      property bool closing: false

      onVisibleChanged: {
        if (visible) {
          pinWin.everShown = true
          pinWin.closing = false
          root.floatOmaQWindows()
          Qt.callLater(function() {
            if (pinWin.visible)
              root.floatOmaQWindows()
          })
          return
        }
        if (!pinWin.closing && pinWin.everShown && pinWin.modelData && pinWin.modelData.conversation)
          root.dismissCard(pinWin.modelData.conversation)
      }

      ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.space(8)
        spacing: Style.space(6)

        RowLayout {
          Layout.fillWidth: true
          CallToolbar { page: pinPage }
          Item { Layout.fillWidth: true }
          SurfaceBtn {
            text: "Close"
            onClicked: {
              var conv = String(pinWin.modelData.conversation)
              pinWin.closing = true
              pinWin.visible = false
              root.dismissCard(conv)
            }
          }
        }

        Pages.ChatPage {
          id: pinPage
          Layout.fillWidth: true
          Layout.fillHeight: true
          service: root.service
          theme: root.theme()
          conversation: pinWin.modelData.conversation
          peerName: pinWin.modelData && pinWin.modelData.name ? pinWin.modelData.name : ""
          peerAvatar: {
            var f = root.service && root.service.friends
            var id = pinWin.modelData ? String(pinWin.modelData.conversation) : ""
            var i
            if (!f || !id)
              return ""
            for (i = 0; i < f.length; i++) {
              if (String(f[i].id) === id)
                return f[i].avatar || ""
            }
            return ""
          }
          peerOnline: {
            var f = root.service && root.service.friends
            var id = pinWin.modelData ? String(pinWin.modelData.conversation) : ""
            var i
            if (!f || !id)
              return false
            for (i = 0; i < f.length; i++) {
              if (String(f[i].id) === id)
                return !!f[i].online
            }
            return false
          }
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

    ColumnLayout {
      anchors.fill: parent
      anchors.margins: Style.space(8)
      spacing: Style.space(4)

      RowLayout {
        Layout.fillWidth: true
        CallToolbar { page: demoPage }
        Item { Layout.fillWidth: true }
        SurfaceBtn {
          text: "Close"
          onClicked: root.closeDemo()
        }
      }

      Pages.ChatPage {
        id: demoPage
        Layout.fillWidth: true
        Layout.fillHeight: true
        demo: true
        service: root.service
        theme: root.theme()
        conversation: "demo"
      }
    }
  }
}
