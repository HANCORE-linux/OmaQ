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
  property string instanceName: "default"

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
  readonly property bool demoOpen: OmaQ.SurfaceCoordinator.demoOpen
  readonly property bool isSurfaceOwner: OmaQ.SurfaceCoordinator.owner === root
  property bool ownershipTeardown: false
  property bool floatRulesReady: false
  property bool floatRuleReloadBlocked: false
  property string blockedOpenConversation: ""
  property string blockedOpenKey: ""
  property string blockedOpenName: ""
  property string lastNotifiedMessageId: ""
  property string focusConversation: ""
  property int focusRequestTick: 0
  property var pendingFocusWindow: null
  property var queuedFocusWindow: null
  property string queuedFocusTitle: ""
  readonly property bool muted: service ? service.muted : false
  readonly property bool callToneNeeded: root.isSurfaceOwner && service &&
    !service.callToneSuppressed &&
    (service.lastCallState === "incoming" || service.lastCallState === "ringing")
  readonly property bool callTonePlaying: OmaQ.CallTone.playing
  property string callToneOwner: ""
  property var autoOpenByConversation: ({})
  property bool autoOpenLoaded: false
  property bool autoOpenUnavailable: false
  property string autoOpenWarning: ""
  property var pendingIncoming: []
  property var pendingAutoOpenToggles: []
  property bool autoOpenDirectDefault: true
  property string autoOpenRequest: ""
  property int autoOpenSequence: 0
  readonly property string floatScriptPath:
    String(Qt.resolvedUrl("scripts/float-omaq.sh")).replace(/^file:\/\//, "")
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
    if (root.floatRuleReloadBlocked)
      OmaQ.SurfaceCoordinator.queueDemo()
    else
      OmaQ.SurfaceCoordinator.openDemo()
  }

  function closeDemo() {
    OmaQ.SurfaceCoordinator.closeDemo()
  }

  function openConversation(conv, name) {
    var conversation = String(conv || "")
    var expectedKey = conversation.charAt(0) === "g" ? "" : root.friendKey(conversation)
    OmaQ.SurfaceCoordinator.requestChat(conversation, expectedKey, String(name || ""))
  }

  function acceptOpenRequest(conv, expectedKey, name) {
    if (!root.isSurfaceOwner)
      return
    if (root.floatRuleReloadBlocked) {
      root.blockedOpenConversation = String(conv || "")
      root.blockedOpenKey = String(expectedKey || "")
      root.blockedOpenName = String(name || "")
      return
    }
    root.ensureCard(String(conv || ""), String(name || ""),
      String(expectedKey || ""))
    root.requestChatFocus(String(conv || ""))
  }

  function requestChatFocus(conv) {
    var key = String(conv || "")
    if (!key)
      return
    root.focusConversation = key
    root.focusRequestTick = root.focusRequestTick + 1
  }

  function chatWindowTitle(conversation) {
    var key = String(conversation || "")
    var label = String(root.friendLabel(key) || "Chat")
      .replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim()
    if (label.length > 48)
      label = label.slice(0, 48)
    var suffix = key.charAt(0) === "g" ? key.slice(0, 10) : key
    return "OmaQ chat — " + label + (suffix ? " · " + suffix : "")
  }

  function focusOmaQWindow(windowObject, title) {
    var target = String(title || "")
    if (!windowObject || !target)
      return
    if (focusOmaQProc.running) {
      root.queuedFocusWindow = windowObject
      root.queuedFocusTitle = target
      return
    }
    root.pendingFocusWindow = windowObject
    focusOmaQProc.command = [root.floatScriptPath, "focus-title", target]
    focusOmaQProc.running = true
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

  function friendKey(conv) {
    var friend = root.friendData(conv)
    var key = friend ? String(friend.key || "") : ""
    return /^[0-9a-f]{64}$/.test(key) ? key : ""
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

  function stableConversationKey(conv, expectedKey) {
    var conversation = String(conv || "")
    if (/^g:[0-9a-f]{64}$/.test(conversation))
      return conversation
    if (!/^(0|[1-9][0-9]*)$/.test(conversation) || conversation.length > 10 ||
        Number(conversation) > 4294967295)
      return ""
    var key = String(expectedKey || "")
    if (!/^[0-9a-f]{64}$/.test(key)) {
      var friend = root.friendData(conversation)
      key = friend ? String(friend.key || "") : ""
    }
    if (!root.service || !root.service.directBindingMatches(conversation, key))
      return ""
    return "d:" + key
  }

  function autoOpenFor(conv, expectedKey) {
    var stable = root.stableConversationKey(conv, expectedKey)
    if (root.autoOpenUnavailable || stable === "")
      return false
    if (root.autoOpenByConversation[stable] !== undefined)
      return root.autoOpenByConversation[stable] === true
    return stable.charAt(0) === "d" ? root.autoOpenDirectDefault : true
  }

  function nextAutoOpenRequest() {
    root.autoOpenSequence++
    return "ao-" + Date.now().toString(36) + "-" +
      root.autoOpenSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }

  function releasePendingIncoming() {
    var queued = root.pendingIncoming.slice()
    root.pendingIncoming = []
    for (var i = 0; i < queued.length; i++)
      root.onIncoming(queued[i].conversation, queued[i])
  }

  function failAutoOpenLoad(warning) {
    root.autoOpenUnavailable = true
    root.autoOpenWarning = warning ||
      "Auto-open settings could not be read; Auto-open is disabled."
    root.autoOpenByConversation = ({})
    root.autoOpenDirectDefault = false
    root.autoOpenLoaded = true
    root.autoOpenRequest = ""
    root.pendingAutoOpenToggles = []
    root.releasePendingIncoming()
  }

  function requestAutoOpenState() {
    if (root.autoOpenRequest !== "" || root.autoOpenLoaded || !root.service ||
        root.service.helperCompatibility !== "compatible")
      return
    if (!root.service.supportsStableDirectState) {
      root.failAutoOpenLoad(
        "Helper update required for stable Auto-open settings; Auto-open is disabled.")
      return
    }
    root.autoOpenRequest = root.nextAutoOpenRequest()
    if (!root.service.requestAutoOpen(root.autoOpenRequest))
      root.failAutoOpenLoad(
        "Auto-open settings could not be requested; Auto-open is disabled.")
  }

  function applyAutoOpenResult() {
    if (!root.service || root.autoOpenRequest === "" ||
        root.service.lastAutoOpenRequest !== root.autoOpenRequest)
      return
    var wasLoaded = root.autoOpenLoaded
    root.autoOpenRequest = ""
    if (!root.service.lastAutoOpenSucceeded) {
      if (!wasLoaded)
        root.failAutoOpenLoad(
          "Auto-open settings could not be read; Auto-open is disabled.")
      else
        root.autoOpenWarning = "Auto-open settings could not be saved."
    } else {
      var next = {}
      var items = root.service.lastAutoOpenItems || []
      for (var i = 0; i < items.length; i++) {
        var stable = String(items[i].conversation || "")
        if (!/^[dg]:[0-9a-f]{64}$/.test(stable) ||
            typeof items[i].enabled !== "boolean" || next[stable] !== undefined) {
          root.failAutoOpenLoad(
            "Auto-open settings were invalid; Auto-open is disabled.")
          return
        }
        next[stable] = items[i].enabled
      }
      root.autoOpenUnavailable = false
      root.autoOpenWarning = ""
      root.autoOpenByConversation = next
      root.autoOpenDirectDefault = !!root.service.lastAutoOpenDirectDefault
      root.autoOpenLoaded = true
      if (!wasLoaded)
        root.releasePendingIncoming()
    }
    if (root.pendingAutoOpenToggles.length > 0) {
      var pending = root.pendingAutoOpenToggles.slice()
      root.pendingAutoOpenToggles = pending.slice(1)
      root.toggleAutoOpen(pending[0].conversation, pending[0].key)
    }
  }

  function toggleAutoOpen(conv, expectedKey) {
    var conversation = String(conv || "")
    var key = String(expectedKey || "")
    var stable = root.stableConversationKey(conversation, key)
    if (stable === "")
      return
    if (!root.autoOpenLoaded || root.autoOpenRequest !== "") {
      var pending = { conversation: conversation, key: key }
      for (var i = 0; i < root.pendingAutoOpenToggles.length; i++)
        if (root.pendingAutoOpenToggles[i].conversation === pending.conversation &&
            root.pendingAutoOpenToggles[i].key === pending.key)
          return
      var queued = root.pendingAutoOpenToggles.slice()
      queued.push(pending)
      root.pendingAutoOpenToggles = queued
      root.requestAutoOpenState()
      return
    }
    root.autoOpenRequest = root.nextAutoOpenRequest()
    if (!root.service.setAutoOpen(stable,
          !root.autoOpenFor(conversation, key), root.autoOpenRequest)) {
      root.autoOpenRequest = ""
      root.autoOpenWarning = "Auto-open settings could not be saved."
    }
  }

  function cardBindingValid(card) {
    if (!card || !card.conversation)
      return false
    var conversation = String(card.conversation)
    if (conversation.charAt(0) === "g")
      return !!(!root.service || !root.service.groupsReady ||
        root.service.groupById(conversation))
    return !!(root.service &&
      root.service.directBindingMatches(conversation, String(card.directKey || "")))
  }

  function closeRemovedGroup(conv) {
    var key = String(conv || "")
    if (!key)
      return
    for (var i = 0; i < openCards.length; i++) {
      if (String(openCards[i].conversation) === key) {
        root.dismissCard(key, "")
        return
      }
    }
    var persisted = service ? (service.surfaces || []) : []
    for (var j = 0; j < persisted.length; j++) {
      if (String(persisted[j].conversation) === key) {
        service.setSurface(key, persisted[j].monitor || "", Number(persisted[j].x || 0),
          Number(persisted[j].y || 0), false, "")
        return
      }
    }
  }

  function reconcileOpenCards() {
    if (!service)
      return
    var current = openCards.slice()
    var next = []
    for (var i = 0; i < current.length; i++) {
      var conversation = String(current[i].conversation || "")
      if (conversation.charAt(0) === "g") {
        if (!service.groupsReady || service.groupById(conversation))
          next.push(current[i])
      } else if (root.cardBindingValid(current[i])) {
        next.push(current[i])
      }
    }
    if (next.length !== current.length)
      openCards = next
  }

  function reconcileOpenGroups() {
    root.reconcileOpenCards()
  }

  function restoreSurfaces() {
    var persisted = service ? (service.surfaces || []) : []
    var current = openCards.slice()
    var next = []
    var i, j, saved, found
    for (i = 0; i < current.length; i++) {
      if (!root.cardBindingValid(current[i]))
        continue
      saved = null
      for (j = 0; j < persisted.length; j++) {
        if (String(persisted[j].conversation) === String(current[i].conversation) &&
            (String(current[i].conversation).charAt(0) === "g" ||
             String(persisted[j].key || "") === String(current[i].directKey || ""))) {
          saved = persisted[j]
          break
        }
      }
      next.push(saved ? {
        conversation: current[i].conversation,
        directKey: current[i].directKey || "",
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
      var persistedConversation = String(persisted[i].conversation)
      var persistedKey = String(persisted[i].key || "")
      if (persistedConversation.charAt(0) === "g") {
        if (service && service.groupsReady && !service.groupById(persistedConversation)) {
          service.setSurface(persistedConversation, persisted[i].monitor || "",
            Number(persisted[i].x || 0), Number(persisted[i].y || 0), false, "")
          continue
        }
      } else if (!service ||
                 !service.directBindingMatches(persistedConversation, persistedKey)) {
        continue
      }
      found = false
      for (j = 0; j < next.length; j++)
        if (String(next[j].conversation) === persistedConversation &&
            (persistedConversation.charAt(0) === "g" ||
             String(next[j].directKey || "") === persistedKey))
          found = true
      if (!found)
        next.push({
          conversation: persistedConversation,
          directKey: persistedConversation.charAt(0) === "g" ? "" : persistedKey,
          monitor: persisted[i].monitor || "",
          x: isFinite(Number(persisted[i].x)) ? Number(persisted[i].x) : 40,
          y: isFinite(Number(persisted[i].y)) ? Number(persisted[i].y) : 80,
          pinned: true,
          name: friendName(persistedConversation)
        })
    }
    if (surfaceMode === "bundled" && next.length > 1)
      next = [next[0]]
    openCards = next
    surfacesHydrated = true
  }

  function activateSurfaceOwner() {
    if (!root.isSurfaceOwner)
      return
    root.floatRulesReady = false
    if (!installFloatRules.running)
      installFloatRules.running = true
    if (service) {
      service.sendOp({ op: "surface.list" })
      root.requestAutoOpenState()
    }
    OmaQ.SurfaceCoordinator.deliverPending()
  }

  onIsSurfaceOwnerChanged: {
    if (root.isSurfaceOwner) {
      root.ownershipTeardown = false
      root.floatRuleReloadBlocked = false
      root.activateSurfaceOwner()
      OmaQ.CallTone.setRequested(root.callToneOwner, root.callToneNeeded)
    } else {
      root.ownershipTeardown = true
      root.floatRuleReloadBlocked = false
      root.blockedOpenConversation = ""
      root.blockedOpenKey = ""
      root.blockedOpenName = ""
      floatRuleWatcher.running = false
      installFloatRules.running = false
      installFloatRulesRetry.stop()
      root.floatRulesReady = false
      OmaQ.CallTone.setRequested(root.callToneOwner, false)
    }
  }

  Component.onCompleted: {
    root.callToneOwner = OmaQ.CallTone.acquireOwner()
    OmaQ.SurfaceCoordinator.registerHost(root)
  }
  Component.onDestruction: {
    OmaQ.CallTone.setRequested(root.callToneOwner, false)
    OmaQ.SurfaceCoordinator.unregisterHost(root)
  }

  component SurfaceBtn: Button {
    id: surfaceButton
    property string helpText: ""
    tooltipText: helpText
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
      id: surfaceFocusTooltip
      visible: surfaceButton.tooltipText !== "" && !surfaceButton.hot &&
        surfaceButton.activeFocus &&
        (surfaceButton.activeFocusReason === Qt.TabFocusReason ||
         surfaceButton.activeFocusReason === Qt.BacktabFocusReason)
      text: surfaceButton.tooltipText
      delay: 400
      padding: 0
      readonly property var tokenBorderSpec: Border.localOrSurfaceSpec(
        "tooltip", "border", Color.tooltip.border, Color.tooltip.border,
        Math.max(1, Style.normalBorderWidth))
      background: BorderSurface {
        color: Color.tooltip.background
        borderSpec: surfaceFocusTooltip.tokenBorderSpec
        radius: 0
      }
      contentItem: Text {
        text: surfaceFocusTooltip.text
        color: Color.tooltip.text
        font.family: Style.font.family
        font.pixelSize: Style.font.bodySmall
        leftPadding: Border.left(surfaceFocusTooltip.tokenBorderSpec) + Style.spacing.controlPaddingX
        rightPadding: Border.right(surfaceFocusTooltip.tokenBorderSpec) + Style.spacing.controlPaddingX
        topPadding: Border.top(surfaceFocusTooltip.tokenBorderSpec) + Style.spacing.controlPaddingY
        bottomPadding: Border.bottom(surfaceFocusTooltip.tokenBorderSpec) + Style.spacing.controlPaddingY
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

  function ensureCard(conv, name, expectedKey) {
    var conversation = String(conv || "")
    if (!conversation)
      return false
    var directKey = conversation.charAt(0) === "g" ? "" : String(expectedKey || "")
    if (conversation.charAt(0) !== "g") {
      if (!/^[0-9a-f]{64}$/.test(directKey) || !service ||
          !service.directBindingMatches(conversation, directKey))
        return false
    }
    var label = name ? String(name) : ""
    var filtered = []
    var existing = -1
    for (var i = 0; i < openCards.length; i++) {
      var item = openCards[i]
      if (String(item.conversation) === conversation &&
          String(item.directKey || "") !== directKey)
        continue
      if (String(item.conversation) === conversation)
        existing = filtered.length
      filtered.push(item)
    }
    if (existing >= 0) {
      var current = filtered[existing]
      filtered[existing] = {
        conversation: conversation,
        directKey: directKey,
        monitor: current.monitor,
        x: current.x,
        y: current.y,
        pinned: true,
        name: label || current.name || ""
      }
      openCards = filtered
      if (!current.pinned)
        root.pin(conversation, true, directKey)
      return false
    }
    var card = { conversation: conversation, directKey: directKey, monitor: "",
      x: 40 + filtered.length * 16, y: 80 + filtered.length * 16,
      pinned: true, name: label }
    if (surfaceMode === "bundled")
      filtered = [card]
    else
      filtered.push(card)
    openCards = filtered
    if (surfacesHydrated)
      service.setSurface(conversation, "", card.x, card.y, true, directKey)
    return true
  }

  function dismissCard(conv, expectedKey) {
    var conversation = String(conv || "")
    var bindingKey = String(expectedKey || "")
    var next = []
    var removed = null
    for (var i = 0; i < openCards.length; i++) {
      if (String(openCards[i].conversation) !== conversation ||
          (bindingKey !== "" && String(openCards[i].directKey || "") !== bindingKey))
        next.push(openCards[i])
      else
        removed = openCards[i]
    }
    openCards = next
    if (removed)
      service.setSurface(conversation, removed.monitor, removed.x, removed.y, false,
        String(removed.directKey || ""))
  }

  function pin(conv, on, expectedKey) {
    var conversation = String(conv || "")
    var bindingKey = String(expectedKey || "")
    var next = []
    var saved = null
    for (var i = 0; i < openCards.length; i++) {
      var card = openCards[i]
      if (String(card.conversation) === conversation &&
          (bindingKey === "" || String(card.directKey || "") === bindingKey)) {
        saved = card
        card = { conversation: card.conversation, directKey: card.directKey || "",
          monitor: card.monitor, x: card.x, y: card.y, pinned: !!on,
          name: card.name || "" }
      }
      next.push(card)
    }
    openCards = next
    if (saved)
      service.setSurface(conversation, saved.monitor, saved.x, saved.y, !!on,
        String(saved.directKey || ""))
  }

  function savePos(conv, mon, x, y, pinned, expectedKey) {
    service.setSurface(String(conv || ""), mon || "", x, y, !!pinned,
      String(expectedKey || ""))
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
    var messageKey = event.key !== undefined
      ? String(event.key || "") : (service ? String(service.lastChatKey || "") : "")
    if (service && key.charAt(0) !== "g" && messageKey === "" &&
        !service.supportsStableDirectState)
      messageKey = root.friendKey(key)
    if (!service || !key || messageDir !== "in" || !messageText ||
        (key.charAt(0) !== "g" && !service.directBindingMatches(key, messageKey)))
      return
    if (!root.autoOpenLoaded) {
      if (messageId) {
        var queuedIndex
        for (queuedIndex = 0; queuedIndex < root.pendingIncoming.length; queuedIndex++) {
          if (root.pendingIncoming[queuedIndex].id === messageId)
            return
        }
        root.pendingIncoming.push({ conversation: key, key: messageKey, id: messageId,
          text: messageText, dir: messageDir })
      }
      return
    }
    if (!messageId || key + "|" + messageId === root.lastNotifiedMessageId)
      return
    var directKey = key.charAt(0) === "g" ? "" : messageKey
    root.lastNotifiedMessageId = key + "|" + messageId
    if (root.autoOpenFor(key, directKey))
      ensureCard(key, "", directKey)
    if (animateUnread)
      pulseConv = key
    playSound()
    if (root.autoOpenFor(key, directKey))
      desktopNotify()
  }

  Connections {
    target: service
    enabled: root.isSurfaceOwner
    function handleIncoming() {
      root.onIncoming(service.lastChatConv || service.lastConversation, {
        conversation: service.lastChatConv || service.lastConversation,
        id: service.lastChatId,
        key: service.lastChatKey,
        text: service.lastChatText,
        dir: service.lastChatDir
      })
    }
    function onMessageTickChanged() { handleIncoming() }
    function onSurfacesTickChanged() { root.restoreSurfaces() }
    function onGroupsTickChanged() { root.reconcileOpenGroups() }
    function onFriendsTickChanged() { root.reconcileOpenCards() }
    function onAutoOpenTickChanged() { root.applyAutoOpenResult() }
    function onHelperHandshakeTickChanged() {
      if (!root.service)
        return
      if (root.autoOpenRequest !== "") {
        if (!root.service.requestAutoOpen(root.autoOpenRequest))
          root.failAutoOpenLoad(
            "Auto-open settings could not be requested; Auto-open is disabled.")
      } else if (!root.autoOpenLoaded) {
        root.requestAutoOpenState()
      }
    }
    function onRemovedGroupTickChanged() { root.closeRemovedGroup(service.lastRemovedGroup) }
    function onIdentityTickChanged() {
      root.openCards = []
      root.pendingIncoming = []
      root.focusConversation = ""
      root.autoOpenByConversation = ({})
      root.autoOpenUnavailable = false
      root.autoOpenWarning = ""
      root.autoOpenDirectDefault = true
      root.autoOpenRequest = ""
      root.pendingAutoOpenToggles = []
      root.autoOpenLoaded = false
      Qt.callLater(root.requestAutoOpenState)
    }
  }

  Process { id: sndProc }
  Process { id: noteProc }
  Process {
    id: installFloatRules
    command: [root.floatScriptPath, "install-rules"]
    running: false
    onExited: function(code) {
      if (!root.isSurfaceOwner)
        return
      if (code !== 0) {
        console.warn("OmaQ: could not install first-map floating rules; retrying")
        installFloatRulesRetry.restart()
        return
      }
      root.floatRulesReady = true
      root.floatRuleReloadBlocked = false
      OmaQ.SurfaceCoordinator.deliverPendingDemo()
      if (!floatRuleWatcher.running)
        floatRuleWatcher.running = true
      if (root.blockedOpenConversation !== "") {
        var conversation = root.blockedOpenConversation
        var expectedKey = root.blockedOpenKey
        var name = root.blockedOpenName
        root.blockedOpenConversation = ""
        root.blockedOpenKey = ""
        root.blockedOpenName = ""
        root.acceptOpenRequest(conversation, expectedKey, name)
      }
    }
  }
  Timer {
    id: installFloatRulesRetry
    interval: 2000
    repeat: false
    onTriggered: if (root.isSurfaceOwner &&
                     (!root.floatRulesReady || root.floatRuleReloadBlocked) &&
                     !installFloatRules.running)
      installFloatRules.running = true
  }
  Process {
    id: floatRuleWatcher
    command: [root.floatScriptPath, "watch-rules"]
    running: false
    onExited: function(code) {
      if (!root.isSurfaceOwner || code === 2)
        return
      if (code === 5) {
        root.floatRuleReloadBlocked = true
        if (!installFloatRules.running)
          installFloatRules.running = true
        return
      }
      floatRuleWatcherRestart.restart()
    }
  }
  Timer {
    id: floatRuleWatcherRestart
    interval: 2000
    repeat: false
    onTriggered: if (root.isSurfaceOwner && root.floatRulesReady)
      floatRuleWatcher.running = true
  }
  Process {
    id: focusOmaQProc
    onExited: function(code) {
      var target = root.pendingFocusWindow
      root.pendingFocusWindow = null
      if (target && typeof target.completeRequestedFocus === "function")
        target.completeRequestedFocus(code === 0)
      if (root.queuedFocusWindow && root.queuedFocusTitle !== "") {
        var queuedWindow = root.queuedFocusWindow
        var queuedTitle = root.queuedFocusTitle
        root.queuedFocusWindow = null
        root.queuedFocusTitle = ""
        Qt.callLater(function() { root.focusOmaQWindow(queuedWindow, queuedTitle) })
      }
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
    model: root.isSurfaceOwner ? Quickshell.screens : []
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
              peerKey: String(card.modelData.directKey || "")
              peerName: root.friendLabel(card.modelData.conversation)
              peerAvatar: root.friendAvatar(card.modelData.conversation)
              peerAvatarRevision: root.service ? root.service.avatarTick : 0
              peerOnline: root.friendOnline(card.modelData.conversation)
              peerNameColor: root.headerNameColor
              peerStatusColor: root.headerStatusColor
              receiptSentColor: root.receiptSentColor
              receiptDeliveredColor: root.receiptDeliveredColor
              receiptReadColor: root.receiptReadColor
              autoOpenEnabled: root.autoOpenFor(card.modelData.conversation,
                card.modelData.directKey || "")
              onAutoOpenToggled: root.toggleAutoOpen(card.modelData.conversation,
                card.modelData.directKey || "")
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
                onClicked: root.pin(card.modelData.conversation, true,
                  card.modelData.directKey || "")
              }
            }

            MouseArea {
              anchors.bottom: parent.bottom
              width: parent.width
              height: 16
              drag.target: card
              cursorShape: Qt.SizeAllCursor
              onReleased: root.savePos(card.modelData.conversation,
                overlay.modelData.name, card.x, card.y, false,
                card.modelData.directKey || "")
            }
          }
        }
      }
    }
  }

  PanelWindow {
    id: rightDock
    visible: root.isSurfaceOwner && root.notifyRight && service && service.lastChatDir === "in" &&
      service.lastChatText !== "" &&
      (root.notificationConversation.charAt(0) === "g" ||
       service.directBindingMatches(root.notificationConversation, service.lastChatKey)) &&
      root.autoOpenFor(root.notificationConversation, service.lastChatKey)
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
        peerKey: String(service.lastChatKey || "")
        peerName: root.friendLabel(root.notificationConversation)
        peerAvatar: root.friendAvatar(root.notificationConversation)
        peerAvatarRevision: root.service ? root.service.avatarTick : 0
        peerOnline: root.friendOnline(root.notificationConversation)
        peerNameColor: root.headerNameColor
        peerStatusColor: root.headerStatusColor
        receiptSentColor: root.receiptSentColor
        receiptDeliveredColor: root.receiptDeliveredColor
        receiptReadColor: root.receiptReadColor
        autoOpenEnabled: root.autoOpenFor(root.notificationConversation,
          service.lastChatKey)
        onAutoOpenToggled: root.toggleAutoOpen(root.notificationConversation,
          service.lastChatKey)
        formatToolbarEnabled: root.formatToolbarEnabled
        onFormatToolbarToggled: function(enabled) { root.formatToolbarToggled(enabled) }
      }
    }
  }

  Instantiator {
    model: root.isSurfaceOwner && root.floatRulesReady ? root.openCards : []
    delegate: FloatingWindow {
      id: pinWin
      required property var modelData
      // Keep the first map title stable for the floating rule, then expose a
      // per-conversation title so an existing chat can be moved precisely.
      title: pinWin.everShown && pinWin.modelData
        ? root.chatWindowTitle(pinWin.modelData.conversation) : "OmaQ chat"
      implicitWidth: 420
      implicitHeight: 420
      minimumSize: Qt.size(360, 420)
      color: root.theme().bg || Color.background
      property bool everShown: false
      property bool closing: false

      function applyRequestedFocus() {
        if (!pinWin.modelData || String(pinWin.modelData.conversation) !== root.focusConversation)
          return
        Qt.callLater(function() {
          if (pinWin.visible)
            root.focusOmaQWindow(pinWin, pinWin.title)
        })
      }

      function completeRequestedFocus(moved) {
        var requestedConversation = pinWin.modelData
          ? String(pinWin.modelData.conversation) : ""
        if (!moved)
          console.warn("OmaQ: could not move chat to the current workspace")
        var win = pinPage.QsWindow.window
        if (win && typeof win.requestActivate === "function")
          win.requestActivate()
        pinPage.focusComposer()
        if (root.focusConversation === requestedConversation)
          root.focusConversation = ""
      }

      Component.onCompleted: {
        if (pinWin.backingWindowVisible)
          pinWin.everShown = true
        pinWin.applyRequestedFocus()
      }
      onBackingWindowVisibleChanged: {
        if (pinWin.backingWindowVisible)
          pinWin.everShown = true
      }

      onVisibleChanged: {
        if (visible) {
          pinWin.everShown = true
          pinWin.closing = false
          return
        }
        if (root.isSurfaceOwner && !root.ownershipTeardown &&
            !pinWin.closing && pinWin.everShown && pinWin.modelData &&
            pinWin.modelData.conversation) {
          if (pinPage.inCall || pinPage.incoming)
            pinPage.hangUp()
          root.dismissCard(pinWin.modelData.conversation,
            pinWin.modelData.directKey || "")
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
              root.dismissCard(conv, pinWin.modelData.directKey || "")
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
          peerKey: pinWin.modelData ? String(pinWin.modelData.directKey || "") : ""
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
          autoOpenEnabled: root.autoOpenFor(
            pinWin.modelData ? pinWin.modelData.conversation : "",
            pinWin.modelData ? pinWin.modelData.directKey || "" : "")
          onAutoOpenToggled: root.toggleAutoOpen(
            pinWin.modelData ? pinWin.modelData.conversation : "",
            pinWin.modelData ? pinWin.modelData.directKey || "" : "")
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
    visible: root.isSurfaceOwner && root.floatRulesReady && root.demoOpen
    title: "OmaQ demo"
    implicitWidth: 420
    implicitHeight: 480
    minimumSize: Qt.size(360, 420)
    color: root.theme().bg || Color.background

    onVisibleChanged: {
      if (visible) {
        Qt.callLater(function() {
          if (demoPage)
            demoPage.resetDemo()
        })
      } else if (root.demoOpen && root.isSurfaceOwner && root.floatRulesReady) {
        OmaQ.SurfaceCoordinator.closeDemo()
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
