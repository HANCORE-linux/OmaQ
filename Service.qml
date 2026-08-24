import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  property var settings: ({})
  property bool muted: false
  property int unreadCount: 0
  property var unreadByConversation: ({})
  property bool authoritativeUnreadSeen: false
  property int unreadTick: 0
  property var unreadClearPendingByConversation: ({})
  property var unreadClearRetryAfter: ({})
  property string statusText: "OmaQ"
  property string lastError: ""
  property string lastErrorConv: ""
  property int lastErrorTick: 0
  property bool attached: false
  property bool procReady: false
  property var pendingOps: []
  property var inFlightMessages: ({})
  property int backoffMs: 200
  property string helperInstance: ""
  property bool awaitingHelperInstance: false
  property int helperHandshakeTick: 0
  property int helperStatusSequence: 0
  property string helperStatusNonce: ""
  property string helperCompatibility: "unknown"
  property int legacyHandshakeAttempts: 0
  property bool legacySnapshotSeen: false
  property var pendingHandshakeEvents: []
  property bool handshakeEventOverflow: false
  property int pendingHandshakeBytes: 0
  readonly property int handshakeEventByteLimit: 4 * 1024 * 1024
  property int helperProtocolPid: 0
  property int helperProtocolVersion: 0
  property string helperProtocolInstance: ""
  readonly property string helperLaunchNonce: Date.now().toString(36) + "-" +
    Math.floor(Math.random() * 0x100000000).toString(36)
  property string helperProtocolNonce: ""
  readonly property int requiredHelperProtocol: 3
  readonly property bool localHelperProtocolConfirmed: !root.attached && proc.processId > 0 &&
    root.helperProtocolPid === proc.processId &&
    root.helperProtocolVersion >= root.requiredHelperProtocol &&
    root.helperProtocolNonce === root.helperLaunchNonce &&
    /^[0-9a-f]{32}$/.test(root.helperProtocolInstance)
  property string identityFingerprint: ""
  property string inviteUrl: ""
  property string qrPath: ""
  property string safetyCode: ""
  property string safetyConv: ""
  property bool pending: false
  property string lastConversation: "0"
  property string lastDirectId: ""
  property string lastAddr: ""
  property string lastGroup: ""
  property bool pendingGroup: false
  property string lastChatText: ""
  property string lastChatId: ""
  property string lastChatReply: ""
  property string lastChatDir: ""
  property string lastChatKind: ""
  property string lastChatRequest: ""
  property string lastMessageFailedConv: ""
  property string lastMessageFailedRequest: ""
  property string lastMessageFailedCode: ""
  property bool lastMessageFailedDelivered: false
  property int messageFailedTick: 0
  property string lastUpdateConv: ""
  property string lastUpdateId: ""
  property string lastUpdateText: ""
  property bool lastUpdateDeleted: false
  property bool lastUpdateEdited: false
  property int updateTick: 0
  property string lastReactionConv: ""
  property string lastReactionId: ""
  property string lastReactionEmoji: ""
  property string lastReactionActor: ""
  property int reactionTick: 0
  property string lastReactionFailedConv: ""
  property string lastReactionFailedId: ""
  property string lastReactionFailedCode: ""
  property int reactionFailedTick: 0
  property string lastUnreadFailedConv: ""
  property string lastUnreadFailedCode: ""
  property int unreadFailedTick: 0
  property string unreadWarning: ""
  property string lastChatConv: ""
  property int messageTick: 0
  property var lastHistoryItems: []
  property bool lastHistoryCleared: false
  property string lastHistoryConv: ""
  property int lastHistoryUnreadCount: 0
  property string lastHistoryUnreadConv: ""
  property int historyTick: 0
  property var pendingHistoryUnread: ({})
  property var historyRetryTickByConversation: ({})
  property var historyRequestByConversation: ({})
  property int historyRequestSequence: 0
  property int reconnectGeneration: 0
  property bool peerTyping: false
  property string lastTypingConv: ""
  property string lastReceiptConv: ""
  property string lastReceiptId: ""
  property string lastReceiptState: ""
  property int receiptTick: 0
  property string lastReceiptSentConv: ""
  property string lastReceiptSentId: ""
  property string lastReceiptSentState: ""
  property int receiptSentTick: 0
  property string lastReceiptFailedConv: ""
  property string lastReceiptFailedId: ""
  property string lastReceiptFailedState: ""
  property string lastReceiptFailedCode: ""
  property int receiptFailedTick: 0
  property var peerTypingByConv: ({})
  property int typingTick: 0
  property var lastSurface: ({})
  property var surfaces: []
  property int surfacesTick: 0
  property string lastFileId: ""
  property string lastFileName: ""
  property string lastFilePath: ""
  property string lastFileConv: ""
  property string lastFileState: ""
  property string lastFileDir: ""
  property string lastFileError: ""
  property int lastFileTick: 0
  property bool pendingFile: false
  property var fileOffers: ({})
  property var outgoingFiles: ({})
  property int fileRequestSequence: 0
  property bool incomingCall: false
  property string lastCallState: ""
  property string lastCallConv: ""
  property bool locked: false
  property bool saveProtected: false
  property var friends: []
  property var searchItems: []
  property int searchTick: 0
  property string selfAvatar: ""
  property int avatarTick: 0
  property string selfNickname: ""
  property int nicknameTick: 0
  property int identityTick: 0
  property bool selfOnline: false
  property string connectionState: "starting"
  property bool everOnline: false
  property bool recoveringHelper: false

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

  function localUnreadTotal() {
    var total = 0
    var key
    for (key in root.unreadByConversation)
      total += Number(root.unreadByConversation[key] || 0)
    return total
  }

  function handleLine(line) {
    var ev
    var replayEventsToApply = null
    var replayOverflowToReport = false
    try { ev = JSON.parse(line) } catch (e) { return }
    var fatalRecoveryError = ev.event === "error" && ev.code === "identity_rollback_failed"
    var identityCleanupError = ev.event === "error" &&
      ev.code === "identity_backup_cleanup_failed"
    var recoveryLifecycle = fatalRecoveryError || identityCleanupError ||
      (ev.event === "identity" && ev.op === "recovered")
    if (root.helperCompatibility === "incompatible" && !recoveryLifecycle)
      return
    if (root.awaitingHelperInstance && ev.event !== "snapshot" && !recoveryLifecycle) {
      if (!root.handshakeEventOverflow) {
        var bufferedLine = String(line)
        var bufferedBytes = bufferedLine.length * 3 + 1
        if (bufferedBytes > root.handshakeEventByteLimit - root.pendingHandshakeBytes) {
          root.pendingHandshakeEvents = []
          root.pendingHandshakeBytes = 0
          root.handshakeEventOverflow = true
        } else {
          root.pendingHandshakeEvents.push(bufferedLine)
          root.pendingHandshakeBytes = root.pendingHandshakeBytes + bufferedBytes
        }
      }
      return
    }
    if (ev.event === "snapshot") {
      var handshakeMatches = ev.instance && root.awaitingHelperInstance && ev.request &&
        String(ev.request) === root.helperStatusNonce
      if (root.awaitingHelperInstance && !handshakeMatches) {
        if (!ev.instance || !ev.request)
          root.legacySnapshotSeen = true
        return
      }
      if (handshakeMatches) {
        var snapshotProtocol = Number(ev.protocol)
        if (!Number.isInteger(snapshotProtocol) ||
            snapshotProtocol < root.requiredHelperProtocol) {
          root.markHelperIncompatible()
          return
        }
        var nextInstance = String(ev.instance)
        var previousInstance = root.helperInstance
        var processChanged = previousInstance && previousInstance !== nextInstance
        var nextIdentity = String(ev.addr || "").slice(0, 64)
        var identityChanged = root.identityFingerprint !== "" && nextIdentity !== "" &&
          root.identityFingerprint !== nextIdentity
        if (processChanged)
          root.failActiveOutgoingFiles("helper_restarted")
        if (identityChanged)
          root.resetStateForIdentity()
        root.helperInstance = nextInstance
        if (nextIdentity !== "")
          root.identityFingerprint = nextIdentity
        root.helperCompatibility = "compatible"
        root.legacyHandshakeAttempts = 0
        root.legacySnapshotSeen = false
        root.authoritativeUnreadSeen = true
        root.unreadByConversation = ({})
        if (identityChanged)
          root.identityTick = root.identityTick + 1
        else
          root.helperHandshakeTick = root.helperHandshakeTick + 1
        if (root.lastError === "helper_down" || root.lastError === "helper_incompatible" ||
            root.lastError === "helper_handshake_pending" ||
            root.lastError === "identity_backup_cleanup_failed")
          root.lastError = ""
        root.awaitingHelperInstance = false
        root.helperStatusNonce = ""
        helperStatusTimer.stop()
        root.sendOp({ op: "identity.ready", id: nextInstance })
        root.flushOps()
        replayOverflowToReport = !identityChanged && root.handshakeEventOverflow
        replayEventsToApply = identityChanged || replayOverflowToReport
          ? [] : root.pendingHandshakeEvents
        root.pendingHandshakeEvents = []
        root.pendingHandshakeBytes = 0
        root.handshakeEventOverflow = false
        if (previousInstance && previousInstance === nextInstance)
          root.reconcileOutgoingFiles()
      }
      if (ev.unread !== undefined && handshakeMatches)
        root.unreadCount = Math.max(0, Number(ev.unread || 0))
      if (ev.addr)
        root.lastAddr = ev.addr
      if (ev.locked !== undefined)
        root.locked = !!ev.locked
      if (ev.locked === true) {
        root.connectionState = "locked"
      } else if (ev.online !== undefined) {
        if (ev.online) {
          root.connectionState = "online"
          root.everOnline = true
          root.recoveringHelper = false
        } else {
          root.connectionState = root.everOnline || root.recoveringHelper
            ? "reconnecting" : "connecting"
        }
      }
      if (ev.protected !== undefined)
        root.saveProtected = !!ev.protected
      if (ev.locked === true)
        root.lastError = "locked"
      else if (root.lastError !== "helper_down" && root.lastError !== "helper_incompatible" &&
               root.lastError !== "identity_rollback_failed" &&
               root.lastError !== "identity_backup_cleanup_failed")
        root.lastError = ""
      if (ev.online !== undefined)
        root.selfOnline = !!ev.online
      if (ev.nickname !== undefined)
        root.selfNickname = String(ev.nickname || "")
    }
    if (replayEventsToApply !== null) {
      if (replayOverflowToReport) {
        root.lastError = "helper_event_overflow"
        root.lastErrorConv = ""
        root.lastErrorTick = root.lastErrorTick + 1
      } else {
        for (var replayIndex = 0; replayIndex < replayEventsToApply.length; replayIndex++)
          root.handleLine(replayEventsToApply[replayIndex])
      }
    }
    if (ev.event === "connection" && !root.awaitingHelperInstance) {
      if (ev.state === "online") {
        root.connectionState = "online"
        root.everOnline = true
        root.recoveringHelper = false
      } else {
        root.connectionState = root.everOnline || root.recoveringHelper
          ? "reconnecting" : "connecting"
      }
      root.selfOnline = root.connectionState === "online"
    }
    if (ev.event === "nickname") {
      root.selfNickname = String(ev.value || "")
      if (root.lastError !== "helper_down" && root.lastError !== "helper_incompatible" &&
          root.lastError !== "identity_rollback_failed" &&
          root.lastError !== "identity_backup_cleanup_failed")
        root.lastError = ""
      root.nicknameTick = root.nicknameTick + 1
    }
    if (ev.event === "error") {
      root.lastError = ev.code || "error"
      root.lastErrorConv = ev.conversation || ""
      root.lastErrorTick = root.lastErrorTick + 1
      if (ev.code === "locked") {
        root.locked = true
        root.connectionState = "locked"
      }
    }
    if (ev.event === "friends") {
      if (!root.locked && root.lastError !== "helper_down" &&
          root.lastError !== "helper_incompatible" &&
          root.lastError !== "identity_rollback_failed" &&
          root.lastError !== "identity_backup_cleanup_failed")
        root.lastError = ""
      root.friends = ev.items || []
      if (root.lastDirectId) {
        var stillFriend = false
        for (var fi = 0; fi < root.friends.length; fi++) {
          if (String(root.friends[fi].id) === String(root.lastDirectId)) {
            stillFriend = true
            break
          }
        }
        if (!stillFriend && root.lastConversation === root.lastDirectId) {
          root.lastDirectId = ""
          root.lastConversation = ""
          root.safetyCode = ""
          root.safetyConv = ""
        }
      }
    }
    if (ev.event === "avatar") {
      var id = ev.id || ""
      var path = ev.path || ""
      root.avatarTick = root.avatarTick + 1
      if (id === "self") {
        root.selfAvatar = path
      } else if (id) {
        var next = (root.friends || []).slice()
        var i, found = false
        for (i = 0; i < next.length; i++) {
          if (String(next[i].id) === String(id)) {
            next[i] = { id: next[i].id, name: next[i].name, avatar: path, online: !!next[i].online }
            found = true
          }
        }
        if (found)
          root.friends = next
      }
    }
    if (ev.event === "identity") {
      if (ev.op === "recovered" && root.lastError === "identity_rollback_failed")
        root.lastError = ""
      if (ev.op === "unlock")
        root.locked = false
      if (ev.op === "import") {
        root.resetStateForIdentity()
        root.identityFingerprint = ""
        root.identityTick = root.identityTick + 1
      }
      if (ev.op === "import" || ev.op === "unlock")
        root.requestHelperStatus()
      if (ev.protected !== undefined)
        root.saveProtected = !!ev.protected
    }
    if (ev.event === "unread") {
      var unreadConversation = String(ev.conversation || "")
      var unreadCountForConversation = Math.max(0, Number(ev.count || 0))
      var clearWasPending = !!root.unreadClearPendingByConversation[unreadConversation]
      var authoritativeNext = {}
      var authoritativeKey
      for (authoritativeKey in root.unreadByConversation) {
        if (authoritativeKey !== unreadConversation)
          authoritativeNext[authoritativeKey] = root.unreadByConversation[authoritativeKey]
      }
      if (unreadConversation && unreadCountForConversation > 0)
        authoritativeNext[unreadConversation] = unreadCountForConversation
      root.unreadByConversation = authoritativeNext
      if (clearWasPending) {
        var pendingClearNext = {}
        var retryAfterNext = {}
        var pendingClearKey
        for (pendingClearKey in root.unreadClearPendingByConversation) {
          if (pendingClearKey !== unreadConversation)
            pendingClearNext[pendingClearKey] = root.unreadClearPendingByConversation[pendingClearKey]
        }
        for (pendingClearKey in root.unreadClearRetryAfter) {
          if (pendingClearKey !== unreadConversation)
            retryAfterNext[pendingClearKey] = root.unreadClearRetryAfter[pendingClearKey]
        }
        if (unreadCountForConversation > 0)
          retryAfterNext[unreadConversation] = Date.now() + 5000
        root.unreadClearPendingByConversation = pendingClearNext
        root.unreadClearRetryAfter = retryAfterNext
      }
      root.unreadCount = ev.total !== undefined
        ? Math.max(0, Number(ev.total || 0)) : root.localUnreadTotal()
      root.authoritativeUnreadSeen = true
      root.unreadWarning = ""
      root.unreadTick = root.unreadTick + 1
    }
    if (ev.event === "message.failed") {
      root.finishInFlightMessage(ev.request)
      root.lastMessageFailedConv = String(ev.conversation || "")
      root.lastMessageFailedRequest = String(ev.request || "")
      root.lastMessageFailedCode = String(ev.code || "error")
      root.lastMessageFailedDelivered = !!ev.delivered
      root.messageFailedTick = root.messageFailedTick + 1
    }
    if (ev.event === "message") {
      if (ev.dir === "out")
        root.finishInFlightMessage(ev.request)
      root.lastChatId = String(ev.id || "")
      root.lastChatReply = String(ev.reply || "")
      root.lastChatDir = ev.dir === "out" ? "out" : "in"
      root.lastChatKind = String(ev.kind || "")
      root.lastChatRequest = String(ev.request || "")
      if (root.lastChatDir !== "out" &&
          (!root.authoritativeUnreadSeen || root.helperCompatibility === "incompatible")) {
        root.unreadCount = root.unreadCount + 1
        var unreadNext = {}
        var unreadKey
        for (unreadKey in root.unreadByConversation)
          unreadNext[unreadKey] = root.unreadByConversation[unreadKey]
        var unreadConv = String(ev.conversation || root.lastConversation || "")
        if (unreadConv)
          unreadNext[unreadConv] = Number(unreadNext[unreadConv] || 0) + 1
        root.unreadByConversation = unreadNext
      }
      if (ev.conversation) {
        root.lastConversation = ev.conversation
        if (String(ev.conversation).charAt(0) !== "g")
          root.lastDirectId = String(ev.conversation)
      }
      root.lastChatConv = ev.conversation || root.lastConversation
      root.lastChatText = ev.text || ""
      root.messageTick = root.messageTick + 1
    }
    if (ev.event === "search") {
      if (ev.conversation && root.lastConversation && String(ev.conversation) !== String(root.lastConversation))
        return
      root.searchItems = ev.items || []
      root.searchTick = root.searchTick + 1
    }
    if (ev.event === "message.updated") {
      root.lastUpdateConv = String(ev.conversation || "")
      root.lastUpdateId = String(ev.id || "")
      root.lastUpdateText = String(ev.text || "")
      root.lastUpdateDeleted = !!ev.deleted
      root.lastUpdateEdited = !!ev.edited
      root.updateTick = root.updateTick + 1
    }
    if (ev.event === "message.reaction") {
      root.lastReactionConv = String(ev.conversation || "")
      root.lastReactionId = String(ev.id || "")
      root.lastReactionEmoji = String(ev.emoji || "")
      root.lastReactionActor = ev.actor === "me" ? "me" : "peer"
      root.reactionTick = root.reactionTick + 1
    }
    if (ev.event === "message.reaction.failed") {
      root.lastReactionFailedConv = String(ev.conversation || "")
      root.lastReactionFailedId = String(ev.id || "")
      root.lastReactionFailedCode = String(ev.code || "forbidden")
      root.reactionFailedTick = root.reactionFailedTick + 1
    }
    if (ev.event === "unread.failed") {
      root.lastUnreadFailedConv = String(ev.conversation || "")
      root.lastUnreadFailedCode = String(ev.code || "unread_persist_failed")
      root.unreadWarning = root.lastUnreadFailedCode === "unread_state_invalid"
        ? "Unread state could not be loaded."
        : "Unread state could not be saved."
      root.unreadFailedTick = root.unreadFailedTick + 1
    }
    if (ev.event === "history") {
      var historyConv = String(ev.conversation || "")
      var expectedHistoryRequest = String(root.historyRequestByConversation[historyConv] || "")
      if (!ev.cleared && (!expectedHistoryRequest ||
          String(ev.request || "") !== expectedHistoryRequest))
        return
      var historyRequestNext = {}
      var historyRequestKey
      for (historyRequestKey in root.historyRequestByConversation) {
        if (historyRequestKey !== historyConv)
          historyRequestNext[historyRequestKey] = root.historyRequestByConversation[historyRequestKey]
      }
      root.historyRequestByConversation = historyRequestNext
      var historyQueue = root.pendingHistoryUnread[historyConv] || []
      var historyUnread = historyQueue.length > 0 ? Number(historyQueue[0] || 0) : 0
      var historyPendingNext = {}
      var historyPendingKey
      for (historyPendingKey in root.pendingHistoryUnread) {
        if (historyPendingKey !== historyConv)
          historyPendingNext[historyPendingKey] = root.pendingHistoryUnread[historyPendingKey]
      }
      if (historyQueue.length > 1)
        historyPendingNext[historyConv] = historyQueue.slice(1)
      root.pendingHistoryUnread = historyPendingNext
      root.lastHistoryUnreadConv = historyConv
      root.lastHistoryUnreadCount = historyUnread
      root.lastHistoryCleared = !!ev.cleared
      root.lastHistoryConv = historyConv
      root.lastHistoryItems = ev.items || []
      root.historyTick = root.historyTick + 1
    }
    if (ev.event === "receipt") {
      root.lastReceiptConv = String(ev.conversation || "")
      root.lastReceiptId = String(ev.id || "")
      root.lastReceiptState = String(ev.state || "")
      root.receiptTick = root.receiptTick + 1
    }
    if (ev.event === "receipt.sent") {
      root.lastReceiptSentConv = String(ev.conversation || "")
      root.lastReceiptSentId = String(ev.id || "")
      root.lastReceiptSentState = String(ev.state || "")
      root.receiptSentTick = root.receiptSentTick + 1
    }
    if (ev.event === "receipt.failed") {
      root.lastReceiptFailedConv = String(ev.conversation || "")
      root.lastReceiptFailedId = String(ev.id || "")
      root.lastReceiptFailedState = String(ev.state || "")
      root.lastReceiptFailedCode = String(ev.code || "forbidden")
      root.receiptFailedTick = root.receiptFailedTick + 1
    }
    if (ev.event === "typing") {
      var typingConv = String(ev.conversation || "")
      var typingNext = {}
      var typingKey
      for (typingKey in root.peerTypingByConv)
        typingNext[typingKey] = root.peerTypingByConv[typingKey]
      if (ev.typing)
        typingNext[typingConv] = Date.now() + 4500
      else
        delete typingNext[typingConv]
      root.peerTypingByConv = typingNext
      root.lastTypingConv = typingConv
      root.peerTyping = root.isPeerTyping(typingConv)
      root.typingTick = root.typingTick + 1
    }
    if (ev.event === "surface")
      root.lastSurface = ev
    if (ev.event === "surfaces") {
      root.surfaces = ev.items || []
      root.surfacesTick = root.surfacesTick + 1
    }
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
      if ((ev.action === "dissolve" || ev.action === "leave") && ev.group === root.lastGroup &&
          (ev.action === "dissolve" || String(ev.peer || "") === "0"))
        root.lastGroup = ""
      else if (ev.group)
        root.lastGroup = ev.group
      if (ev.action === "create" || ev.action === "join")
        root.lastConversation = ev.group || root.lastConversation
    }
    if (ev.event === "safety") {
      root.safetyCode = ev.code || ""
      root.safetyConv = ev.conversation || root.lastConversation
      if (ev.conversation) {
        root.lastConversation = ev.conversation
        if (String(ev.conversation).charAt(0) !== "g")
          root.lastDirectId = String(ev.conversation)
      }
    }
    if (ev.event === "file.offer") {
      root.lastFileState = "offer"
      root.lastFileDir = "in"
      root.lastFileError = ""
      var offerConv = String(ev.conversation || root.lastConversation)
      root.setFileOffer(offerConv, { id: ev.id || "", name: ev.name || "", path: "", pending: true })
      root.lastFileId = ev.id || ""
      root.lastFileName = ev.name || ""
      root.pendingFile = true
      root.lastConversation = offerConv
      root.lastFileConv = offerConv
      if (offerConv.charAt(0) !== "g")
        root.lastDirectId = offerConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.sending" && ev.dir === "out") {
      var sendingConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var sendingOld = root.outgoingFile(sendingConv)
      if (!root.outgoingEventMatches(ev, sendingConv))
        return
      root.setOutgoingFile(sendingConv, {
        id: String(ev.id || sendingOld.id || ""),
        path: String(sendingOld.path || ""),
        request: String(sendingOld.request || ev.request || ""),
        pending: true,
        cancelRequested: !!sendingOld.cancelRequested
      })
      root.lastFileId = String(ev.id || "")
      root.lastFileConv = sendingConv
      root.lastFileDir = "out"
      if (sendingOld.cancelRequested && ev.id)
        root.sendOp({ op: "file.cancel", id: String(ev.id) })
      else
        root.lastFileState = "sending"
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.done") {
      var doneConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var doneDir = root.fileEventDirection(ev, doneConv)
      if (doneDir === "out" && !root.outgoingEventMatches(ev, doneConv))
        return
      root.lastFileState = "done"
      root.lastFileError = ""
      root.lastFileDir = doneDir
      if (doneDir === "out") {
        var doneOutgoing = root.outgoingFile(doneConv)
        if (doneOutgoing.pending && (!doneOutgoing.id || String(doneOutgoing.id) === String(ev.id || "")))
          root.setOutgoingFile(doneConv, { id: doneOutgoing.id || ev.id || "", path: doneOutgoing.path || "", request: doneOutgoing.request || "", pending: false, cancelRequested: false })
        root.lastFilePath = ""
      } else {
        var doneOld = root.fileOffer(doneConv)
        root.setFileOffer(doneConv, { id: ev.id || doneOld.id || "", name: doneOld.name || root.lastFileName, path: ev.path || "", pending: false })
        root.pendingFile = false
        root.lastFilePath = ev.path || ""
      }
      root.lastConversation = doneConv
      root.lastFileConv = doneConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.canceled") {
      var canceledConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var canceledDir = root.fileEventDirection(ev, canceledConv)
      if (canceledDir === "out" && !root.outgoingEventMatches(ev, canceledConv))
        return
      root.lastFileState = "canceled"
      root.lastFileError = ""
      root.lastFileDir = canceledDir
      if (canceledDir === "out") {
        var canceledOutgoing = root.outgoingFile(canceledConv)
        if (!canceledOutgoing.id || String(canceledOutgoing.id) === String(ev.id || ""))
          root.setOutgoingFile(canceledConv, { id: canceledOutgoing.id || ev.id || "", path: canceledOutgoing.path || "", request: canceledOutgoing.request || "", pending: false, cancelRequested: false })
      } else {
        var canceledOffer = root.fileOffer(canceledConv)
        root.setFileOffer(canceledConv, { id: canceledOffer.id || ev.id || "", name: canceledOffer.name || "", path: "", pending: false })
        root.pendingFile = false
      }
      root.lastFilePath = ""
      root.lastFileConv = canceledConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.failed") {
      var failedConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var failedDir = root.fileEventDirection(ev, failedConv)
      if (failedDir === "out" && !root.outgoingEventMatches(ev, failedConv))
        return
      root.lastFileState = "failed"
      root.lastFileError = ev.code || "file_failed"
      root.lastFileDir = failedDir
      if (failedDir === "out") {
        var failedOutgoing = root.outgoingFile(failedConv)
        if (!failedOutgoing.id || String(failedOutgoing.id) === String(ev.id || ""))
          root.setOutgoingFile(failedConv, { id: failedOutgoing.id || ev.id || "", path: failedOutgoing.path || "", request: failedOutgoing.request || "", pending: false, cancelRequested: false })
      } else {
        var failedOld = root.fileOffer(failedConv)
        root.setFileOffer(failedConv, { id: failedOld.id || ev.id || "", name: failedOld.name || "", path: "", pending: false })
        root.pendingFile = false
      }
      root.lastError = "file_failed"
      root.lastErrorConv = failedConv
      root.lastErrorTick = root.lastErrorTick + 1
      root.lastFileConv = failedConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "call.incoming") {
      root.incomingCall = true
      root.lastCallState = "incoming"
      if (ev.conversation)
        root.lastConversation = ev.conversation
      root.lastCallConv = ev.conversation || root.lastConversation
      if (root.lastCallConv && String(root.lastCallConv).charAt(0) !== "g")
        root.lastDirectId = String(root.lastCallConv)
    }
    if (ev.event === "call.state") {
      root.lastCallState = ev.state || ""
      if (ev.state === "ended" || ev.state === "")
        root.incomingCall = false
      if (ev.conversation)
        root.lastConversation = ev.conversation
      if (ev.conversation)
        root.lastCallConv = ev.conversation
      if (ev.conversation && String(ev.conversation).charAt(0) !== "g")
        root.lastDirectId = String(ev.conversation)
    }
  }

  function fileOffer(conv) {
    var c = String(conv || "")
    return c && root.fileOffers[c] ? root.fileOffers[c] : ({})
  }
  function setFileOffer(conv, offer) {
    var next = {}
    var key
    for (key in root.fileOffers)
      next[key] = root.fileOffers[key]
    next[String(conv)] = offer
    root.fileOffers = next
  }
  function filePending(conv) { return !!fileOffer(conv).pending }
  function fileNameFor(conv) { return fileOffer(conv).name || "" }
  function filePathFor(conv) { return fileOffer(conv).path || "" }
  function outgoingFile(conv) {
    var c = String(conv || "")
    return c && root.outgoingFiles[c] ? root.outgoingFiles[c] : ({})
  }
  function setOutgoingFile(conv, transfer) {
    var next = {}
    var key
    for (key in root.outgoingFiles)
      next[key] = root.outgoingFiles[key]
    next[String(conv)] = transfer
    root.outgoingFiles = next
  }
  function fileSendingFor(conv) {
    var transfer = root.outgoingFile(conv)
    return !!transfer.pending && !transfer.cancelRequested
  }
  function fileEventDirection(ev, conv) {
    if (ev && (ev.dir === "in" || ev.dir === "out"))
      return ev.dir
    if (ev && ev.path)
      return "in"
    var transfer = root.outgoingFile(conv)
    if (transfer.pending && (!transfer.id || !ev || !ev.id || String(transfer.id) === String(ev.id)))
      return "out"
    return "in"
  }
  function outgoingEventMatches(ev, conv) {
    var transfer = root.outgoingFile(conv)
    if (!transfer.pending || !ev)
      return false
    var eventRequest = String(ev.request || "")
    var request = String(transfer.request || "")
    if (eventRequest && request !== eventRequest)
      return false
    if (!transfer.id)
      return request !== "" && eventRequest !== "" && request === eventRequest
    if (ev.id)
      return String(transfer.id) === String(ev.id)
    return request !== "" && eventRequest !== "" && request === eventRequest
  }

  function reconcileOutgoingFiles() {
    var key
    for (key in root.outgoingFiles) {
      var transfer = root.outgoingFiles[key]
      if (!transfer || !transfer.pending || !transfer.request)
        continue
      root.sendOp({ op: "file.status", conversation: String(key), id: transfer.request })
    }
  }

  function trackInFlightMessage(op) {
    if (!op || op.op !== "msg.send" || !op.id)
      return
    var next = {}
    var key
    for (key in root.inFlightMessages)
      next[key] = root.inFlightMessages[key]
    next[String(op.id)] = { conversation: String(op.conversation || "") }
    root.inFlightMessages = next
  }

  function trackSerializedMessage(line) {
    var op
    try { op = JSON.parse(line) } catch (e) { return }
    root.trackInFlightMessage(op)
  }

  function finishInFlightMessage(request) {
    var requestKey = String(request || "")
    if (!requestKey || !root.inFlightMessages[requestKey])
      return
    var next = {}
    var key
    for (key in root.inFlightMessages)
      if (key !== requestKey)
        next[key] = root.inFlightMessages[key]
    root.inFlightMessages = next
  }

  function failInFlightMessages(reason) {
    var outstanding = root.inFlightMessages
    root.inFlightMessages = ({})
    var key
    for (key in outstanding) {
      root.lastMessageFailedConv = String(outstanding[key].conversation || "")
      root.lastMessageFailedRequest = String(key)
      root.lastMessageFailedCode = String(reason || "delivery_unknown")
      root.lastMessageFailedDelivered = false
      root.messageFailedTick = root.messageFailedTick + 1
    }
  }

  function flushOps() {
    if (root.awaitingHelperInstance || !root.pendingOps.length)
      return
    if (sock.connected) {
      var queued = root.pendingOps
      root.pendingOps = []
      for (var i = 0; i < queued.length; i++) {
        root.trackSerializedMessage(queued[i])
        sock.write(queued[i])
      }
    } else if (root.procReady) {
      var pending = root.pendingOps
      root.pendingOps = []
      for (var j = 0; j < pending.length; j++) {
        root.trackSerializedMessage(pending[j])
        proc.write(pending[j])
      }
    }
  }

  function failQueuedMessages(reason) {
    var code = String(reason || "helper_down")
    for (var i = 0; i < root.pendingOps.length; i++) {
      var queued
      try { queued = JSON.parse(root.pendingOps[i]) } catch (e) { continue }
      if (!queued || queued.op !== "msg.send" || !queued.id)
        continue
      root.lastMessageFailedConv = String(queued.conversation || "")
      root.lastMessageFailedRequest = String(queued.id)
      root.lastMessageFailedCode = code
      root.lastMessageFailedDelivered = false
      root.messageFailedTick = root.messageFailedTick + 1
    }
  }

  function markHelperIncompatible() {
    helperStatusTimer.stop()
    root.awaitingHelperInstance = false
    root.helperStatusNonce = ""
    root.failActiveOutgoingFiles("helper_incompatible")
    root.failQueuedMessages("helper_incompatible")
    root.pendingOps = []
    root.pendingHandshakeEvents = []
    root.pendingHandshakeBytes = 0
    root.handshakeEventOverflow = false
    root.helperCompatibility = "incompatible"
    root.connectionState = "reconnecting"
    root.selfOnline = false
    root.lastError = "helper_incompatible"
    root.lastErrorConv = ""
    root.lastErrorTick = root.lastErrorTick + 1
  }

  function requestHelperStatus() {
    root.helperStatusSequence = root.helperStatusSequence + 1
    root.helperStatusNonce = Date.now().toString(36) + "-status-" +
      root.helperStatusSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    var line = JSON.stringify({ op: "status", id: root.helperStatusNonce }) + "\n"
    root.awaitingHelperInstance = true
    root.legacySnapshotSeen = false
    helperStatusTimer.restart()
    if (sock.connected)
      sock.write(line)
    else if (root.procReady)
      proc.write(line)
  }

  function sendOp(obj) {
    if (root.helperCompatibility === "incompatible") {
      root.lastError = "helper_incompatible"
      root.lastErrorConv = String(obj && obj.conversation || "")
      root.lastErrorTick = root.lastErrorTick + 1
      return false
    }
    var line = JSON.stringify(obj) + "\n"
    if (!root.awaitingHelperInstance && sock.connected) {
      root.trackInFlightMessage(obj)
      sock.write(line)
      return true
    }
    if (!root.awaitingHelperInstance && root.procReady) {
      root.trackInFlightMessage(obj)
      proc.write(line)
      return true
    }
    var next = root.pendingOps.slice()
    next.push(line)
    root.pendingOps = next
    return true
  }

  function createInvite() { sendOp({ op: "invite.create", kind: "direct", ttlSec: 86400 }) }
  function revokeInvite() { sendOp({ op: "invite.revoke" }); root.inviteUrl = ""; root.qrPath = "" }
  function setAvatar(path) { sendOp({ op: "avatar.set", path: path }) }
  function setNickname(value) {
    var nickname = String(value || "").trim()
    if (!nickname || nickname.length > 128)
      return false
    sendOp({ op: "nickname.set", nickname: nickname })
    return true
  }
  function confirmAutoOpenMigration() {
    if (root.identityFingerprint)
      root.sendOp({ op: "settings.auto-open.migrated", id: root.identityFingerprint })
  }

  function toggleMute() {
    root.muted = !root.muted
  }

  function unreadFor(conv) {
    var key = String(conv || "")
    return Number(root.unreadByConversation[key] || 0)
  }

  function clearUnread(conv) {
    var key = String(conv || "")
    if (!key || !root.unreadByConversation[key] ||
        root.unreadClearPendingByConversation[key] ||
        Number(root.unreadClearRetryAfter[key] || 0) > Date.now())
      return
    var pendingClearNext = {}
    var pendingClearKey
    for (pendingClearKey in root.unreadClearPendingByConversation)
      pendingClearNext[pendingClearKey] = root.unreadClearPendingByConversation[pendingClearKey]
    pendingClearNext[key] = true
    root.unreadClearPendingByConversation = pendingClearNext
    if (!root.sendOp({ op: "unread.clear", conversation: key })) {
      var failedClearNext = {}
      for (pendingClearKey in root.unreadClearPendingByConversation) {
        if (pendingClearKey !== key)
          failedClearNext[pendingClearKey] = root.unreadClearPendingByConversation[pendingClearKey]
      }
      root.unreadClearPendingByConversation = failedClearNext
      return
    }
  }

  function nextHistoryRequestId() {
    root.historyRequestSequence = root.historyRequestSequence + 1
    return Date.now().toString(36) + "-history-" +
      root.historyRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }
  function requestHistory(conv) {
    var c = String(conv || root.lastConversation || "")
    var existingQueue = root.pendingHistoryUnread[c] || []
    if (existingQueue.length > 0) {
      if (Number(root.historyRetryTickByConversation[c] || 0) < root.reconnectGeneration)
        root.retryHistory(c)
      return
    }
    var next = {}
    var retryNext = {}
    var requestNext = {}
    var requestId = root.nextHistoryRequestId()
    var key
    var queue = existingQueue.slice()
    for (key in root.pendingHistoryUnread)
      next[key] = root.pendingHistoryUnread[key]
    for (key in root.historyRetryTickByConversation)
      retryNext[key] = root.historyRetryTickByConversation[key]
    for (key in root.historyRequestByConversation)
      requestNext[key] = root.historyRequestByConversation[key]
    queue.push(root.unreadFor(c))
    next[c] = queue
    retryNext[c] = root.reconnectGeneration
    requestNext[c] = requestId
    root.pendingHistoryUnread = next
    root.historyRetryTickByConversation = retryNext
    root.historyRequestByConversation = requestNext
    sendOp({ op: "history", conversation: c, limit: 50, id: requestId })
  }
  function retryHistory(conv) {
    var c = String(conv || root.lastConversation || "")
    if (!c || Number(root.historyRetryTickByConversation[c] || 0) === root.reconnectGeneration)
      return
    var historyQueue = root.pendingHistoryUnread[c] || []
    var unread = historyQueue.length > 0 ? Number(historyQueue[0] || 0) : root.unreadFor(c)
    var pendingNext = {}
    var retryNext = {}
    var requestNext = {}
    var requestId = root.nextHistoryRequestId()
    var key
    for (key in root.pendingHistoryUnread) {
      if (key !== c)
        pendingNext[key] = root.pendingHistoryUnread[key]
    }
    for (key in root.historyRetryTickByConversation)
      retryNext[key] = root.historyRetryTickByConversation[key]
    for (key in root.historyRequestByConversation)
      requestNext[key] = root.historyRequestByConversation[key]
    pendingNext[c] = [unread]
    retryNext[c] = root.reconnectGeneration
    requestNext[c] = requestId
    root.pendingHistoryUnread = pendingNext
    root.historyRetryTickByConversation = retryNext
    root.historyRequestByConversation = requestNext
    root.sendOp({ op: "history", conversation: c, limit: 50, id: requestId })
  }
  function editMessage(conv, id, text) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    var value = String(text || "")
    if (!c || !messageId || !value)
      return
    sendOp({ op: "message.edit", conversation: c, id: messageId, text: value })
  }
  function deleteMessage(conv, id) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId)
      return
    sendOp({ op: "message.delete", conversation: c, id: messageId })
  }
  function reactMessage(conv, id, emoji) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId || c.charAt(0) === "g")
      return
    sendOp({ op: "message.react", conversation: c, id: messageId, text: String(emoji || "") })
  }
  function clearHistory(conv) {
    var c = String(conv || root.lastConversation || "")
    if (!c)
      return
    sendOp({ op: "history.clear", conversation: c })
  }
  function sendReceipt(conv, id, state) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId || (state !== "delivered" && state !== "read"))
      return
    sendOp({ op: "receipt.send", conversation: c, id: messageId, state: state })
  }
  function setTyping(conv, typing) {
    var c = String(conv || root.lastConversation || "")
    if (!c)
      return
    sendOp({ op: "typing.set", conversation: c, typing: !!typing })
  }
  function isPeerTyping(conv) {
    var c = String(conv || "")
    var expiry = c !== "" ? Number(root.peerTypingByConv[c] || 0) : 0
    if (!expiry || expiry <= Date.now())
      return false
    return true
  }
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
  function removeContact() {
    if (!root.lastDirectId)
      return
    sendOp({ op: "contact.remove", id: root.lastDirectId })
    root.safetyCode = ""
    root.safetyConv = ""
  }
  function rotateNospam() {
    sendOp({ op: "nospam.rotate" })
    root.inviteUrl = ""
    root.qrPath = ""
  }
  function getSafety() {
    if (!root.lastDirectId)
      return
    sendOp({ op: "safety.get", conversation: root.lastDirectId })
  }
  function createGroup() { sendOp({ op: "group.create", title: "group" }) }
  function inviteToGroup() {
    if (!root.lastGroup || !root.lastDirectId)
      return
    sendOp({ op: "invite.create", kind: "group", group: root.lastGroup, role: "member", id: root.lastDirectId, ttlSec: 86400 })
  }
  function dissolveGroup() {
    if (!root.lastGroup)
      return
    sendOp({ op: "group.dissolve", group: root.lastGroup })
  }
  function leaveGroup() {
    if (!root.lastGroup)
      return
    sendOp({ op: "group.leave", group: root.lastGroup })
  }
  function setLastGroupMemberRole(role) {
    if (!root.lastGroup || !root.lastDirectId)
      return
    sendOp({ op: "group.member.setRole", group: root.lastGroup, member: root.lastDirectId, role: role })
  }
  function removeLastGroupMember() {
    if (!root.lastGroup || !root.lastDirectId)
      return
    sendOp({ op: "group.member.remove", group: root.lastGroup, member: root.lastDirectId })
  }
  function openCard() {
    sendOp({ op: "surface.set", conversation: root.lastConversation, monitor: "", x: 40, y: 80, pinned: true })
  }
  function setSurface(conv, mon, x, y, pinned) {
    sendOp({ op: "surface.set", conversation: conv, monitor: mon || "", x: x, y: y, pinned: !!pinned })
  }
  function exportIdentity() {
    sendOp({ op: "identity.export" })
  }
  function importIdentity(path, replace, passphrase) {
    var o = { op: "identity.import", path: path }
    if (replace)
      o.replace = true
    if (passphrase)
      o.passphrase = String(passphrase)
    sendOp(o)
  }
  function searchChat(q) {
    sendOp({ op: "search", conversation: root.lastConversation, text: q, limit: 20 })
  }
  function unlockIdentity(pass) {
    sendOp({ op: "identity.unlock", passphrase: pass })
  }
  function protectIdentity(pass) {
    sendOp({ op: "identity.protect", passphrase: pass })
  }
  function unprotectIdentity(pass) {
    sendOp({ op: "identity.unprotect", passphrase: pass })
  }
  function sendFile(path, conv) {
    var c = String(conv || root.lastConversation || "")
    var filePath = String(path || "")
    if (!c || !filePath || root.helperCompatibility === "incompatible" ||
        root.outgoingFile(c).pending)
      return false
    root.fileRequestSequence = root.fileRequestSequence + 1
    var requestId = Date.now().toString(36) + "-" + root.fileRequestSequence.toString(36) +
      "-" + Math.floor(Math.random() * 0x100000000).toString(36)
    root.lastFileConv = c
    root.lastFileState = "sending"
    root.lastFileDir = "out"
    root.lastFileError = ""
    root.lastFilePath = filePath
    root.setOutgoingFile(c, { id: "", path: filePath, request: requestId, pending: true, cancelRequested: false })
    root.lastFileTick = root.lastFileTick + 1
    sendOp({ op: "file.send", conversation: c, path: filePath, id: requestId })
    return true
  }
  function cancelOutgoingFile(conv) {
    var c = String(conv || root.lastFileConv || "")
    var transfer = root.outgoingFile(c)
    if (!transfer.pending || transfer.cancelRequested)
      return false
    root.setOutgoingFile(c, {
      id: transfer.id || "",
      path: transfer.path || "",
      request: transfer.request || "",
      pending: true,
      cancelRequested: true
    })
    root.lastFileConv = c
    root.lastFileState = "canceling"
    root.lastFileDir = "out"
    root.lastFilePath = ""
    root.lastFileTick = root.lastFileTick + 1
    if (transfer.id)
      root.sendOp({ op: "file.cancel", id: transfer.id })
    return true
  }
  function acceptFile(conv) {
    var c = String(conv || root.lastFileConv || "")
    var offer = fileOffer(c)
    if (!offer.id)
      return
    sendOp({ op: "file.accept", id: offer.id })
    setFileOffer(c, { id: offer.id, name: offer.name || "", path: offer.path || "", pending: false })
    if (c === root.lastFileConv)
      root.pendingFile = false
  }
  function cancelFile(conv) {
    var c = String(conv || root.lastFileConv || "")
    var offer = fileOffer(c)
    if (!offer.id)
      return
    sendOp({ op: "file.cancel", id: offer.id })
    setFileOffer(c, { id: offer.id, name: offer.name || "", path: "", pending: false })
    if (c === root.lastFileConv)
      root.pendingFile = false
  }
  function startCall(conv) {
    sendOp({ op: "call.start", conversation: conv || root.lastConversation })
  }
  function answerCall(conv) {
    var c = conv || root.lastCallConv || root.lastDirectId
    if (!c)
      return
    sendOp({ op: "call.answer", conversation: c })
    root.incomingCall = false
  }
  function stopCall(conv) {
    var c = conv || root.lastCallConv || root.lastDirectId
    if (!c)
      return
    sendOp({ op: "call.stop", conversation: c })
    root.incomingCall = false
  }

  function resetBackoff() {
    root.backoffMs = 200
  }

  function retryHelperHandshake() {
    if (!root.awaitingHelperInstance)
      return
    if (root.legacySnapshotSeen && (root.attached || !root.localHelperProtocolConfirmed)) {
      root.legacyHandshakeAttempts = root.legacyHandshakeAttempts + 1
      var legacyAttemptLimit = root.attached ? 3 : 12
      if (root.legacyHandshakeAttempts >= legacyAttemptLimit) {
        root.markHelperIncompatible()
        return
      }
    }
    root.connectionState = root.everOnline ? "reconnecting" : "starting"
    root.lastError = "helper_handshake_pending"
    root.lastErrorConv = ""
    root.lastErrorTick = root.lastErrorTick + 1
    root.requestHelperStatus()
  }

  function resetStateForIdentity() {
    root.failActiveOutgoingFiles("identity_replaced")
    root.failInFlightMessages("delivery_unknown")
    root.failQueuedMessages("identity_changed")
    root.pendingOps = []
    root.pendingHandshakeEvents = []
    root.pendingHandshakeBytes = 0
    root.handshakeEventOverflow = false
    root.fileOffers = ({})
    root.outgoingFiles = ({})
    root.pendingFile = false
    root.pendingHistoryUnread = ({})
    root.historyRetryTickByConversation = ({})
    root.historyRequestByConversation = ({})
    root.lastHistoryItems = []
    root.lastHistoryConv = ""
    root.lastHistoryUnreadConv = ""
    root.lastHistoryUnreadCount = 0
    root.searchItems = []
    root.lastChatText = ""
    root.lastChatId = ""
    root.lastChatReply = ""
    root.lastChatDir = ""
    root.lastChatKind = ""
    root.lastChatRequest = ""
    root.lastChatConv = ""
    root.lastMessageFailedConv = ""
    root.lastMessageFailedRequest = ""
    root.lastMessageFailedCode = ""
    root.lastMessageFailedDelivered = false
    root.lastUpdateConv = ""
    root.lastUpdateId = ""
    root.lastUpdateText = ""
    root.lastReactionConv = ""
    root.lastReactionId = ""
    root.lastReactionEmoji = ""
    root.lastReactionActor = ""
    root.lastReactionFailedConv = ""
    root.lastReactionFailedId = ""
    root.lastReactionFailedCode = ""
    root.lastUnreadFailedConv = ""
    root.lastUnreadFailedCode = ""
    root.lastReceiptConv = ""
    root.lastReceiptId = ""
    root.lastReceiptState = ""
    root.lastReceiptSentConv = ""
    root.lastReceiptSentId = ""
    root.lastReceiptSentState = ""
    root.lastReceiptFailedConv = ""
    root.lastReceiptFailedId = ""
    root.lastReceiptFailedState = ""
    root.lastReceiptFailedCode = ""
    root.peerTyping = false
    root.peerTypingByConv = ({})
    root.unreadByConversation = ({})
    root.unreadClearPendingByConversation = ({})
    root.unreadClearRetryAfter = ({})
    root.unreadWarning = ""
    root.unreadCount = 0
    root.selfAvatar = ""
    root.lastConversation = ""
    root.lastDirectId = ""
    root.lastAddr = ""
    root.lastGroup = ""
    root.pending = false
    root.pendingGroup = false
    root.inviteUrl = ""
    root.qrPath = ""
    root.safetyCode = ""
    root.safetyConv = ""
    root.incomingCall = false
    root.everOnline = false
    root.recoveringHelper = false
    root.selfOnline = false
    root.connectionState = "starting"
    root.lastCallState = ""
    root.lastCallConv = ""
    root.friends = []
    root.surfaces = []
    root.surfacesTick = root.surfacesTick + 1
    root.lastError = "identity_changed"
    root.lastErrorConv = ""
    root.lastErrorTick = root.lastErrorTick + 1
  }

  function failActiveOutgoingFiles(reason) {
    var next = {}
    var failed = []
    var failedRequests = {}
    var failedTransferIds = {}
    var key
    for (key in root.outgoingFiles) {
      var transfer = root.outgoingFiles[key]
      if (transfer && transfer.pending) {
        next[key] = { id: transfer.id || "", path: transfer.path || "", request: transfer.request || "", pending: false, cancelRequested: false }
        failed.push(key)
        if (transfer.request)
          failedRequests[String(transfer.request)] = true
        if (transfer.id)
          failedTransferIds[String(transfer.id)] = true
      } else {
        next[key] = transfer
      }
    }
    var keptOps = []
    for (var opIndex = 0; opIndex < root.pendingOps.length; opIndex++) {
      var keep = true
      try {
        var queued = JSON.parse(root.pendingOps[opIndex])
        if ((queued.op === "file.send" && failedRequests[String(queued.id || "")]) ||
            (queued.op === "file.cancel" && failedTransferIds[String(queued.id || "")]))
          keep = false
      } catch (e) {
      }
      if (keep)
        keptOps.push(root.pendingOps[opIndex])
    }
    root.pendingOps = keptOps
    root.outgoingFiles = next
    for (var i = 0; i < failed.length; i++) {
      root.lastFileConv = failed[i]
      root.lastFileState = "failed"
      root.lastFileDir = "out"
      root.lastFileError = reason || "helper_down"
      root.lastFilePath = ""
      root.lastFileTick = root.lastFileTick + 1
    }
  }

  function scheduleRestart() {
    helperStatusTimer.stop()
    root.failInFlightMessages("delivery_unknown")
    root.reconnectGeneration = root.reconnectGeneration + 1
    root.helperCompatibility = "unknown"
    root.legacyHandshakeAttempts = 0
    root.legacySnapshotSeen = false
    root.awaitingHelperInstance = false
    root.helperStatusNonce = ""
    root.recoveringHelper = true
    root.connectionState = "reconnecting"
    root.selfOnline = false
    if (root.lastError !== "identity_rollback_failed" &&
        root.lastError !== "identity_backup_cleanup_failed")
      root.lastError = "helper_down"
    root.lastHistoryUnreadConv = ""
    root.lastHistoryUnreadCount = 0
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

  FileView {
    id: helperProtocolFile
    path: root.stateDir + "/omaq.protocol"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: {
      var parsed = null
      try { parsed = JSON.parse(text()) } catch (e) { parsed = null }
      if (!parsed || !Number.isInteger(Number(parsed.pid)) ||
          !Number.isInteger(Number(parsed.version)) ||
          !/^[0-9a-f]{32}$/.test(String(parsed.instance || "")) ||
          String(parsed.nonce || "") !== root.helperLaunchNonce) {
        root.helperProtocolPid = 0
        root.helperProtocolVersion = 0
        root.helperProtocolInstance = ""
        root.helperProtocolNonce = ""
        return
      }
      root.helperProtocolPid = Number(parsed.pid)
      root.helperProtocolVersion = Number(parsed.version)
      root.helperProtocolInstance = String(parsed.instance)
      root.helperProtocolNonce = String(parsed.nonce)
    }
    onLoadFailed: {
      root.helperProtocolPid = 0
      root.helperProtocolVersion = 0
      root.helperProtocolInstance = ""
      root.helperProtocolNonce = ""
    }
  }

  Timer {
    id: helperStatusTimer
    interval: 5000
    repeat: false
    onTriggered: root.retryHelperHandshake()
  }

  Timer {
    id: typingSweep
    interval: 500
    repeat: true
    running: true
    onTriggered: {
      var now = Date.now()
      var next = {}
      var changed = false
      var key
      for (key in root.peerTypingByConv) {
        if (Number(root.peerTypingByConv[key]) > now)
          next[key] = root.peerTypingByConv[key]
        else
          changed = true
      }
      if (changed) {
        root.peerTypingByConv = next
        root.typingTick = root.typingTick + 1
      }
      root.peerTyping = root.isPeerTyping(root.lastTypingConv)
    }
  }

  Timer {
    id: restartTimer
    repeat: false
    onTriggered: {
      root.attached = false
      root.procReady = false
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
      if (connected) {
        root.resetBackoff()
        root.requestHelperStatus()
      } else if (root.attached)
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
      "OMAQ_STATE": root.stateDir,
      "OMAQ_PROTOCOL_NONCE": root.helperLaunchNonce
    })
    running: true
    stdinEnabled: true
    stdout: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onStarted: {
      root.procReady = true
      root.connectionState = root.helperInstance === "" ? "starting" : "reconnecting"
      root.resetBackoff()
      root.requestHelperStatus()
    }
    onExited: function(code) {
      root.procReady = false
      if (code === 2) {
        root.attachSocket()
        return
      }
      root.scheduleRestart()
    }
  }
}
