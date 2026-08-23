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
  property string lastNotifiedMessageId: ""
  readonly property bool muted: service ? service.muted : false
  property var autoOpenByConversation: ({})
  property bool autoOpenLoaded: false
  property var pendingIncoming: []
  property bool autoOpenSavePending: false
  property var pendingAutoOpenToggles: []
  property var autoOpenPersisted: ({})
  property var autoOpenSaveSnapshot: ({})

  readonly property string autoOpenPath: service
    ? service.stateDir + "/auto-open.json"
    : (Quickshell.env("HOME") + "/.local/state/omaq/auto-open.json")
  readonly property var visualTokens: root.bar && "visualTokens" in root.bar
    ? root.bar.visualTokens : null
  readonly property var paletteState: visualTokens ? visualTokens.stateService : null
  readonly property string notificationConversation: service
    ? String(service.lastChatConv || service.lastConversation || "") : ""
  readonly property color headerNameColor: root.paletteColor("color03", root.theme().accent || Color.accent)
  readonly property color headerStatusColor: root.paletteColor("color02", root.theme().accent || Color.accent)

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

  function paletteColor(id, fallback) {
    if (root.paletteState && typeof root.paletteState.paletteColor === "function")
      return root.paletteState.paletteColor(id)
    return fallback
  }

  function friendData(conv) {
    var list = service ? (service.friends || []) : []
    var i
    for (i = 0; i < list.length; i++) {
      if (String(list[i].id) === String(conv))
        return list[i]
    }
    return null
  }

  function friendName(conv) {
    var friend = root.friendData(conv)
    return friend && friend.name ? String(friend.name) : ""
  }

  function friendLabel(conv, fallback) {
    var key = String(conv || "")
    var name = root.friendName(key)
    if (name)
      return name
    if (fallback)
      return String(fallback)
    return key && key.charAt(0) !== "g" ? "Friend " + key : key
  }

  function friendAvatar(conv) {
    var friend = root.friendData(conv)
    return friend && friend.avatar ? String(friend.avatar) : ""
  }

  function friendOnline(conv) {
    var friend = root.friendData(conv)
    return !!(friend && friend.online)
  }

  function autoOpenFor(conv) {
    var key = String(conv || "")
    return key !== "" && key.charAt(0) !== "g"
      ? root.autoOpenByConversation[key] !== false : true
  }

  function cloneAutoOpen(value) {
    try { return JSON.parse(JSON.stringify(value || {})) } catch (e) { return ({}) }
  }

  function loadAutoOpen(raw) {
    var parsed
    try { parsed = JSON.parse(String(raw || "")) } catch (e) { parsed = null }
    if (!parsed || typeof parsed !== "object") {
      root.autoOpenByConversation = ({})
      root.autoOpenPersisted = ({})
      return
    }
    var users = parsed.users && typeof parsed.users === "object" ? parsed.users : parsed
    root.autoOpenByConversation = users || ({})
    root.autoOpenPersisted = root.cloneAutoOpen(root.autoOpenByConversation)
  }

  function queueAutoOpenSave() {
    if (root.autoOpenSavePending)
      return
    root.autoOpenSavePending = true
    root.autoOpenSaveSnapshot = root.cloneAutoOpen(root.autoOpenByConversation)
    autoOpenFile.setText(JSON.stringify({ version: 1, users: root.autoOpenSaveSnapshot }, null, 2) + "\n")
  }

  function finishAutoOpenSave() {
    root.autoOpenSavePending = false
    root.autoOpenPersisted = root.cloneAutoOpen(root.autoOpenSaveSnapshot)
    if (JSON.stringify(root.autoOpenByConversation) !== JSON.stringify(root.autoOpenSaveSnapshot))
      root.queueAutoOpenSave()
  }

  function failAutoOpenSave() {
    root.autoOpenSavePending = false
    root.autoOpenByConversation = root.cloneAutoOpen(root.autoOpenPersisted)
  }

  function finishAutoOpenLoad(raw) {
    root.loadAutoOpen(raw)
    root.autoOpenLoaded = true
    var toggles = root.pendingAutoOpenToggles.slice()
    root.pendingAutoOpenToggles = []
    for (var t = 0; t < toggles.length; t++)
      root.toggleAutoOpen(toggles[t])
    var queued = root.pendingIncoming.slice()
    root.pendingIncoming = []
    for (var i = 0; i < queued.length; i++)
      root.onIncoming(queued[i].conversation, queued[i])
  }

  function toggleAutoOpen(conv) {
    var key = String(conv || "")
    if (!key || key.charAt(0) === "g")
      return
    if (!root.autoOpenLoaded) {
      if (root.pendingAutoOpenToggles.indexOf(key) === -1)
        root.pendingAutoOpenToggles.push(key)
      return
    }
    var next = {}
    var current
    for (current in root.autoOpenByConversation)
      next[current] = root.autoOpenByConversation[current]
    next[key] = !root.autoOpenFor(key)
    root.autoOpenByConversation = next
    root.queueAutoOpenSave()
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
        return false
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
    return true
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

  function toggleMute() {
    if (root.service && typeof root.service.toggleMute === "function")
      root.service.toggleMute()
    if (root.muted)
      sndProc.running = false
  }

  function playSound() {
    if (root.muted || soundName === "off" || soundName === "")
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

  function onIncoming(conv, snapshot) {
    var event = snapshot || ({})
    var key = String(event.conversation || conv || "")
    var messageId = event.id !== undefined
      ? String(event.id || "") : (service ? String(service.lastChatId || "") : "")
    var messageDir = event.dir !== undefined
      ? String(event.dir || "") : (service ? String(service.lastChatDir || "") : "")
    var messageText = event.text !== undefined
      ? String(event.text || "") : (service ? String(service.lastChatText || "") : "")
    if (!service || !key || messageDir !== "in" || !messageText)
      return
    if (!root.autoOpenLoaded) {
      if (messageId) {
        var queuedIndex
        for (queuedIndex = 0; queuedIndex < root.pendingIncoming.length; queuedIndex++) {
          if (root.pendingIncoming[queuedIndex].id === messageId)
            return
        }
        root.pendingIncoming.push({ conversation: key, id: messageId, text: messageText, dir: messageDir })
      }
      return
    }
    if (!messageId || key + "|" + messageId === root.lastNotifiedMessageId)
      return
    root.lastNotifiedMessageId = key + "|" + messageId
    if (root.autoOpenFor(key))
      ensureCard(key)
    if (animateUnread)
      pulseConv = key
    playSound()
    if (root.autoOpenFor(key))
      desktopNotify()
  }

  Connections {
    target: service
    function handleIncoming() {
      root.onIncoming(service.lastChatConv || service.lastConversation, {
        conversation: service.lastChatConv || service.lastConversation,
        id: service.lastChatId,
        text: service.lastChatText,
        dir: service.lastChatDir
      })
    }
    function onMessageTickChanged() { handleIncoming() }
    function onSurfacesTickChanged() { root.restoreSurfaces() }
  }

  FileView {
    id: autoOpenFile
    path: root.autoOpenPath
    watchChanges: true
    atomicWrites: true
    printErrors: false
    onFileChanged: if (!root.autoOpenSavePending) reload()
    onLoaded: if (!root.autoOpenSavePending) root.finishAutoOpenLoad(text())
    onLoadFailed: if (!root.autoOpenSavePending) root.finishAutoOpenLoad("")
    onSaved: root.finishAutoOpenSave()
    onSaveFailed: {
      console.warn("OmaQ: could not save per-contact auto-open settings")
      root.failAutoOpenSave()
    }
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
              peerName: root.friendLabel(card.modelData.conversation)
              peerAvatar: root.friendAvatar(card.modelData.conversation)
              peerAvatarRevision: root.service ? root.service.avatarTick : 0
              peerOnline: root.friendOnline(card.modelData.conversation)
              peerNameColor: root.headerNameColor
              peerStatusColor: root.headerStatusColor
              autoOpenEnabled: root.autoOpenFor(card.modelData.conversation)
              onAutoOpenToggled: root.toggleAutoOpen(card.modelData.conversation)
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
    visible: root.notifyRight && service && service.lastChatText !== "" &&
      root.autoOpenFor(root.notificationConversation)
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
        conversation: root.notificationConversation
        peerName: root.friendLabel(root.notificationConversation)
        peerAvatar: root.friendAvatar(root.notificationConversation)
        peerAvatarRevision: root.service ? root.service.avatarTick : 0
        peerOnline: root.friendOnline(root.notificationConversation)
        peerNameColor: root.headerNameColor
        peerStatusColor: root.headerStatusColor
        autoOpenEnabled: root.autoOpenFor(root.notificationConversation)
        onAutoOpenToggled: root.toggleAutoOpen(root.notificationConversation)
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
            text: "Auto-open"
            selected: pinPage.autoOpenEnabled
            tooltipText: "Open new messages automatically"
            onClicked: pinPage.autoOpenToggled()
          }
          SurfaceBtn {
            text: root.muted ? "Unmute" : "Mute"
            selected: root.muted
            tooltipText: "Mute notification sound"
            onClicked: root.toggleMute()
          }
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
          peerName: root.friendLabel(pinWin.modelData ? pinWin.modelData.conversation : "",
            pinWin.modelData ? pinWin.modelData.name : "")
          peerAvatarRevision: root.service ? root.service.avatarTick : 0
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
          peerOnline: root.friendOnline(pinWin.modelData ? pinWin.modelData.conversation : "")
          peerNameColor: root.headerNameColor
          peerStatusColor: root.headerStatusColor
          autoOpenEnabled: root.autoOpenFor(pinWin.modelData ? pinWin.modelData.conversation : "")
          onAutoOpenToggled: root.toggleAutoOpen(pinWin.modelData ? pinWin.modelData.conversation : "")
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
          text: root.muted ? "Unmute" : "Mute"
          selected: root.muted
          tooltipText: "Mute notification sound"
          onClicked: root.toggleMute()
        }
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
