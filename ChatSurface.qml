import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui
import qs.Commons
import "pages" as Pages
import "Model.js" as Model
import "." as OmaQ

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
  readonly property real messageScale: {
    var value = Number(setting("messageScale", 1.0))
    return [0.9, 1.0, 1.1, 1.2, 1.4].indexOf(value) >= 0 ? value : 1.0
  }
  readonly property bool formatToolbarEnabled: !!setting("formatToolbar", false)
  signal formatToolbarToggled(bool enabled)

  property var openCards: []
  property bool surfacesHydrated: false
  property string pulseConv: ""
  property bool demoOpen: false
  property string lastNotifiedMessageId: ""
  property string focusConversation: ""
  property int focusRequestTick: 0
  readonly property bool muted: service ? service.muted : false
  readonly property bool callToneNeeded: service && !service.callToneSuppressed &&
    (service.lastCallState === "incoming" || service.lastCallState === "ringing")
  readonly property bool callTonePlaying: OmaQ.CallTone.playing
  property string callToneOwner: ""
  property var autoOpenByConversation: ({})
  property bool autoOpenLoaded: false
  property bool autoOpenUnavailable: false
  property string autoOpenWarning: ""
  property var pendingIncoming: []
  property bool autoOpenSavePending: false
  property var pendingAutoOpenToggles: []
  property var autoOpenPersisted: ({})
  property var autoOpenSaveSnapshot: ({})
  property bool legacyAutoOpenMigrationPending: false
  property bool legacyAutoOpenMigrationSaving: false

  readonly property string autoOpenPath: service && service.identityFingerprint
    ? service.stateDir + "/auto-open." + service.identityFingerprint + ".json"
    : ""
  readonly property string legacyAutoOpenPath: service
    ? service.stateDir + "/auto-open.json" : ""
  readonly property var autoOpenFile: autoOpenLoader.item
  readonly property var visualTokens: root.bar && "visualTokens" in root.bar
    ? root.bar.visualTokens : null
  readonly property var paletteState: visualTokens ? visualTokens.stateService : null
  readonly property color receiptSentColor: root.paletteColor("color05", root.theme().accent || Color.accent)
  readonly property color receiptDeliveredColor: root.paletteColor("color04", root.theme().fg || Color.foreground)
  readonly property color receiptReadColor: root.paletteColor("color03", root.theme().accent || Color.accent)
  readonly property string notificationConversation: service
    ? String(service.lastChatConv || service.lastConversation || "") : ""
  readonly property color headerNameColor: root.paletteColor("color03", root.theme().accent || Color.accent)
  readonly property color headerStatusColor: root.paletteColor("color02", root.theme().accent || Color.accent)

  onCallToneNeededChanged:
    OmaQ.CallTone.setRequested(root.callToneOwner, root.callToneNeeded)

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

  function requestChatFocus(conv) {
    var key = String(conv || "")
    if (!key)
      return
    root.focusConversation = key
    root.focusRequestTick = root.focusRequestTick + 1
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
    if (key.charAt(0) === "g" && root.service &&
        typeof root.service.groupName === "function") {
      var groupRevision = Number(root.service.groupsTick || 0)
      return groupRevision >= 0 ? root.service.groupName(key) : "Group"
    }
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
    var key = String(conv || "")
    if (key.charAt(0) === "g" && root.service &&
        typeof root.service.groupOnlineCount === "function") {
      var groupRevision = Number(root.service.groupsTick || 0)
      return groupRevision >= 0 && root.service.groupOnlineCount(key) > 0
    }
    var friend = root.friendData(key)
    return !!(friend && friend.online)
  }

  function conversationKeyOk(conv) {
    var key = String(conv || "")
    return (/^(0|[1-9][0-9]*)$/.test(key) && key.length <= 10 &&
            Number(key) <= 4294967295) || /^g:[0-9a-f]{64}$/.test(key)
  }

  function autoOpenFor(conv) {
    var key = String(conv || "")
    if (root.autoOpenUnavailable || !root.conversationKeyOk(key))
      return false
    return root.autoOpenByConversation[key] !== false
  }

  function cloneAutoOpen(value) {
    try { return JSON.parse(JSON.stringify(value || {})) } catch (e) { return ({}) }
  }

  function loadAutoOpen(raw) {
    var parsed
    try { parsed = JSON.parse(String(raw || "")) } catch (e) { return false }
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed))
      return false
    var users
    if (parsed.version !== undefined) {
      if (typeof parsed.version !== "number" || parsed.version !== 1 || !parsed.users ||
          typeof parsed.users !== "object" || Array.isArray(parsed.users))
        return false
      users = parsed.users
    } else {
      users = parsed
    }
    var key
    for (key in users) {
      if (!root.conversationKeyOk(key) || typeof users[key] !== "boolean")
        return false
    }
    root.autoOpenUnavailable = false
    root.autoOpenWarning = ""
    root.autoOpenByConversation = users
    root.autoOpenPersisted = root.cloneAutoOpen(root.autoOpenByConversation)
    return true
  }

  function queueAutoOpenSave() {
    if (root.autoOpenSavePending || !root.autoOpenFile)
      return
    root.autoOpenSavePending = true
    root.autoOpenSaveSnapshot = root.cloneAutoOpen(root.autoOpenByConversation)
    root.autoOpenFile.setText(JSON.stringify({ version: 1, users: root.autoOpenSaveSnapshot }, null, 2) + "\n")
  }

  function finishAutoOpenSave() {
    root.autoOpenSavePending = false
    if (root.legacyAutoOpenMigrationSaving) {
      root.legacyAutoOpenMigrationSaving = false
      if (root.service)
        root.service.confirmAutoOpenMigration()
    }
    root.autoOpenPersisted = root.cloneAutoOpen(root.autoOpenSaveSnapshot)
    if (JSON.stringify(root.autoOpenByConversation) !== JSON.stringify(root.autoOpenSaveSnapshot))
      root.queueAutoOpenSave()
  }

  function failAutoOpenSave() {
    root.autoOpenSavePending = false
    root.autoOpenByConversation = root.cloneAutoOpen(root.autoOpenPersisted)
    root.autoOpenWarning = "Auto-open settings could not be saved."
  }

  function failAutoOpenLoad(error) {
    root.autoOpenUnavailable = true
    root.autoOpenWarning = "Auto-open settings could not be read; Auto-open is disabled."
    root.autoOpenByConversation = ({})
    root.autoOpenPersisted = ({})
    root.autoOpenLoaded = true
    root.legacyAutoOpenMigrationPending = false
    root.legacyAutoOpenMigrationSaving = false
    root.pendingAutoOpenToggles = []
    var queued = root.pendingIncoming.slice()
    root.pendingIncoming = []
    for (var i = 0; i < queued.length; i++)
      root.onIncoming(queued[i].conversation, queued[i])
  }

  function loadLegacyAutoOpen() {
    if (root.legacyAutoOpenMigrationPending)
      return
    root.legacyAutoOpenMigrationPending = true
  }

  function finishLegacyAutoOpen(raw) {
    if (!root.finishAutoOpenLoad(raw))
      return
    root.legacyAutoOpenMigrationSaving = true
    root.queueAutoOpenSave()
    Qt.callLater(function() { root.legacyAutoOpenMigrationPending = false })
  }

  function finishAutoOpenLoad(raw) {
    if (!root.loadAutoOpen(raw)) {
      root.failAutoOpenLoad(FileViewError.Unknown)
      return false
    }
    root.autoOpenLoaded = true
    var toggles = root.pendingAutoOpenToggles.slice()
    root.pendingAutoOpenToggles = []
    for (var t = 0; t < toggles.length; t++)
      root.toggleAutoOpen(toggles[t])
    var queued = root.pendingIncoming.slice()
    root.pendingIncoming = []
    for (var i = 0; i < queued.length; i++)
      root.onIncoming(queued[i].conversation, queued[i])
    return true
  }

  function toggleAutoOpen(conv) {
    var key = String(conv || "")
    if (!root.conversationKeyOk(key))
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

  function closeRemovedGroup(conv) {
    var key = String(conv || "")
    if (!key)
      return
    for (var i = 0; i < openCards.length; i++) {
      if (String(openCards[i].conversation) === key) {
        root.dismissCard(key)
        return
      }
    }
    var persisted = service ? (service.surfaces || []) : []
    for (var j = 0; j < persisted.length; j++) {
      if (String(persisted[j].conversation) === key) {
        service.setSurface(key, persisted[j].monitor || "", Number(persisted[j].x || 0),
          Number(persisted[j].y || 0), false)
        return
      }
    }
  }

  function reconcileOpenGroups() {
    if (!service || !service.groupsReady)
      return
    var current = openCards.slice()
    for (var i = 0; i < current.length; i++)
      if (String(current[i].conversation || "").charAt(0) === "g" &&
          !service.groupById(current[i].conversation))
        root.closeRemovedGroup(current[i].conversation)
  }

  function restoreSurfaces() {
    var persisted = service ? (service.surfaces || []) : []
    var current = openCards.slice()
    var next = []
    var i, j, saved, found
    for (i = 0; i < current.length; i++) {
      if (String(current[i].conversation).charAt(0) === "g" && service &&
          service.groupsReady && !service.groupById(current[i].conversation)) {
        root.closeRemovedGroup(current[i].conversation)
        continue
      }
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
      if (String(persisted[i].conversation).charAt(0) === "g" && service &&
          service.groupsReady && !service.groupById(persisted[i].conversation)) {
        service.setSurface(String(persisted[i].conversation), persisted[i].monitor || "",
          Number(persisted[i].x || 0), Number(persisted[i].y || 0), false)
        continue
      }
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
    root.callToneOwner = OmaQ.CallTone.acquireOwner()
    OmaQ.CallTone.setRequested(root.callToneOwner, root.callToneNeeded)
    root.floatOmaQWindows()
    if (service)
      service.sendOp({ op: "surface.list" })
  }
  Component.onDestruction:
    OmaQ.CallTone.setRequested(root.callToneOwner, false)

  component SurfaceBtn: Button {
    id: surfaceButton
    property string helpText: ""
    tooltipText: ""
    Accessible.name: helpText !== "" ? helpText : text
    foreground: root.theme().fg || Color.foreground
    accent: root.theme().accent || Color.accent
    fontFamily: Style.font.family
    radius: Style.cornerRadius
    iconSize: Style.font.icon
    fontSize: Style.font.body
    horizontalPadding: Style.space(6)
    verticalPadding: Style.space(4)
    focusable: true

    Controls.ToolTip {
      id: surfaceTooltip
      visible: (surfaceButton.hot || surfaceButton.activeFocus) &&
        surfaceButton.helpText !== ""
      text: surfaceButton.helpText
      delay: 450
      timeout: 2600
      padding: Style.space(5)
      background: Rectangle {
        radius: Style.cornerRadius
        color: Qt.darker(root.theme().bg || Color.background, 1.08)
        border.color: Qt.rgba(surfaceButton.foreground.r,
                              surfaceButton.foreground.g,
                              surfaceButton.foreground.b, 0.24)
        border.width: 1
      }
      contentItem: Text {
        text: surfaceTooltip.text
        color: surfaceButton.foreground
        font.family: Style.font.family
        font.pixelSize: Style.font.bodySmall
        renderType: Text.QtRendering
      }
    }
  }

  component CallToolbar: Row {
    id: toolbar
    required property var page
    visible: !!page && page.directConversation
    spacing: Style.space(4)

    SurfaceBtn {
      visible: toolbar.page && !toolbar.page.inCall && !toolbar.page.incoming
      iconText: "call"
      fontFamily: "Material Symbols Rounded"
      helpText: "Start call with " + (toolbar.page ? toolbar.page.peerName : "contact")
      onClicked: toolbar.page.startCall()
    }
    SurfaceBtn {
      visible: toolbar.page && toolbar.page.incoming && !toolbar.page.inCall
      iconText: "call"
      fontFamily: "Material Symbols Rounded"
      helpText: "Answer call from " + (toolbar.page ? toolbar.page.peerName : "contact")
      bordered: true
      selected: true
      onClicked: toolbar.page.answerCall()
    }
    SurfaceBtn {
      visible: toolbar.page && toolbar.page.incoming && !toolbar.page.inCall
      iconText: "call_end"
      fontFamily: "Material Symbols Rounded"
      helpText: "Decline call from " + (toolbar.page ? toolbar.page.peerName : "contact")
      bordered: true
      onClicked: toolbar.page.hangUp()
    }
    SurfaceBtn {
      visible: toolbar.page && toolbar.page.inCall
      iconText: "call_end"
      fontFamily: "Material Symbols Rounded"
      helpText: "End call with " + (toolbar.page ? toolbar.page.peerName : "contact")
      bordered: true
      selected: true
      onClicked: toolbar.page.hangUp()
    }
    Text {
      visible: toolbar.page && toolbar.page.callActive
      anchors.verticalCenter: parent.verticalCenter
      text: toolbar.page ? toolbar.page.callDurationText : "0:00"
      color: root.theme().fg || Color.foreground
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
      font.features: ({ "tnum": 1 })
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

  function packagedSoundFile(name) {
    var selectedSound = String(name || "")
    if (["qq", "wechat", "skype", "msn", "aurora", "crystal", "ripple", "glow", "halo"].indexOf(selectedSound) >= 0)
      return selectedSound + ".oga"
    return selectedSound + ".wav"
  }

  function playNamedSound(name) {
    var selectedSound = String(name || "")
    if (root.muted || selectedSound === "off" || selectedSound === "")
      return
    var path = soundCustom
    if (selectedSound === "icq-message")
      path = String(Qt.resolvedUrl("sounds/icq-message.mp3")).replace(/^file:\/\//, "")
    else if (selectedSound !== "custom")
      path = String(Qt.resolvedUrl("sounds/" + root.packagedSoundFile(selectedSound))).replace(/^file:\/\//, "")
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

  function playSound() {
    root.playNamedSound(root.soundName)
  }

  function previewSound(name) {
    root.playNamedSound(name)
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
    function onGroupsTickChanged() { root.reconcileOpenGroups() }
    function onRemovedGroupTickChanged() { root.closeRemovedGroup(service.lastRemovedGroup) }
    function onIdentityTickChanged() {
      root.openCards = []
      root.pendingIncoming = []
      root.focusConversation = ""
      root.autoOpenByConversation = ({})
      root.autoOpenPersisted = ({})
      root.autoOpenUnavailable = false
      root.autoOpenWarning = ""
      root.autoOpenSaveSnapshot = ({})
      root.autoOpenSavePending = false
      root.legacyAutoOpenMigrationPending = false
      root.legacyAutoOpenMigrationSaving = false
      root.pendingAutoOpenToggles = []
      root.autoOpenLoaded = false
    }
  }

  Loader {
    id: autoOpenLoader
    active: !!(root.service && root.service.identityFingerprint)
    sourceComponent: Component {
      FileView {
        path: root.autoOpenPath
        watchChanges: true
        atomicWrites: true
        printErrors: false
        onFileChanged: if (!root.autoOpenSavePending) reload()
        onLoaded: if (!root.autoOpenSavePending) root.finishAutoOpenLoad(text())
        onLoadFailed: function(error) {
          if (root.autoOpenSavePending)
            return
          if (error === FileViewError.FileNotFound)
            root.loadLegacyAutoOpen()
          else
            root.failAutoOpenLoad(error)
        }
        onSaved: root.finishAutoOpenSave()
        onSaveFailed: {
          console.warn("OmaQ: could not save per-contact auto-open settings")
          root.failAutoOpenSave()
        }
      }
    }
  }

  Loader {
    id: legacyAutoOpenLoader
    active: root.legacyAutoOpenMigrationPending && root.legacyAutoOpenPath !== ""
    sourceComponent: Component {
      FileView {
        path: root.legacyAutoOpenPath
        printErrors: false
        onLoaded: root.finishLegacyAutoOpen(text())
        onLoadFailed: function(error) {
          if (error === FileViewError.FileNotFound)
            root.finishLegacyAutoOpen("{}")
          else
            root.failAutoOpenLoad(error)
        }
      }
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
              messageScale: root.messageScale
              conversation: card.modelData.conversation
              peerName: root.friendLabel(card.modelData.conversation)
              peerAvatar: root.friendAvatar(card.modelData.conversation)
              peerAvatarRevision: root.service ? root.service.avatarTick : 0
              peerOnline: root.friendOnline(card.modelData.conversation)
              peerNameColor: root.headerNameColor
              peerStatusColor: root.headerStatusColor
              receiptSentColor: root.receiptSentColor
              receiptDeliveredColor: root.receiptDeliveredColor
              receiptReadColor: root.receiptReadColor
              autoOpenEnabled: root.autoOpenFor(card.modelData.conversation)
              onAutoOpenToggled: root.toggleAutoOpen(card.modelData.conversation)
              formatToolbarEnabled: root.formatToolbarEnabled
              onFormatToolbarToggled: function(enabled) { root.formatToolbarToggled(enabled) }
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
                helpText: "Pin chat window"
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
        messageScale: root.messageScale
        conversation: root.notificationConversation
        peerName: root.friendLabel(root.notificationConversation)
        peerAvatar: root.friendAvatar(root.notificationConversation)
        peerAvatarRevision: root.service ? root.service.avatarTick : 0
        peerOnline: root.friendOnline(root.notificationConversation)
        peerNameColor: root.headerNameColor
        peerStatusColor: root.headerStatusColor
        receiptSentColor: root.receiptSentColor
        receiptDeliveredColor: root.receiptDeliveredColor
        receiptReadColor: root.receiptReadColor
        autoOpenEnabled: root.autoOpenFor(root.notificationConversation)
        onAutoOpenToggled: root.toggleAutoOpen(root.notificationConversation)
        formatToolbarEnabled: root.formatToolbarEnabled
        onFormatToolbarToggled: function(enabled) { root.formatToolbarToggled(enabled) }
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
      implicitHeight: 420
      minimumSize: Qt.size(360, 420)
      color: root.theme().bg || Color.background
      property bool everShown: false
      property bool closing: false

      function applyRequestedFocus() {
        if (!pinWin.modelData || String(pinWin.modelData.conversation) !== root.focusConversation)
          return
        var requestedConversation = root.focusConversation
        Qt.callLater(function() {
          var win = pinPage.QsWindow.window
          if (win && typeof win.requestActivate === "function")
            win.requestActivate()
          pinPage.focusComposer()
          if (root.focusConversation === requestedConversation)
            root.focusConversation = ""
        })
      }

      Component.onCompleted: pinWin.applyRequestedFocus()

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
        if (!pinWin.closing && pinWin.everShown && pinWin.modelData && pinWin.modelData.conversation) {
          if (pinPage.inCall || pinPage.incoming)
            pinPage.hangUp()
          root.dismissCard(pinWin.modelData.conversation)
        }
      }

      FocusScope {
        id: pinFocus
        anchors.fill: parent
        Keys.onPressed: function(event) {
          if (event.key === Qt.Key_Escape && pinPage.handleEscape()) {
            event.accepted = true
            return
          }
          if (event.key === Qt.Key_O && (event.modifiers & Qt.ControlModifier)) {
            pinPage.attachFile()
            event.accepted = true
          }
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
            text: pinPage.autoOpenEnabled ? "Auto-off" : "Auto-open"
            helpText: pinPage.autoOpenEnabled
              ? "Auto-open chat: on" : "Auto-open chat: off"
            onClicked: pinPage.autoOpenToggled()
          }
          SurfaceBtn {
            text: root.muted ? "Unmute" : "Mute"
            selected: root.muted
            helpText: root.muted ? "Notification sound: off" : "Notification sound: on"
            onClicked: root.toggleMute()
          }
          SurfaceBtn {
            text: "Close"
            helpText: "Close chat"
            onClicked: {
              var conv = String(pinWin.modelData.conversation)
              if (pinPage.inCall || pinPage.incoming)
                pinPage.hangUp()
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
          messageScale: root.messageScale
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
          receiptSentColor: root.receiptSentColor
          receiptDeliveredColor: root.receiptDeliveredColor
          receiptReadColor: root.receiptReadColor
          autoOpenEnabled: root.autoOpenFor(pinWin.modelData ? pinWin.modelData.conversation : "")
          onAutoOpenToggled: root.toggleAutoOpen(pinWin.modelData ? pinWin.modelData.conversation : "")
          formatToolbarEnabled: root.formatToolbarEnabled
          onFormatToolbarToggled: function(enabled) { root.formatToolbarToggled(enabled) }
          readActive: {
            var win = pinPage.QsWindow.window
            return !!(pinWin.visible && win && win.active && pinFocus.activeFocus)
          }
        }

          Connections {
            target: root
            function onFocusRequestTickChanged() { pinWin.applyRequestedFocus() }
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
    minimumSize: Qt.size(360, 420)
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
          helpText: root.muted ? "Notification sound: off" : "Notification sound: on"
          onClicked: root.toggleMute()
        }
        SurfaceBtn {
          text: "Close"
          helpText: "Close demo"
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
        messageScale: root.messageScale
        conversation: "demo"
        receiptSentColor: root.receiptSentColor
        receiptDeliveredColor: root.receiptDeliveredColor
        receiptReadColor: root.receiptReadColor
        formatToolbarEnabled: root.formatToolbarEnabled
        onFormatToolbarToggled: function(enabled) { root.formatToolbarToggled(enabled) }
      }
    }
  }
}
