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
  readonly property string soundName: {
    var value = String(setting("sound", "icq-message"))
    return ["off", "icq-message", "qq", "msn", "aurora", "glow", "click",
      "knock", "custom"].indexOf(value) >= 0 ? value : "icq-message"
  }
  readonly property string soundCustom: String(setting("soundCustomPath", ""))
  readonly property string soundCustomId: String(setting("soundCustomId", ""))
  readonly property string chatTheme: String(setting("chatTheme", "system"))
  readonly property real messageScale: {
    var value = Number(setting("messageScale", 1.0))
    return [0.9, 1.0, 1.1, 1.2, 1.4].indexOf(value) >= 0 ? value : 1.0
  }
  readonly property bool formatToolbarEnabled: !!setting("formatToolbar", false)
  signal formatToolbarToggled(bool enabled)

  readonly property var openCards: openCardModel
  property bool surfacesHydrated: false
  property string pulseConv: ""
  readonly property bool demoOpen: OmaQ.SurfaceCoordinator.demoOpen
  readonly property bool isSurfaceOwner: OmaQ.SurfaceCoordinator.owner === root
  property bool ownershipTeardown: false
  property bool floatRulesReady: false
  property bool floatRuleReloadBlocked: false
  property int geometryGeneration: 0
  property int geometrySnapshotGeneration: -1
  property string lastNotifiedMessageId: ""
  property string activeCustomSoundId: ""
  property string activeCustomSoundPath: ""
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
  property var pendingSurfaceOpens: []
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

  ListModel {
    id: openCardModel
  }

  function cardCount() {
    return openCardModel.count
  }

  function cardAt(index) {
    return index >= 0 && index < openCardModel.count
      ? openCardModel.get(index) : null
  }

  function cardIndex(conversation, expectedKey) {
    var target = String(conversation || "")
    var binding = String(expectedKey || "")
    for (var i = 0; i < openCardModel.count; i++) {
      var card = openCardModel.get(i)
      if (String(card.conversation || "") === target &&
          (target.charAt(0) === "g" || String(card.directKey || "") === binding))
        return i
    }
    return -1
  }

  function appendCard(card) {
    var value = card || ({})
    openCardModel.append({
      conversation: String(value.conversation || ""),
      directKey: String(value.directKey || ""),
      monitor: String(value.monitor || ""),
      surfaceX: Math.round(Number(value.surfaceX || 0)),
      surfaceY: Math.round(Number(value.surfaceY || 0)),
      surfaceWidth: Math.max(360, Math.round(Number(value.surfaceWidth || 420))),
      surfaceHeight: Math.max(420, Math.round(Number(value.surfaceHeight || 420))),
      pinned: value.pinned !== false,
      name: String(value.name || ""),
      placeOnMap: !!value.placeOnMap,
      explicitOpen: !!value.explicitOpen
    })
  }

  function updateCard(index, values) {
    if (index < 0 || index >= openCardModel.count)
      return
    var next = values || ({})
    for (var key in next)
      openCardModel.setProperty(index, key, next[key])
  }

  function clearCards() {
    while (openCardModel.count > 0)
      openCardModel.remove(openCardModel.count - 1)
  }

  function queueSurfaceOpen(conversation, directKey, name, monitor, focus) {
    var pending = root.pendingSurfaceOpens.slice()
    var value = { conversation: String(conversation || ""),
      directKey: String(directKey || ""), name: String(name || ""),
      monitor: String(monitor || ""), focus: !!focus }
    for (var i = 0; i < pending.length; i++)
      if (pending[i].conversation === value.conversation &&
          pending[i].directKey === value.directKey) {
        if (pending[i].focus && !value.focus) {
          value.name = pending[i].name
          value.monitor = pending[i].monitor
        }
        value.focus = value.focus || !!pending[i].focus
        pending[i] = value
        root.pendingSurfaceOpens = pending
        return
      }
    pending.push(value)
    root.pendingSurfaceOpens = pending
  }

  function flushPendingSurfaceOpens() {
    if (!root.surfacesHydrated || root.floatRuleReloadBlocked ||
        root.pendingSurfaceOpens.length === 0)
      return
    var pending = root.pendingSurfaceOpens.slice()
    root.pendingSurfaceOpens = []
    for (var i = 0; i < pending.length; i++) {
      root.ensureCard(pending[i].conversation, pending[i].name,
        pending[i].directKey, pending[i].monitor)
      if (pending[i].focus)
        root.requestChatFocus(pending[i].conversation)
    }
  }

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
    OmaQ.SurfaceCoordinator.requestChat(conversation, expectedKey, String(name || ""),
      root.instanceName)
  }

  function acceptOpenRequest(conv, expectedKey, name, monitor) {
    if (!root.isSurfaceOwner)
      return
    if (!root.surfacesHydrated || root.floatRuleReloadBlocked) {
      root.queueSurfaceOpen(conv, expectedKey, name, monitor, true)
      return
    }
    root.ensureCard(String(conv || ""), String(name || ""),
      String(expectedKey || ""), String(monitor || ""))
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
    var suffix = key
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
    var index = root.cardIndex(key, "")
    if (index >= 0) {
      root.dismissCard(key, "")
      return
    }
    var persisted = service ? (service.surfaces || []) : []
    for (var j = 0; j < persisted.length; j++) {
      if (String(persisted[j].conversation) === key) {
        service.setSurface(key, persisted[j].monitor || "", Number(persisted[j].x || 0),
          Number(persisted[j].y || 0), false, "", Number(persisted[j].width || 420),
          Number(persisted[j].height || 420))
        return
      }
    }
  }

  function reconcileOpenCards() {
    if (!service)
      return
    for (var i = openCardModel.count - 1; i >= 0; i--)
      if (!root.cardBindingValid(openCardModel.get(i)))
        openCardModel.remove(i)
  }

  function reconcileOpenGroups() {
    root.reconcileOpenCards()
  }

  function restoredSize(value, fallback, minimum) {
    var number = Number(value)
    return isFinite(number) && number >= minimum && number <= 4096
      ? Math.round(number) : fallback
  }

  function restoreSurfaces() {
    var persisted = service ? (service.surfaces || []) : []
    var i, j, saved
    root.reconcileOpenCards()
    for (i = 0; i < openCardModel.count; i++) {
      var current = openCardModel.get(i)
      saved = null
      for (j = 0; j < persisted.length; j++) {
        if (String(persisted[j].conversation) === String(current.conversation) &&
            (String(current.conversation).charAt(0) === "g" ||
             String(persisted[j].key || "") === String(current.directKey || ""))) {
          saved = persisted[j]
          break
        }
      }
      if (saved) {
        var keepExplicitlyOpen = !!current.explicitOpen
        root.updateCard(i, {
          monitor: String(saved.monitor || ""),
          surfaceX: isFinite(Number(saved.x)) ? Math.round(Number(saved.x)) : 40,
          surfaceY: isFinite(Number(saved.y)) ? Math.round(Number(saved.y)) : 80,
          surfaceWidth: root.restoredSize(saved.width, 420, 360),
          surfaceHeight: root.restoredSize(saved.height, 420, 420),
          pinned: keepExplicitlyOpen ? true : !!saved.pinned,
          name: String(current.name || root.friendName(current.conversation)),
          explicitOpen: false
        })
        if (keepExplicitlyOpen)
          service.setSurface(String(current.conversation || ""),
            String(saved.monitor || ""), Number(saved.x || 0), Number(saved.y || 0),
            true, String(current.directKey || ""),
            root.restoredSize(saved.width, 420, 360),
            root.restoredSize(saved.height, 420, 420))
      } else {
        service.setSurface(String(current.conversation || ""),
          String(current.monitor || ""), Number(current.surfaceX || 0),
          Number(current.surfaceY || 0), !!current.pinned,
          String(current.directKey || ""), Number(current.surfaceWidth || 420),
          Number(current.surfaceHeight || 420))
        root.updateCard(i, { explicitOpen: false })
      }
    }
    var persistedOrder = []
    for (i = 0; i < persisted.length; i++)
      if (String(persisted[i].monitor || "") !== "")
        persistedOrder.push(i)
    for (i = 0; i < persisted.length; i++)
      if (String(persisted[i].monitor || "") === "")
        persistedOrder.push(i)
    for (var persistedOrderIndex = 0;
         persistedOrderIndex < persistedOrder.length; persistedOrderIndex++) {
      i = persistedOrder[persistedOrderIndex]
      if (!persisted[i].pinned || !persisted[i].conversation)
        continue
      var persistedConversation = String(persisted[i].conversation)
      var persistedKey = String(persisted[i].key || "")
      if (persistedConversation.charAt(0) === "g") {
        if (service && service.groupsReady && !service.groupById(persistedConversation)) {
          service.setSurface(persistedConversation, persisted[i].monitor || "",
            Number(persisted[i].x || 0), Number(persisted[i].y || 0), false, "",
            Number(persisted[i].width || 420), Number(persisted[i].height || 420))
          continue
        }
      } else if (!service ||
                 !service.directBindingMatches(persistedConversation, persistedKey)) {
        continue
      }
      if (root.cardIndex(persistedConversation, persistedKey) < 0) {
        var persistedMonitor = String(persisted[i].monitor || "")
        var persistedLegacyMonitor = persistedMonitor === ""
        var persistedWidth = root.restoredSize(persisted[i].width, 420, 360)
        var persistedHeight = root.restoredSize(persisted[i].height, 420, 420)
        var persistedX = isFinite(Number(persisted[i].x)) ? Number(persisted[i].x) : 40
        var persistedY = isFinite(Number(persisted[i].y)) ? Number(persisted[i].y) : 80
        if (persistedMonitor === "") {
          persistedMonitor = String(root.instanceName || "")
          for (var occupiedIndex = 0; occupiedIndex < openCardModel.count;
               occupiedIndex++)
            if (root.cardMonitorCollides(openCardModel.get(occupiedIndex).monitor,
                  persistedMonitor) && root.surfaceRectanglesOverlap(persistedX,
                  persistedY, persistedWidth, persistedHeight,
                  openCardModel.get(occupiedIndex))) {
              var legacyGeometry = root.initialGeometry(persistedMonitor,
                persistedWidth, persistedHeight)
              persistedX = legacyGeometry.surfaceX
              persistedY = legacyGeometry.surfaceY
              break
            }
        }
        root.appendCard({
          conversation: persistedConversation,
          directKey: persistedConversation.charAt(0) === "g" ? "" : persistedKey,
          monitor: persistedMonitor,
          surfaceX: persistedX,
          surfaceY: persistedY,
          surfaceWidth: persistedWidth,
          surfaceHeight: persistedHeight,
          pinned: true,
          name: root.friendName(persistedConversation),
          placeOnMap: true,
          explicitOpen: false
        })
        if (persistedLegacyMonitor)
          service.setSurface(persistedConversation, persistedMonitor,
            persistedX, persistedY, true,
            persistedConversation.charAt(0) === "g" ? "" : persistedKey,
            persistedWidth, persistedHeight)
      }
    }
    if (surfaceMode === "bundled")
      while (openCardModel.count > 1)
        openCardModel.remove(openCardModel.count - 1)
    surfacesHydrated = true
    root.flushPendingSurfaceOpens()
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
      root.pendingSurfaceOpens = []
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

  function persistedSurface(conversation, directKey) {
    var persisted = service ? (service.surfaces || []) : []
    for (var i = 0; i < persisted.length; i++)
      if (String(persisted[i].conversation || "") === conversation &&
          (conversation.charAt(0) === "g" ||
           String(persisted[i].key || "") === directKey))
        return persisted[i]
    return null
  }

  function cardMonitorCollides(cardMonitor, targetMonitor) {
    var cardName = String(cardMonitor || "")
    var targetName = String(targetMonitor || "")
    return cardName === "" || targetName === "" || cardName === targetName
  }

  function surfaceRectanglesOverlap(x, y, width, height, card) {
    var otherX = Number(card.surfaceX || 0)
    var otherY = Number(card.surfaceY || 0)
    var otherWidth = Math.max(200, Number(card.surfaceWidth || 420))
    var otherHeight = Math.max(160, Number(card.surfaceHeight || 420))
    return x < otherX + otherWidth + 12 && x + width + 12 > otherX &&
      y < otherY + otherHeight + 12 && y + height + 12 > otherY
  }

  function initialGeometry(monitor, preferredWidth, preferredHeight) {
    var screenName = String(monitor || root.instanceName || "")
    var screenX = 0
    var screenY = 0
    var screenWidth = 960
    var screenHeight = 900
    var screens = Quickshell.screens || []
    for (var screenIndex = 0; screenIndex < screens.length; screenIndex++)
      if (String(screens[screenIndex].name || "") === screenName) {
        screenX = Number(screens[screenIndex].x || 0)
        screenY = Number(screens[screenIndex].y || 0)
        screenWidth = Math.max(420, Number(screens[screenIndex].width || 960))
        screenHeight = Math.max(420, Number(screens[screenIndex].height || 900))
        break
      }
    var width = root.restoredSize(preferredWidth, 420, 360)
    var height = root.restoredSize(preferredHeight, 420, 420)
    var x = Math.round(screenX + 40)
    var y = Math.round(screenY + 80)
    for (var attempt = 0; attempt < 64; attempt++) {
      var occupied = false
      for (var i = 0; i < openCardModel.count; i++) {
        var card = openCardModel.get(i)
        if (root.cardMonitorCollides(card.monitor, screenName) &&
            root.surfaceRectanglesOverlap(x, y, width, height, card)) {
          occupied = true
          break
        }
      }
      if (!occupied)
        break
      x += width + 28
      if (x + width > screenX + screenWidth - 20) {
        x = Math.round(screenX + 40)
        y += height + 28
      }
    }
    return { monitor: screenName, surfaceX: x, surfaceY: y,
      surfaceWidth: width, surfaceHeight: height }
  }

  function ensureCard(conv, name, expectedKey, preferredMonitor) {
    var conversation = String(conv || "")
    if (!conversation)
      return false
    var directKey = conversation.charAt(0) === "g" ? "" : String(expectedKey || "")
    if (!root.surfacesHydrated || root.floatRuleReloadBlocked) {
      root.queueSurfaceOpen(conversation, directKey, name, preferredMonitor, false)
      return false
    }
    if (conversation.charAt(0) !== "g") {
      if (!/^[0-9a-f]{64}$/.test(directKey) || !service ||
          !service.directBindingMatches(conversation, directKey))
        return false
    }
    for (var stale = openCardModel.count - 1; stale >= 0; stale--) {
      var staleCard = openCardModel.get(stale)
      if (String(staleCard.conversation || "") === conversation &&
          conversation.charAt(0) !== "g" &&
          String(staleCard.directKey || "") !== directKey)
        openCardModel.remove(stale)
    }
    var existing = root.cardIndex(conversation, directKey)
    var label = name ? String(name) : ""
    if (existing >= 0) {
      var current = openCardModel.get(existing)
      root.updateCard(existing, { pinned: true,
        name: label || String(current.name || "") })
      return false
    }
    if (surfaceMode === "bundled")
      root.clearCards()
    var saved = root.persistedSurface(conversation, directKey)
    var geometry = saved ? {
      monitor: String(saved.monitor || preferredMonitor || root.instanceName || ""),
      surfaceX: isFinite(Number(saved.x)) ? Number(saved.x) : 40,
      surfaceY: isFinite(Number(saved.y)) ? Number(saved.y) : 80,
      surfaceWidth: root.restoredSize(saved.width, 420, 360),
      surfaceHeight: root.restoredSize(saved.height, 420, 420)
    } : root.initialGeometry(preferredMonitor)
    root.appendCard({ conversation: conversation, directKey: directKey,
      monitor: geometry.monitor, surfaceX: geometry.surfaceX,
      surfaceY: geometry.surfaceY, surfaceWidth: geometry.surfaceWidth,
      surfaceHeight: geometry.surfaceHeight, pinned: true, name: label,
      placeOnMap: true, explicitOpen: true })
    if (surfacesHydrated)
      service.setSurface(conversation, geometry.monitor, geometry.surfaceX,
        geometry.surfaceY, true, directKey, geometry.surfaceWidth,
        geometry.surfaceHeight)
    return true
  }

  function dismissCard(conv, expectedKey, width, height) {
    var conversation = String(conv || "")
    var bindingKey = String(expectedKey || "")
    var index = root.cardIndex(conversation, bindingKey)
    if (index < 0)
      return
    var removed = openCardModel.get(index)
    var removedMonitor = String(removed.monitor || "")
    var removedX = Number(removed.surfaceX || 0)
    var removedY = Number(removed.surfaceY || 0)
    var removedKey = String(removed.directKey || "")
    var savedWidth = root.restoredSize(width, Number(removed.surfaceWidth || 420), 360)
    var savedHeight = root.restoredSize(height, Number(removed.surfaceHeight || 420), 420)
    openCardModel.remove(index)
    service.setSurface(conversation, removedMonitor, removedX,
      removedY, false, removedKey, savedWidth, savedHeight)
  }

  function pin(conv, on, expectedKey) {
    var conversation = String(conv || "")
    var bindingKey = String(expectedKey || "")
    var index = root.cardIndex(conversation, bindingKey)
    if (index < 0)
      return
    var saved = openCardModel.get(index)
    root.updateCard(index, { pinned: !!on })
    service.setSurface(conversation, saved.monitor, saved.surfaceX, saved.surfaceY,
      !!on, String(saved.directKey || ""), saved.surfaceWidth, saved.surfaceHeight)
  }

  function savePos(conv, mon, x, y, pinned, expectedKey, width, height) {
    var conversation = String(conv || "")
    var binding = String(expectedKey || "")
    var index = root.cardIndex(conversation, binding)
    if (index >= 0)
      root.updateCard(index, { monitor: String(mon || ""),
        surfaceX: Math.round(Number(x || 0)), surfaceY: Math.round(Number(y || 0)),
        surfaceWidth: root.restoredSize(width, 420, 200),
        surfaceHeight: root.restoredSize(height, 420, 160) })
    service.setSurface(conversation, mon || "", x, y, !!pinned, binding,
      width, height)
  }

  function pinWindowAt(index) {
    return pinWindows.objectAt(index)
  }

  function toggleMute() {
    if (root.service && typeof root.service.toggleMute === "function")
      root.service.toggleMute()
    if (root.muted)
      sndProc.running = false
  }

  function packagedSoundFile(name) {
    var selectedSound = String(name || "")
    if (["qq", "msn", "aurora", "glow"].indexOf(selectedSound) >= 0)
      return selectedSound + ".oga"
    return selectedSound + ".wav"
  }

  function managedCustomSoundPath() {
    if (!root.service || root.service.helperCompatibility !== "compatible" ||
        !root.service.supportsCustomSounds)
      return ""
    var revision = Number(root.service.soundTick || 0)
    var sounds = root.service.customSounds || []
    for (var i = 0; revision >= 0 && i < sounds.length; i++) {
      var id = String(sounds[i].id || "")
      var path = String(sounds[i].path || "")
      if ((root.soundCustomId !== "" && id === root.soundCustomId) ||
          (root.soundCustomId === "" && root.soundCustom !== "" &&
           path === root.soundCustom))
        return /^[0-9a-f]{32}$/.test(id) ? path : ""
    }
    return ""
  }

  function stopUntrustedCustomSound() {
    if (root.activeCustomSoundPath === "")
      return
    if (root.managedCustomSoundPath() === root.activeCustomSoundPath)
      return
    sndProc.running = false
    root.activeCustomSoundId = ""
    root.activeCustomSoundPath = ""
  }

  function playNamedSound(name) {
    var selectedSound = String(name || "")
    if (root.muted || selectedSound === "off" || selectedSound === "")
      return
    var path = selectedSound === "custom" ? root.managedCustomSoundPath() : ""
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
    root.activeCustomSoundId = selectedSound === "custom" ? root.soundCustomId : ""
    root.activeCustomSoundPath = selectedSound === "custom" ? path : ""
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
    function onSoundTickChanged() { root.stopUntrustedCustomSound() }
    function onHelperCompatibilityChanged() { root.stopUntrustedCustomSound() }
    function onActiveHelperProtocolChanged() { root.stopUntrustedCustomSound() }
    function onHelperInstanceGenerationChanged() { root.stopUntrustedCustomSound() }
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
      root.clearCards()
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
      root.flushPendingSurfaceOpens()
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
  function applyGeometrySnapshot(raw) {
    var items
    try { items = JSON.parse(String(raw || "")) } catch (error) { return }
    if (root.geometrySnapshotGeneration !== root.geometryGeneration ||
        !Array.isArray(items) || items.length > 32)
      return
    for (var itemIndex = 0; itemIndex < items.length; itemIndex++) {
      var item = items[itemIndex] || ({})
      var title = String(item.title || "")
      var monitor = String(item.monitor || "")
      var x = Number(item.x)
      var y = Number(item.y)
      var width = Number(item.width)
      var height = Number(item.height)
      if (!title || monitor.length > 63 || /[\u0000-\u001f\u007f]/.test(monitor) ||
          typeof item.floating !== "boolean" ||
          !Number.isInteger(x) || !Number.isInteger(y) ||
          !Number.isInteger(width) || !Number.isInteger(height) ||
          x < -32768 || x > 32768 || y < -32768 || y > 32768 ||
          width < 360 || width > 4096 || height < 420 || height > 4096)
        continue
      for (var cardIndex = 0; cardIndex < openCardModel.count; cardIndex++) {
        var card = openCardModel.get(cardIndex)
        if (root.chatWindowTitle(card.conversation) !== title || card.placeOnMap)
          continue
        var pinWindow = root.pinWindowAt(cardIndex)
        if (pinWindow)
          pinWindow.compositorFloating = item.floating
        if (pinWindow &&
            (pinWindow.localResizePending || pinWindow.placementBusy)) {
          if (pinWindow.localResizePending && !pinWindow.placementBusy &&
              (Number(card.surfaceX) !== x || Number(card.surfaceY) !== y ||
               String(card.monitor || "") !== monitor))
            root.updateCard(cardIndex, { monitor: monitor, surfaceX: x,
              surfaceY: y })
          break
        }
        if (Number(card.surfaceX) !== x || Number(card.surfaceY) !== y ||
            Number(card.surfaceWidth) !== width || Number(card.surfaceHeight) !== height ||
            String(card.monitor || "") !== monitor) {
          var conversation = String(card.conversation || "")
          var directKey = String(card.directKey || "")
          root.updateCard(cardIndex, { monitor: monitor, surfaceX: x, surfaceY: y,
            surfaceWidth: width, surfaceHeight: height })
          service.setSurface(conversation, monitor, x, y, !!card.pinned,
            directKey, width, height)
        }
        break
      }
    }
  }

  Process {
    id: geometrySnapshot
    running: false
    command: [root.floatScriptPath, "list-geometry"]
    stdout: SplitParser {
      onRead: function(line) { root.applyGeometrySnapshot(line) }
    }
  }

  Timer {
    interval: 1200
    repeat: true
    running: root.isSurfaceOwner && root.floatRulesReady && openCardModel.count > 0
    triggeredOnStart: true
    onTriggered: if (!geometrySnapshot.running) {
      root.geometrySnapshotGeneration = root.geometryGeneration
      geometrySnapshot.running = true
    }
  }

  function overlayVisibleOn(screenName) {
    var i, c
    for (i = 0; i < openCardModel.count; i++) {
      c = openCardModel.get(i)
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
            required property string conversation
            required property string directKey
            required property string monitor
            required property real surfaceX
            required property real surfaceY
            required property real surfaceWidth
            required property real surfaceHeight
            required property bool pinned
            required property string name
            required property bool placeOnMap
            required property bool explicitOpen
            readonly property var modelData: ({ conversation: card.conversation,
              directKey: card.directKey, monitor: card.monitor,
              x: card.surfaceX, y: card.surfaceY, width: card.surfaceWidth,
              height: card.surfaceHeight, pinned: card.pinned, name: card.name,
              placeOnMap: card.placeOnMap, explicitOpen: card.explicitOpen })
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
                card.modelData.directKey || "", card.width, card.height)
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
    id: pinWindows
    model: root.isSurfaceOwner && root.floatRulesReady ? root.openCards : null
    delegate: FloatingWindow {
      id: pinWin
      required property string conversation
      required property string directKey
      required property string monitor
      required property real surfaceX
      required property real surfaceY
      required property real surfaceWidth
      required property real surfaceHeight
      required property bool pinned
      required property string name
      required property bool placeOnMap
      required property bool explicitOpen
      readonly property var modelData: ({ conversation: pinWin.conversation,
        directKey: pinWin.directKey, monitor: pinWin.monitor,
        x: pinWin.surfaceX, y: pinWin.surfaceY, width: pinWin.surfaceWidth,
        height: pinWin.surfaceHeight, pinned: pinWin.pinned, name: pinWin.name,
        placeOnMap: pinWin.placeOnMap, explicitOpen: pinWin.explicitOpen })
      visible: pinWin.pinned
      // Keep the first map title stable for the floating rule, then expose a
      // per-conversation title so an existing chat can be moved precisely.
      title: pinWin.everShown && pinWin.modelData
        ? root.chatWindowTitle(pinWin.modelData.conversation) : "OmaQ chat"
      property int desiredWidth: pinWin.boundedWidth(pinWin.surfaceWidth)
      property int desiredHeight: pinWin.boundedHeight(pinWin.surfaceHeight)
      property int pendingWidth: pinWin.boundedWidth(pinWin.surfaceWidth)
      property int pendingHeight: pinWin.boundedHeight(pinWin.surfaceHeight)
      property bool localResizePending: false
      implicitWidth: pinWin.desiredWidth
      implicitHeight: pinWin.desiredHeight
      minimumSize: Qt.size(360, 420)
      color: root.theme().bg || Color.background
      property bool everShown: false
      property bool closing: false
      property bool closePending: false
      property bool compositorFloating: false
      property string geometryObservationTitle: ""
      property bool geometryObservationForClose: false
      property var observedCurrentGeometry: null
      property bool geometryProcessStarted: false
      property bool geometryProcessHandled: false
      readonly property bool placementBusy: placement.busy

      function boundedWidth(value) {
        return Math.max(360, Math.min(4096,
          Math.round(Number(value || 420))))
      }

      function boundedHeight(value) {
        return Math.max(420, Math.min(4096,
          Math.round(Number(value || 420))))
      }

      function syncDesiredWidth() {
        var nextWidth = pinWin.boundedWidth(pinWin.surfaceWidth)
        if (pinWin.localResizePending && nextWidth !== pinWin.pendingWidth)
          return
        pinWin.desiredWidth = nextWidth
        if (!pinWin.localResizePending)
          pinWin.pendingWidth = nextWidth
      }

      function syncDesiredHeight() {
        var nextHeight = pinWin.boundedHeight(pinWin.surfaceHeight)
        if (pinWin.localResizePending && nextHeight !== pinWin.pendingHeight)
          return
        pinWin.desiredHeight = nextHeight
        if (!pinWin.localResizePending)
          pinWin.pendingHeight = nextHeight
      }

      function captureActualWidth() {
        if (!pinWin.everShown || pinWin.closing || pinWin.placeOnMap ||
            pinWin.placementBusy)
          return
        var nextWidth = pinWin.boundedWidth(pinWin.width)
        pinWin.desiredWidth = nextWidth
        pinWin.pendingWidth = nextWidth
        pinWin.localResizePending = true
        root.geometryGeneration++
        var index = root.cardIndex(pinWin.conversation, pinWin.directKey)
        if (index >= 0 && Number(pinWin.surfaceWidth) !== nextWidth)
          root.updateCard(index, { surfaceWidth: nextWidth })
        geometrySave.restart()
      }

      function captureActualHeight() {
        if (!pinWin.everShown || pinWin.closing || pinWin.placeOnMap ||
            pinWin.placementBusy)
          return
        var nextHeight = pinWin.boundedHeight(pinWin.height)
        pinWin.desiredHeight = nextHeight
        pinWin.pendingHeight = nextHeight
        pinWin.localResizePending = true
        root.geometryGeneration++
        var index = root.cardIndex(pinWin.conversation, pinWin.directKey)
        if (index >= 0 && Number(pinWin.surfaceHeight) !== nextHeight)
          root.updateCard(index, { surfaceHeight: nextHeight })
        geometrySave.restart()
      }

      function completeInitialPlacement(success, geometry) {
        root.geometryGeneration++
        var index = root.cardIndex(pinWin.conversation, pinWin.directKey)
        var values = { placeOnMap: false }
        if (success && geometry) {
          values.monitor = geometry.monitor
          values.surfaceX = geometry.x
          values.surfaceY = geometry.y
          values.surfaceWidth = geometry.width
          values.surfaceHeight = geometry.height
          pinWin.compositorFloating = true
          pinWin.desiredWidth = geometry.width
          pinWin.desiredHeight = geometry.height
          pinWin.pendingWidth = geometry.width
          pinWin.pendingHeight = geometry.height
          pinWin.localResizePending = false
        }
        if (index >= 0)
          root.updateCard(index, values)
        if (success && geometry)
          service.setSurface(pinWin.conversation, geometry.monitor,
            geometry.x, geometry.y, true, pinWin.directKey,
            geometry.width, geometry.height)
        else
          console.warn("OmaQ: could not restore independent chat geometry; manual geometry is enabled")
      }

      function applyRequestedFocus() {
        if (!placement.settled || pinWin.placeOnMap || !pinWin.modelData ||
            String(pinWin.modelData.conversation) !== root.focusConversation)
          return
        Qt.callLater(function() {
          if (pinWin.visible && placement.settled && !pinWin.placeOnMap)
            root.focusOmaQWindow(pinWin, pinWin.title)
        })
      }

      function completeRequestedFocus(moved) {
        var requestedConversation = pinWin.modelData
          ? String(pinWin.modelData.conversation) : ""
        if (!moved)
          console.warn("OmaQ: could not move chat to the current workspace")
        else if (placement.settled && pinWin.compositorFloating)
          Qt.callLater(function() { pinWin.requestCurrentGeometry(false) })
        var win = pinPage.QsWindow.window
        if (win && typeof win.requestActivate === "function")
          win.requestActivate()
        pinPage.focusComposer()
        if (root.focusConversation === requestedConversation)
          root.focusConversation = ""
      }

      function recordCurrentGeometry(raw) {
        var text = String(raw || "")
        var value
        if (text.length === 0 || text.length > 2048)
          return
        try { value = JSON.parse(text) } catch (error) { return }
        var monitor = String(value.monitor || "")
        if (String(value.title || "") !== pinWin.geometryObservationTitle ||
            value.floating !== true || monitor.length === 0 || monitor.length > 63 ||
            /[\u0000-\u001f\u007f]/.test(monitor) ||
            !Number.isInteger(value.x) || !Number.isInteger(value.y) ||
            !Number.isInteger(value.width) || !Number.isInteger(value.height) ||
            value.x < -32768 || value.x > 32768 ||
            value.y < -32768 || value.y > 32768 ||
            value.width < 360 || value.width > 4096 ||
            value.height < 420 || value.height > 4096)
          return
        pinWin.observedCurrentGeometry = { monitor: monitor, x: value.x,
          y: value.y, width: value.width, height: value.height, floating: true }
      }

      function applyCurrentGeometry(geometry) {
        if (!geometry)
          return
        root.geometryGeneration++
        pinWin.compositorFloating = true
        var index = root.cardIndex(pinWin.conversation, pinWin.directKey)
        var values = { monitor: geometry.monitor, surfaceX: geometry.x,
          surfaceY: geometry.y }
        var width = pinWin.pendingWidth
        var height = pinWin.pendingHeight
        if (pinWin.closePending || !pinWin.localResizePending) {
          width = geometry.width
          height = geometry.height
          values.surfaceWidth = width
          values.surfaceHeight = height
          pinWin.desiredWidth = width
          pinWin.desiredHeight = height
          pinWin.pendingWidth = width
          pinWin.pendingHeight = height
          pinWin.localResizePending = false
        }
        if (index >= 0)
          root.updateCard(index, values)
        service.setSurface(pinWin.conversation, geometry.monitor,
          geometry.x, geometry.y, true, pinWin.directKey, width, height)
      }

      function finishGeometryObservation(code) {
        var geometry = pinWin.observedCurrentGeometry
        var wasForClose = pinWin.geometryObservationForClose
        if (code === 0 && geometry)
          pinWin.applyCurrentGeometry(geometry)
        pinWin.observedCurrentGeometry = null
        pinWin.geometryObservationForClose = false
        if (pinWin.closePending && !wasForClose) {
          Qt.callLater(function() { pinWin.requestCurrentGeometry(true) })
          return
        }
        if (pinWin.closePending) {
          pinWin.closePending = false
          pinWin.performClose()
        }
      }

      function requestCurrentGeometry(closeAfter) {
        if (closeAfter)
          pinWin.closePending = true
        if (geometryObservation.running)
          return
        if (!placement.settled ||
            (!pinWin.compositorFloating && !closeAfter) ||
            pinWin.title === "OmaQ chat") {
          if (pinWin.closePending) {
            pinWin.closePending = false
            pinWin.performClose()
          }
          return
        }
        pinWin.geometryObservationTitle = pinWin.title
        pinWin.geometryObservationForClose = closeAfter
        pinWin.observedCurrentGeometry = null
        pinWin.geometryProcessStarted = false
        pinWin.geometryProcessHandled = false
        geometryObservation.command = ["/usr/bin/timeout", "--kill-after=1s", "2s",
          root.floatScriptPath, "observe-title", pinWin.geometryObservationTitle]
        geometryObservation.running = true
      }

      function performClose() {
        if (pinWin.closing || !pinWin.modelData)
          return
        dragObservationTimer.stop()
        if (pinPage.inCall || pinPage.incoming)
          pinPage.hangUp()
        pinWin.closing = true
        pinWin.visible = false
        root.dismissCard(pinWin.modelData.conversation,
          pinWin.modelData.directKey || "", pinWin.width, pinWin.height)
      }

      function closeWindow() {
        if (pinWin.closing || pinWin.closePending || !pinWin.modelData)
          return
        dragObservationTimer.stop()
        if (placement.busy) {
          pinWin.closePending = true
          placement.cancel()
          return
        }
        if (placement.settled && pinWin.compositorFloating) {
          pinWin.requestCurrentGeometry(true)
          return
        }
        pinWin.performClose()
      }

      PlacementController {
        id: placement
        scriptPath: root.floatScriptPath
        windowTitle: pinWin.title
        placementRequested: pinWin.placeOnMap
        windowReady: pinWin.backingWindowVisible && pinWin.title !== "OmaQ chat"
        requestedX: Math.round(pinWin.surfaceX)
        requestedY: Math.round(pinWin.surfaceY)
        requestedWidth: pinWin.boundedWidth(pinWin.surfaceWidth)
        requestedHeight: pinWin.boundedHeight(pinWin.surfaceHeight)
        onPlacementFinished: function(success, geometry) {
          pinWin.completeInitialPlacement(success, geometry)
        }
        onPlacementCanceled: {
          if (pinWin.closePending)
            pinWin.requestCurrentGeometry(true)
        }
      }

      Component.onCompleted: {
        if (pinWin.backingWindowVisible)
          pinWin.everShown = true
        Qt.callLater(function() {
          placement.begin()
          placement.settleWithoutPlacement()
          pinWin.applyRequestedFocus()
        })
      }
      onBackingWindowVisibleChanged: {
        if (pinWin.backingWindowVisible) {
          pinWin.everShown = true
          Qt.callLater(placement.begin)
        }
      }
      onTitleChanged: Qt.callLater(placement.begin)
      onPlaceOnMapChanged: {
        if (!pinWin.placeOnMap) {
          placement.settleWithoutPlacement()
          Qt.callLater(pinWin.applyRequestedFocus)
        }
      }
      onSurfaceWidthChanged: pinWin.syncDesiredWidth()
      onSurfaceHeightChanged: pinWin.syncDesiredHeight()
      onWidthChanged: pinWin.captureActualWidth()
      onHeightChanged: pinWin.captureActualHeight()

      Process {
        id: geometryObservation
        running: false
        stdout: SplitParser {
          onRead: function(line) { pinWin.recordCurrentGeometry(line) }
        }
        onStarted: pinWin.geometryProcessStarted = true
        onExited: function(code) {
          pinWin.geometryProcessHandled = true
          pinWin.finishGeometryObservation(code)
        }
        onRunningChanged: {
          if (!running && !pinWin.geometryProcessHandled &&
              !pinWin.geometryProcessStarted &&
              (pinWin.closePending || pinWin.geometryObservationTitle !== "")) {
            pinWin.geometryProcessHandled = true
            pinWin.finishGeometryObservation(127)
          }
        }
      }

      Timer {
        id: dragObservationTimer
        interval: 250
        repeat: true
        property int attempts: 0
        onTriggered: {
          dragObservationTimer.attempts++
          pinWin.requestCurrentGeometry(false)
          if (dragObservationTimer.attempts >= 20)
            dragObservationTimer.stop()
        }
      }

      Timer {
        id: geometrySave
        interval: 350
        repeat: false
        onTriggered: {
          if (pinWin.visible && !pinWin.closing)
            root.savePos(pinWin.conversation, pinWin.monitor, pinWin.surfaceX,
              pinWin.surfaceY, true, pinWin.directKey, pinWin.pendingWidth,
              pinWin.pendingHeight)
          pinWin.localResizePending = false
        }
      }

      onVisibleChanged: {
        if (visible) {
          pinWin.everShown = true
          pinWin.closing = false
          return
        }
        if (root.isSurfaceOwner && !root.ownershipTeardown &&
            !pinWin.closing && pinWin.everShown && pinWin.modelData &&
            pinWin.modelData.conversation)
          pinWin.closeWindow()
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
          Item {
            id: chatDragHandle
            Layout.fillWidth: true
            Layout.preferredHeight: Style.space(28)
            enabled: placement.settled && !pinWin.placeOnMap &&
              pinWin.compositorFloating && !pinWin.closing

            Text {
              anchors.centerIn: parent
              text: "drag_indicator"
              color: chatDragHandle.enabled ? root.theme().fg : "transparent"
              opacity: 0.55
              font.family: "Material Symbols Rounded"
              font.pixelSize: Style.font.body
              font.variableAxes: ({ "FILL": 0, "wght": 400 })
            }

            MouseArea {
              anchors.fill: parent
              enabled: chatDragHandle.enabled
              acceptedButtons: Qt.LeftButton
              cursorShape: Qt.SizeAllCursor
              onPressed: function(mouse) {
                if (!pinWin.startSystemMove()) {
                  mouse.accepted = false
                  return
                }
                dragObservationTimer.attempts = 0
                dragObservationTimer.restart()
              }
              onReleased: pinWin.requestCurrentGeometry(false)
              onCanceled: pinWin.requestCurrentGeometry(false)
            }
          }
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
            onClicked: pinWin.closeWindow()
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
