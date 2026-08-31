import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  signal chatSearchResult(string conversation, string key, string request, var items)
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
  property string persistentWarning: ""
  property string lastErrorConv: ""
  property string lastErrorRequest: ""
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
  property bool friendsReady: false
  property var pendingCallSnapshot: null
  property bool pendingCallSnapshotSet: false
  property var pendingDirectEvents: []
  property int pendingDirectEventBytes: 0
  property bool pendingDirectEventOverflow: false
  readonly property int handshakeEventByteLimit: 4 * 1024 * 1024
  readonly property int directEventByteLimit: 4 * 1024 * 1024
  property int helperProtocolPid: 0
  property int helperProtocolVersion: 0
  property int activeHelperProtocol: 0
  property string helperProtocolInstance: ""
  readonly property string helperLaunchNonce: Date.now().toString(36) + "-" +
    Math.floor(Math.random() * 0x100000000).toString(36)
  property string helperProtocolNonce: ""
  readonly property int requiredHelperProtocol: 7
  readonly property bool supportsIdentityActions: root.activeHelperProtocol >= 8
  readonly property bool supportsAttachments: root.activeHelperProtocol >= 9
  readonly property bool supportsDirectRecovery: root.activeHelperProtocol >= 10
  readonly property bool supportsRedeemResults: root.activeHelperProtocol >= 10
  readonly property bool supportsStableDirectState: root.activeHelperProtocol >= 11
  readonly property bool supportsGroupAttachments: root.activeHelperProtocol >= 12
  readonly property bool supportsCorrelatedGroupProjection: root.activeHelperProtocol >= 13
  readonly property bool supportsGroupTyping: root.activeHelperProtocol >= 13
  readonly property bool supportsCustomSounds: root.activeHelperProtocol >= 14
  onActiveHelperProtocolChanged: {
    if (root.supportsCustomSounds)
      return
    if (root.customSounds.length > 0) {
      root.customSounds = []
      root.soundTick = root.soundTick + 1
    }
    root.failPendingSoundRequests("helper_incompatible")
  }
  onHelperCompatibilityChanged: {
    if (root.helperCompatibility === "compatible" || root.customSounds.length === 0)
      return
    root.customSounds = []
    root.soundTick = root.soundTick + 1
  }
  readonly property bool localHelperProtocolConfirmed: !root.attached && proc.processId > 0 &&
    root.helperProtocolPid === proc.processId &&
    root.helperProtocolVersion >= root.requiredHelperProtocol &&
    root.helperProtocolNonce === root.helperLaunchNonce &&
    /^[0-9a-f]{32}$/.test(root.helperProtocolInstance)
  property string identityFingerprint: ""
  property bool directReinviteRequired: false
  property bool identityRecoveryDegraded: false
  property bool identityPrimaryUncertain: false
  property string lastIdentityPrimaryRequest: ""
  property int identityPrimaryTick: 0
  property string lastDirectReinviteRequest: ""
  property int directReinviteTick: 0
  property int redeemRequestSequence: 0
  property string lastRedeemRequest: ""
  property string lastRedeemKind: ""
  property int redeemTick: 0
  property string inviteUrl: ""
  property double inviteExpiresAt: 0
  property string lastInviteAction: ""
  property string lastInviteRequest: ""
  property int inviteActionTick: 0
  property string qrPath: ""
  property string safetyCode: ""
  property string safetyConv: ""
  property bool pending: false
  property string lastConversation: "0"
  property string lastDirectId: ""
  property string lastDirectKey: ""
  property string selectedConversation: ""
  property string selectedDirectId: ""
  property string selectedDirectKey: ""
  property string safetyRequest: ""
  property int safetyRequestSequence: 0
  property string lastAddr: ""
  property string lastGroup: ""
  property bool pendingGroup: false
  property var groups: []
  property var pendingGroupBuild: ({})
  property var pendingGroupOrder: []
  property string pendingGroupGeneration: ""
  property string pendingGroupRequest: ""
  property string expectedGroupRequest: ""
  property int pendingGroupExpectedGroups: -1
  property int pendingGroupExpectedMembers: -1
  property int pendingGroupReceivedMembers: 0
  property bool pendingGroupInvalid: false
  property int lastGroupGeneration: 0
  property int groupRefreshSequence: 0
  property int groupRefreshAttempts: 0
  property bool groupProjectionFailed: false
  property int groupsTick: 0
  property string lastGroupInviteSentGroup: ""
  property string lastGroupInviteSentFriend: ""
  property string lastGroupInviteSentRequest: ""
  property int groupInviteSentTick: 0
  property string lastGroupInviteFailedGroup: ""
  property string lastGroupInviteFailedFriend: ""
  property string lastGroupInviteFailedRequest: ""
  property string lastGroupInviteFailedCode: ""
  property int groupInviteFailedTick: 0
  property int groupInviteRequestSequence: 0
  property bool groupsReady: false
  property string lastRemovedGroup: ""
  property int removedGroupTick: 0
  property string lastChatText: ""
  property string lastChatId: ""
  property string lastChatReply: ""
  property string lastChatDir: ""
  property string lastChatKind: ""
  property string lastChatSender: ""
  property double lastChatTimestamp: 0
  property string lastChatRequest: ""
  property string lastChatKey: ""
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
  property string lastHistoryFailedConv: ""
  property string lastHistoryFailedCode: ""
  property int historyFailedTick: 0
  property var pendingHistoryUnread: ({})
  property var historyRetryTickByConversation: ({})
  property var historyRequestByConversation: ({})
  property var historyKeyByConversation: ({})
  property int historyRequestSequence: 0
  property int reconnectGeneration: 0
  property int helperInstanceGeneration: 0
  property bool peerTyping: false
  property string lastTypingConv: ""
  property string lastReceiptConv: ""
  property string lastReceiptId: ""
  property string lastReceiptState: ""
  property string lastReceiptActor: ""
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
  property string lastConversationReadConv: ""
  property int conversationReadTick: 0
  property string lastConversationReadFailedConv: ""
  property string lastConversationReadFailedCode: ""
  property int conversationReadFailedTick: 0
  property var peerTypingByConv: ({})
  property var groupTypingByConv: ({})
  property int typingTick: 0
  property var lastSurface: ({})
  property var surfaces: []
  property int surfacesTick: 0
  property var lastAutoOpenItems: []
  property bool lastAutoOpenDirectDefault: true
  property string lastAutoOpenRequest: ""
  property string lastAutoOpenCode: ""
  property bool lastAutoOpenSucceeded: false
  property int autoOpenTick: 0
  property var customSounds: []
  property var pendingSoundRequests: ({})
  property int soundRequestSequence: 0
  property string lastSoundRequest: ""
  property string lastSoundOperation: ""
  property string lastSoundSelected: ""
  property string lastSoundCode: ""
  property bool lastSoundSucceeded: false
  property int soundTick: 0
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
  property var fileNotices: ({})
  property int fileNoticeTick: 0
  property int fileRequestSequence: 0
  property string lastAttachmentInspectionRequest: ""
  property string lastAttachmentInspectionPath: ""
  property bool lastAttachmentInspectionAccepted: false
  property int attachmentInspectionTick: 0
  property string lastAttachmentStageRequest: ""
  property string lastAttachmentStagePath: ""
  property int attachmentStageTick: 0
  property var attachmentCleanupDebts: []
  property bool incomingCall: false
  property string lastCallState: ""
  property string lastCallConv: ""
  property string lastCallKey: ""
  property bool callToneSuppressed: false
  property int callDurationSeconds: 0
  property bool locked: false
  property bool saveProtected: false
  property var friends: []
  property int friendsTick: 0
  property string pendingFriendGeneration: ""
  property var pendingFriendBuild: []
  property string selfAvatar: ""
  property int avatarTick: 0
  property string selfNickname: ""
  property string lastNicknameRequest: ""
  property int nicknameTick: 0
  property int identityTick: 0
  property int identityActionTick: 0
  property string lastIdentityOp: ""
  property string lastIdentityPath: ""
  property string lastIdentityRequest: ""
  property bool lastIdentityProtected: false
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

  function applyCallSnapshot(snapshotCall) {
    var conv = ""
    var key = ""
    var state = ""
    if (snapshotCall && typeof snapshotCall === "object") {
      conv = String(snapshotCall.conversation || "")
      key = String(snapshotCall.key || "")
      state = String(snapshotCall.state || "")
      if (!/^(0|[1-9][0-9]*)$/.test(conv) ||
          ["incoming", "ringing", "active"].indexOf(state) === -1 ||
          (root.supportsStableDirectState &&
           (!/^[0-9a-f]{64}$/.test(key) ||
            (root.friendsReady && !root.directBindingMatches(conv, key))))) {
        conv = ""
        key = ""
        state = ""
      }
    }
    if (state === "active" &&
        (root.lastCallState !== "active" || String(root.lastCallConv || "") !== conv))
      root.callDurationSeconds = 0
    if (!state)
      root.callDurationSeconds = 0
    root.incomingCall = state === "incoming"
    root.callToneSuppressed = state !== "incoming" && state !== "ringing"
    root.lastCallState = state
    root.lastCallConv = conv
    root.lastCallKey = conv ? (key || root.friendKeyForConversation(conv)) : ""
    if (conv) {
      root.lastConversation = conv
      root.lastDirectId = conv
      root.lastDirectKey = key || root.friendKeyForConversation(conv)
    }
  }

  function applyPendingCallSnapshot() {
    if (!root.pendingCallSnapshotSet)
      return
    var snapshot = root.pendingCallSnapshot
    root.pendingCallSnapshot = null
    root.pendingCallSnapshotSet = false
    root.applyCallSnapshot(snapshot)
  }

  function friendKeyForConversation(conversation) {
    var id = String(conversation || "")
    if (!/^(0|[1-9][0-9]*)$/.test(id))
      return ""
    for (var i = 0; i < root.friends.length; i++)
      if (String(root.friends[i].id || "") === id) {
        var key = String(root.friends[i].key || "")
        return /^[0-9a-f]{64}$/.test(key) ? key : ""
      }
    return ""
  }

  function directBindingMatches(conversation, expectedKey) {
    var id = String(conversation || "")
    var key = String(expectedKey || "")
    return /^(0|[1-9][0-9]*)$/.test(id) && /^[0-9a-f]{64}$/.test(key) &&
      root.friendKeyForConversation(id) === key
  }

  function directEventBindingValid(event) {
    var ev = event || ({})
    var conversation = String(ev.conversation || "")
    if (!/^(0|[1-9][0-9]*)$/.test(conversation) ||
        !root.supportsStableDirectState)
      return true
    return root.directBindingMatches(conversation, String(ev.key || ""))
  }

  function applySurfaceEvent(event) {
    var ev = event || ({})
    var conversation = String(ev.conversation || "")
    var direct = /^(0|[1-9][0-9]*)$/.test(conversation)
    var group = /^g:[0-9a-f]{64}$/.test(conversation)
    if ((!direct && !group) || (direct && !root.directEventBindingValid(ev)))
      return false
    var key = direct ? String(ev.key || "") : ""
    var monitor = String(ev.monitor || "")
    var x = Number(ev.x)
    var y = Number(ev.y)
    if ((direct && !/^[0-9a-f]{64}$/.test(key)) || monitor.length > 63 ||
        /[\u0000-\u001f\u007f]/.test(monitor) || typeof ev.pinned !== "boolean" ||
        !Number.isInteger(x) || !Number.isInteger(y) ||
        x < -2147483648 || x > 2147483647 ||
        y < -2147483648 || y > 2147483647)
      return false
    var current = root.surfaces || []
    var previous = null
    var i
    for (i = 0; i < current.length; i++) {
      var candidate = current[i] || ({})
      if (String(candidate.conversation || "") === conversation &&
          (!direct || String(candidate.key || "") === key)) {
        previous = candidate
        break
      }
    }
    var width = ev.width === undefined
      ? Number(previous && previous.width !== undefined ? previous.width : 420)
      : Number(ev.width)
    var height = ev.height === undefined
      ? Number(previous && previous.height !== undefined ? previous.height : 420)
      : Number(ev.height)
    if (!Number.isInteger(width) || !Number.isInteger(height) ||
        width < 200 || width > 4096 || height < 160 || height > 4096)
      return false
    var canonical = { conversation: conversation, key: key, monitor: monitor,
      x: x, y: y, width: width, height: height, pinned: ev.pinned }
    var next = []
    var inserted = false
    for (i = 0; i < current.length; i++) {
      var item = current[i] || ({})
      var same = String(item.conversation || "") === conversation ||
        (direct && String(item.key || "") === key)
      if (same) {
        if (!inserted) {
          next.push(canonical)
          inserted = true
        }
      } else {
        next.push(item)
      }
    }
    if (!inserted)
      next.push(canonical)
    root.surfaces = next
    return true
  }

  function clearPendingGroupProjection() {
    root.pendingGroupGeneration = ""
    root.pendingGroupRequest = ""
    root.pendingGroupBuild = ({})
    root.pendingGroupOrder = []
    root.pendingGroupExpectedGroups = -1
    root.pendingGroupExpectedMembers = -1
    root.pendingGroupReceivedMembers = 0
    root.pendingGroupInvalid = false
  }

  function beginGroupProjection(event) {
    var ev = event || ({})
    var generation = String(ev.generation || "")
    var request = String(ev.request || "")
    if (!/^(0|[1-9][0-9]*)$/.test(generation))
      return false
    if (root.supportsCorrelatedGroupProjection) {
      var generationNumber = Number(generation)
      var expectedGroups = Number(ev.groups)
      var expectedMembers = Number(ev.members)
      if (String(ev.instance || "") !== root.helperInstance ||
          !Number.isInteger(generationNumber) || generationNumber <= 0 ||
          generationNumber > 0xffffffff ||
          !Number.isInteger(expectedGroups) || expectedGroups < 0 || expectedGroups > 10 ||
          !Number.isInteger(expectedMembers) || expectedMembers < 0 || expectedMembers > 100 ||
          (root.expectedGroupRequest !== "" && request !== root.expectedGroupRequest) ||
          (root.expectedGroupRequest === "" && request !== "") ||
          (request === "" && generationNumber <= root.lastGroupGeneration))
        return false
      root.pendingGroupExpectedGroups = expectedGroups
      root.pendingGroupExpectedMembers = expectedMembers
    } else {
      root.pendingGroupExpectedGroups = -1
      root.pendingGroupExpectedMembers = -1
    }
    root.groupsReady = false
    root.pendingGroupGeneration = generation
    root.pendingGroupRequest = request
    root.pendingGroupBuild = ({})
    root.pendingGroupOrder = []
    root.pendingGroupReceivedMembers = 0
    root.pendingGroupInvalid = false
    groupProjectionTimer.interval = 5000
    groupProjectionTimer.restart()
    return true
  }

  function groupProjectionEventMatches(event) {
    var ev = event || ({})
    if (String(ev.generation || "") !== root.pendingGroupGeneration ||
        root.pendingGroupGeneration === "")
      return false
    if (!root.supportsCorrelatedGroupProjection)
      return true
    return String(ev.instance || "") === root.helperInstance &&
      String(ev.request || "") === root.pendingGroupRequest
  }

  function groupProjectionComplete(event) {
    var ev = event || ({})
    if (!root.groupProjectionEventMatches(ev))
      return false
    if (root.supportsCorrelatedGroupProjection) {
      if (Number(ev.groups) !== root.pendingGroupExpectedGroups ||
          Number(ev.members) !== root.pendingGroupExpectedMembers ||
          root.pendingGroupOrder.length !== root.pendingGroupExpectedGroups ||
          root.pendingGroupReceivedMembers !== root.pendingGroupExpectedMembers)
        return false
      for (var index = 0; index < root.pendingGroupOrder.length; index++) {
        var staged = root.pendingGroupBuild[root.pendingGroupOrder[index]]
        if (!staged || Number(staged.memberCount) !== (staged.members || []).length)
          return false
      }
    }
    return !root.pendingGroupInvalid
  }

  function rejectGroupProjection() {
    root.clearPendingGroupProjection()
    root.groupsReady = false
    groupProjectionTimer.interval = 250
    groupProjectionTimer.restart()
  }

  function refreshGroups(resetAttempts) {
    if (resetAttempts) {
      root.groupRefreshAttempts = 0
      root.groupProjectionFailed = false
    }
    if (root.helperCompatibility === "incompatible")
      return false
    if (!root.supportsCorrelatedGroupProjection) {
      if (!root.awaitingHelperInstance)
        root.requestHelperStatus()
      return true
    }
    if (root.groupRefreshAttempts >= 5) {
      root.groupProjectionFailed = true
      return false
    }
    root.groupRefreshSequence = root.groupRefreshSequence + 1
    root.groupRefreshAttempts = root.groupRefreshAttempts + 1
    root.expectedGroupRequest = Date.now().toString(36) + "-groups-" +
      root.groupRefreshSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    root.clearPendingGroupProjection()
    root.groupsReady = false
    groupProjectionTimer.interval = Math.min(8000,
      1000 * Math.pow(2, root.groupRefreshAttempts - 1))
    groupProjectionTimer.restart()
    return root.sendOp({ op: "group.list", id: root.expectedGroupRequest })
  }

  function withoutConversation(source, conversation) {
    var next = {}
    for (var key in (source || {}))
      if (String(key) !== String(conversation))
        next[key] = source[key]
    return next
  }

  function purgeReboundConversation(conversation) {
    var id = String(conversation || "")
    if (!/^(0|[1-9][0-9]*)$/.test(id))
      return
    var pending = []
    for (var i = 0; i < root.pendingOps.length; i++) {
      var operation
      try { operation = JSON.parse(root.pendingOps[i]) } catch (e) {
        continue
      }
      if (String(operation.conversation || "") === id &&
          root.directConversationOperation(operation.op)) {
        root.rejectBoundOperation(operation, "identity_changed")
        continue
      }
      pending.push(root.pendingOps[i])
    }
    root.pendingOps = pending
    var outgoing = root.outgoingFile(id)
    if (outgoing.pending) {
      root.lastFileConv = id
      root.lastFileState = "failed"
      root.lastFileDir = "out"
      root.lastFileError = "identity_changed"
      root.lastFileTick = root.lastFileTick + 1
    }
    root.fileOffers = root.withoutConversation(root.fileOffers, id)
    root.outgoingFiles = root.withoutConversation(root.outgoingFiles, id)
    root.fileNotices = root.withoutConversation(root.fileNotices, id)
    root.peerTypingByConv = root.withoutConversation(root.peerTypingByConv, id)
    root.unreadByConversation = root.withoutConversation(root.unreadByConversation, id)
    root.unreadClearPendingByConversation = root.withoutConversation(
      root.unreadClearPendingByConversation, id)
    root.unreadClearRetryAfter = root.withoutConversation(root.unreadClearRetryAfter, id)
    root.pendingHistoryUnread = root.withoutConversation(root.pendingHistoryUnread, id)
    root.historyRetryTickByConversation = root.withoutConversation(
      root.historyRetryTickByConversation, id)
    root.historyRequestByConversation = root.withoutConversation(
      root.historyRequestByConversation, id)
    root.historyKeyByConversation = root.withoutConversation(
      root.historyKeyByConversation, id)
    if (String(root.lastChatConv || "") === id) {
      root.lastChatText = ""
      root.lastChatId = ""
      root.lastChatReply = ""
      root.lastChatDir = ""
      root.lastChatKind = ""
      root.lastChatSender = ""
      root.lastChatTimestamp = 0
      root.lastChatRequest = ""
      root.lastChatKey = ""
      root.lastChatConv = ""
      root.messageTick = root.messageTick + 1
    }
    root.unreadCount = root.localUnreadTotal()
  }

  function applyFriendSnapshot(items) {
    var previous = root.friends || []
    root.friends = items || []
    for (var previousIndex = 0; previousIndex < previous.length; previousIndex++) {
      var previousId = String(previous[previousIndex].id || "")
      var previousKey = String(previous[previousIndex].key || "")
      if (/^[0-9a-f]{64}$/.test(previousKey) &&
          !root.directBindingMatches(previousId, previousKey))
        root.purgeReboundConversation(previousId)
    }
    var stillFriend = root.lastDirectId === ""
    var selectedStillValid = root.selectedDirectId === ""
    for (var i = 0; i < root.friends.length; i++) {
      var friendId = String(root.friends[i].id || "")
      var friendKey = String(root.friends[i].key || "")
      if (friendId === String(root.lastDirectId) &&
          (root.lastDirectKey === "" || friendKey === root.lastDirectKey)) {
        stillFriend = true
        root.lastDirectKey = friendKey
      }
      if (friendId === String(root.lastCallConv || "") &&
          (root.lastCallKey === "" || friendKey === root.lastCallKey))
        root.lastCallKey = friendKey
      if (friendId === root.selectedDirectId &&
          friendKey === root.selectedDirectKey)
        selectedStillValid = true
    }
    if (!stillFriend) {
      var removedDirectId = String(root.lastDirectId)
      root.lastDirectId = ""
      root.lastDirectKey = ""
      if (String(root.lastConversation || "") === removedDirectId)
        root.lastConversation = ""
      if (String(root.lastCallConv || "") === removedDirectId) {
        root.incomingCall = false
        root.lastCallState = ""
        root.lastCallConv = ""
        root.lastCallKey = ""
        root.callToneSuppressed = true
        root.callDurationSeconds = 0
      }
    }
    if (!selectedStillValid) {
      var removedSelectedId = root.selectedDirectId
      root.selectedDirectId = ""
      root.selectedDirectKey = ""
      root.safetyRequest = ""
      if (root.selectedConversation === removedSelectedId)
        root.selectedConversation = ""
      root.safetyCode = ""
      root.safetyConv = ""
    }
    root.friendsTick = root.friendsTick + 1
  }

  function identityErrorCode(code) {
    return ["locked", "identity_changed", "identity_exists",
      "identity_passphrase_required", "identity_import_failed", "busy",
      "forbidden", "identity_backup_failed", "identity_state_archive_failed",
      "identity_rollback_failed", "identity_backup_cleanup_failed",
      "identity_missing", "identity_mismatch", "identity_guard_invalid",
      "identity_recovery_degraded", "identity_primary_uncertain", "invite_self",
      "contact_exists", "contact_limit", "invite_rejected",
      "safety_key_changed", "group_registry_failed", "unsupported"].indexOf(
        String(code || "")) >= 0
  }

  function persistentError(code) {
    return ["identity_rollback_failed", "identity_backup_cleanup_failed",
      "identity_missing", "identity_mismatch", "identity_guard_invalid",
      "identity_recovery_degraded", "identity_primary_uncertain",
      "direct_state_migration_failed", "direct_state_reinvite_required"].indexOf(
        String(code || "")) >= 0
  }

  function clearRequestError(request) {
    var requestId = String(request || "")
    if (requestId !== "" && root.lastErrorRequest === requestId) {
      root.lastError = root.persistentWarning
      root.lastErrorRequest = ""
      root.lastErrorTick = root.lastErrorTick + 1
    }
  }

  function clearIdentityError(request) {
    var requestId = String(request || "")
    if (requestId !== "" && root.lastErrorRequest === requestId &&
        root.identityErrorCode(root.lastError)) {
      root.lastError = root.persistentWarning
      root.lastErrorRequest = ""
      root.lastErrorTick = root.lastErrorTick + 1
    }
  }

  function eventNeedsFriendProjection(event) {
    var ev = event || ({})
    if (!root.supportsStableDirectState || root.friendsReady ||
        !/^(0|[1-9][0-9]*)$/.test(String(ev.conversation || "")))
      return false
    return ["unread", "message", "message.updated", "message.reaction", "history",
      "search", "receipt", "receipt.sent", "typing", "file.offer", "file.sending",
      "file.done", "file.canceled", "file.failed", "call.incoming",
      "call.state"].indexOf(String(ev.event || "")) >= 0
  }

  function bufferDirectEvent(line) {
    if (root.pendingDirectEventOverflow)
      return
    var buffered = String(line || "")
    var bytes = buffered.length * 3 + 1
    if (bytes > root.directEventByteLimit - root.pendingDirectEventBytes) {
      root.pendingDirectEvents = []
      root.pendingDirectEventBytes = 0
      root.pendingDirectEventOverflow = true
      return
    }
    var next = root.pendingDirectEvents.slice()
    next.push(buffered)
    root.pendingDirectEvents = next
    root.pendingDirectEventBytes = root.pendingDirectEventBytes + bytes
  }

  function releaseDirectEvents() {
    var events = root.pendingDirectEvents
    var overflow = root.pendingDirectEventOverflow
    root.pendingDirectEvents = []
    root.pendingDirectEventBytes = 0
    root.pendingDirectEventOverflow = false
    if (overflow) {
      root.lastError = "helper_event_overflow"
      root.lastErrorConv = ""
      root.lastErrorTick = root.lastErrorTick + 1
      return
    }
    for (var i = 0; i < events.length; i++)
      root.handleLine(events[i])
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
      if (ev.event === "invite")
        return
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
        root.friendsReady = false
        if (processChanged || identityChanged) {
          root.pendingDirectEvents = []
          root.pendingDirectEventBytes = 0
          root.pendingDirectEventOverflow = false
        }
        if (processChanged) {
          root.customSounds = []
          root.soundTick = root.soundTick + 1
          root.lastGroupGeneration = 0
          root.clearPendingGroupProjection()
          if (root.lastError === root.persistentWarning)
            root.lastError = ""
          root.persistentWarning = ""
          root.failActiveIncomingFiles()
          root.failActiveOutgoingFiles("helper_restarted")
          root.failQueuedGroupInvites("helper_restarted")
          root.failPendingSoundRequests("helper_restarted")
          root.helperInstanceGeneration = root.helperInstanceGeneration + 1
        }
        if (identityChanged)
          root.resetStateForIdentity()
        root.helperInstance = nextInstance
        root.activeHelperProtocol = snapshotProtocol
        if (!root.supportsCustomSounds) {
          if (root.customSounds.length > 0) {
            root.customSounds = []
            root.soundTick = root.soundTick + 1
          }
          root.failPendingSoundRequests("helper_incompatible")
        }
        if (identityChanged && root.supportsCorrelatedGroupProjection)
          root.expectedGroupRequest = String(ev.request || "")
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
          root.lastError = root.persistentWarning
        root.awaitingHelperInstance = false
        root.helperStatusNonce = ""
        helperStatusTimer.stop()
        root.sendOp({ op: "identity.ready", id: nextInstance })
        root.flushOps()
        root.retryPendingSoundRequests()
        root.refreshCustomSounds()
        root.retryAttachmentCleanupDebts()
        replayOverflowToReport = !identityChanged && root.handshakeEventOverflow
        replayEventsToApply = identityChanged || replayOverflowToReport
          ? [] : root.pendingHandshakeEvents
        root.pendingHandshakeEvents = []
        root.pendingHandshakeBytes = 0
        root.handshakeEventOverflow = false
        root.pendingCallSnapshot = ev.call || null
        root.pendingCallSnapshotSet = true
        if (ev.locked === true)
          root.applyPendingCallSnapshot()
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
               !root.persistentError(root.lastError))
        root.lastError = root.persistentWarning
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
    if (root.eventNeedsFriendProjection(ev)) {
      root.bufferDirectEvent(line)
      return
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
    if (ev.event === "identity.primary") {
      root.identityPrimaryUncertain = !!ev.uncertain
      root.lastIdentityPrimaryRequest = String(ev.request || "")
      if (root.identityPrimaryUncertain) {
        root.persistentWarning = "identity_primary_uncertain"
        root.lastError = root.persistentWarning
      } else if (root.persistentWarning === "identity_primary_uncertain") {
        root.persistentWarning = root.identityRecoveryDegraded
          ? "identity_recovery_degraded"
          : (root.directReinviteRequired ? "direct_state_reinvite_required" : "")
        if (root.lastError === "identity_primary_uncertain")
          root.lastError = root.persistentWarning
      }
      root.identityPrimaryTick = root.identityPrimaryTick + 1
    }
    if (ev.event === "identity.recovery") {
      root.identityRecoveryDegraded = !!ev.degraded
      if (root.identityRecoveryDegraded) {
        if (!root.identityPrimaryUncertain)
          root.persistentWarning = "identity_recovery_degraded"
        root.lastError = root.persistentWarning
      } else if (root.persistentWarning === "identity_recovery_degraded") {
        root.persistentWarning = root.directReinviteRequired
          ? "direct_state_reinvite_required" : ""
        if (root.lastError === "identity_recovery_degraded")
          root.lastError = root.persistentWarning
      }
    }
    if (ev.event === "invite.redeemed") {
      root.lastRedeemRequest = String(ev.request || "")
      root.lastRedeemKind = String(ev.kind || "")
      root.redeemTick = root.redeemTick + 1
    }
    if (ev.event === "direct.reinvite") {
      root.directReinviteRequired = !!ev.required
      root.lastDirectReinviteRequest = String(ev.request || "")
      if (root.directReinviteRequired) {
        if (!root.identityRecoveryDegraded && !root.identityPrimaryUncertain)
          root.persistentWarning = "direct_state_reinvite_required"
        if (root.lastError === "" ||
            root.lastError === "direct_state_reinvite_required")
          root.lastError = root.persistentWarning
      } else if (root.persistentWarning === "direct_state_reinvite_required") {
        root.persistentWarning = ""
        if (root.lastError === "direct_state_reinvite_required")
          root.lastError = ""
      }
      root.directReinviteTick = root.directReinviteTick + 1
    }
    if (ev.event === "nickname") {
      root.lastNicknameRequest = String(ev.request || "")
      root.selfNickname = String(ev.value || "")
      if (root.lastError !== "helper_down" && root.lastError !== "helper_incompatible" &&
          !root.persistentError(root.lastError))
        root.lastError = root.persistentWarning
      root.nicknameTick = root.nicknameTick + 1
    }
    if (ev.event === "error") {
      root.lastError = ev.code || "error"
      root.lastErrorConv = ev.conversation || ""
      root.lastErrorRequest = String(ev.request || "")
      root.lastErrorTick = root.lastErrorTick + 1
      if (ev.code === "direct_state_migration_failed" ||
          (ev.code === "direct_state_reinvite_required" &&
           !root.identityRecoveryDegraded) ||
          ev.code === "identity_missing" || ev.code === "identity_mismatch" ||
          ev.code === "identity_guard_invalid" ||
          ev.code === "identity_recovery_degraded" ||
          ev.code === "identity_primary_uncertain")
        root.persistentWarning = String(ev.code)
      if (ev.code === "direct_state_reinvite_required")
        root.directReinviteRequired = true
      if (ev.code === "identity_rollback_failed")
        root.failQueuedGroupInvites("identity_rollback_failed")
      if (ev.code === "locked") {
        root.locked = true
        root.connectionState = "locked"
      }
    }
    if (ev.event === "friend.list.begin") {
      root.friendsReady = false
      root.pendingFriendGeneration = String(ev.generation || "")
      root.pendingFriendBuild = []
    }
    if (ev.event === "friend.info" &&
        String(ev.generation || "") === root.pendingFriendGeneration) {
      var friendBuild = root.pendingFriendBuild.slice()
      friendBuild.push({
        id: String(ev.id || ""),
        key: String(ev.key || ""),
        name: String(ev.name || ("Friend " + String(ev.id || ""))),
        avatar: String(ev.avatar || ""),
        online: !!ev.online,
        status: String(ev.status || (ev.online ? "online" : "offline"))
      })
      root.pendingFriendBuild = friendBuild
    }
    if (ev.event === "friend.list.end" &&
        String(ev.generation || "") === root.pendingFriendGeneration) {
      if (!root.locked && root.lastError !== "helper_down" &&
          root.lastError !== "helper_incompatible" &&
          !root.persistentError(root.lastError))
        root.lastError = root.persistentWarning
      root.applyFriendSnapshot(root.pendingFriendBuild)
      root.pendingFriendGeneration = ""
      root.pendingFriendBuild = []
      root.friendsReady = true
      root.applyPendingCallSnapshot()
      root.releaseDirectEvents()
    }
    if (ev.event === "friends") {
      if (!root.locked && root.lastError !== "helper_down" &&
          root.lastError !== "helper_incompatible" &&
          !root.persistentError(root.lastError))
        root.lastError = root.persistentWarning
      root.applyFriendSnapshot(ev.items || [])
      root.friendsReady = true
      root.applyPendingCallSnapshot()
      root.releaseDirectEvents()
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
            var updatedFriend = {}
            for (var friendKey in next[i])
              updatedFriend[friendKey] = next[i][friendKey]
            updatedFriend.avatar = path
            next[i] = updatedFriend
            found = true
          }
        }
        if (found)
          root.friends = next
      }
    }
    if (ev.event === "identity") {
      root.lastIdentityOp = String(ev.op || "")
      root.lastIdentityPath = String(ev.path || "")
      root.lastIdentityRequest = String(ev.request || "")
      root.lastIdentityProtected = ev.protected === true
      root.identityActionTick = root.identityActionTick + 1
      if (root.lastErrorRequest !== "" && root.identityErrorCode(root.lastError)) {
        root.lastError = root.persistentWarning
        root.lastErrorRequest = ""
      }
      if (ev.op === "recovered" && root.lastError === "identity_rollback_failed")
        root.lastError = root.persistentWarning
      if (ev.op === "unlock")
        root.locked = false
      if (ev.op === "import") {
        root.resetStateForIdentity(true)
        root.identityFingerprint = ""
        root.identityTick = root.identityTick + 1
      }
      if (ev.op === "import" || ev.op === "unlock")
        root.requestHelperStatus()
      if (ev.protected !== undefined)
        root.saveProtected = !!ev.protected
    }
    if (ev.event === "unread") {
      if (!root.directEventBindingValid(ev))
        return
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
      var messageConversation = String(ev.conversation || "")
      var messageKey = String(ev.key || "")
      if (!root.directEventBindingValid(ev))
        return
      if (ev.dir === "out")
        root.finishInFlightMessage(ev.request)
      root.lastChatId = String(ev.id || "")
      root.lastChatReply = String(ev.reply || "")
      root.lastChatDir = ev.dir === "out" ? "out" : (ev.dir === "sys" ? "sys" : "in")
      root.lastChatKind = String(ev.kind || "")
      root.lastChatSender = String(ev.sender || "")
      root.lastChatTimestamp = Number(ev.ts || 0)
      root.lastChatRequest = String(ev.request || "")
      root.lastChatKey = messageKey
      if (root.lastChatDir === "in" &&
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
        if (String(ev.conversation).charAt(0) !== "g") {
          root.lastDirectId = String(ev.conversation)
          root.lastDirectKey = root.friendKeyForConversation(root.lastDirectId)
        }
      }
      root.lastChatConv = ev.conversation || root.lastConversation
      root.lastChatText = ev.text || ""
      root.messageTick = root.messageTick + 1
    }
    if (ev.event === "search") {
      if (!root.directEventBindingValid(ev))
        return
      var searchConversation = String(ev.conversation || "")
      var searchRequest = String(ev.request || "")
      var searchKey = String(ev.key || "")
      if (!searchConversation || !searchRequest)
        return
      root.chatSearchResult(searchConversation, searchKey, searchRequest,
        ev.items || [])
    }
    if (ev.event === "message.updated") {
      if (!root.directEventBindingValid(ev))
        return
      root.lastUpdateConv = String(ev.conversation || "")
      root.lastUpdateId = String(ev.id || "")
      root.lastUpdateText = String(ev.text || "")
      root.lastUpdateDeleted = !!ev.deleted
      root.lastUpdateEdited = !!ev.edited
      root.updateTick = root.updateTick + 1
    }
    if (ev.event === "message.reaction") {
      if (!root.directEventBindingValid(ev))
        return
      root.lastReactionConv = String(ev.conversation || "")
      root.lastReactionId = String(ev.id || "")
      root.lastReactionEmoji = String(ev.emoji || "")
      root.lastReactionActor = String(ev.actor || "peer")
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
    if (ev.event === "history.failed") {
      var failedHistoryConv = String(ev.conversation || "")
      var expectedFailedHistoryRequest = String(root.historyRequestByConversation[failedHistoryConv] || "")
      if (!expectedFailedHistoryRequest ||
          String(ev.request || "") !== expectedFailedHistoryRequest)
        return
      var failedHistoryRequests = {}
      for (var failedRequestKey in root.historyRequestByConversation)
        if (failedRequestKey !== failedHistoryConv)
          failedHistoryRequests[failedRequestKey] = root.historyRequestByConversation[failedRequestKey]
      root.historyRequestByConversation = failedHistoryRequests
      var failedHistoryKeys = {}
      for (var failedKey in root.historyKeyByConversation)
        if (failedKey !== failedHistoryConv)
          failedHistoryKeys[failedKey] = root.historyKeyByConversation[failedKey]
      root.historyKeyByConversation = failedHistoryKeys
      var failedHistoryPending = {}
      for (var failedPendingKey in root.pendingHistoryUnread) {
        if (failedPendingKey !== failedHistoryConv)
          failedHistoryPending[failedPendingKey] = root.pendingHistoryUnread[failedPendingKey]
        else if ((root.pendingHistoryUnread[failedPendingKey] || []).length > 1)
          failedHistoryPending[failedPendingKey] = root.pendingHistoryUnread[failedPendingKey].slice(1)
      }
      root.pendingHistoryUnread = failedHistoryPending
      root.lastHistoryFailedConv = failedHistoryConv
      root.lastHistoryFailedCode = String(ev.code || "history_failed")
      root.historyFailedTick = root.historyFailedTick + 1
    }
    if (ev.event === "history") {
      if (!root.directEventBindingValid(ev))
        return
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
      var historyKeyNext = {}
      for (var historyBindingKey in root.historyKeyByConversation)
        if (historyBindingKey !== historyConv)
          historyKeyNext[historyBindingKey] = root.historyKeyByConversation[historyBindingKey]
      root.historyKeyByConversation = historyKeyNext
      var historyQueue = root.pendingHistoryUnread[historyConv] || []
      var historyUnread = ev.unread !== undefined
        ? Math.max(0, Number(ev.unread || 0))
        : (historyQueue.length > 0 ? Number(historyQueue[0] || 0) : 0)
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
    if (ev.event === "conversation.read") {
      root.lastConversationReadConv = String(ev.conversation || "")
      root.conversationReadTick = root.conversationReadTick + 1
    }
    if (ev.event === "conversation.read.failed") {
      root.lastConversationReadFailedConv = String(ev.conversation || "")
      root.lastConversationReadFailedCode = String(ev.code || "receipt_state_failed")
      root.conversationReadFailedTick = root.conversationReadFailedTick + 1
    }
    if (ev.event === "receipt") {
      if (!root.directEventBindingValid(ev))
        return
      var receiptConversation = String(ev.conversation || "")
      if (receiptConversation.charAt(0) === "g" &&
          (!/^g:[0-9a-f]{64}$/.test(receiptConversation) ||
           !/^[0-9a-f]{64}$/.test(String(ev.actor || ""))))
        return
      root.lastReceiptConv = receiptConversation
      root.lastReceiptId = String(ev.id || "")
      root.lastReceiptState = String(ev.state || "")
      root.lastReceiptActor = String(ev.actor || "")
      root.receiptTick = root.receiptTick + 1
    }
    if (ev.event === "receipt.sent") {
      if (!root.directEventBindingValid(ev))
        return
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
      if (!root.directEventBindingValid(ev))
        return
      var typingConv = String(ev.conversation || "")
      if (typingConv.charAt(0) === "g") {
        var actor = String(ev.actor || "")
        if (!root.supportsGroupTyping || !/^g:[0-9a-f]{64}$/.test(typingConv) ||
            !/^[0-9a-f]{64}$/.test(actor))
          return
        var groupTypingNext = {}
        var groupTypingKey
        for (groupTypingKey in root.groupTypingByConv)
          groupTypingNext[groupTypingKey] = root.groupTypingByConv[groupTypingKey]
        var actors = {}
        var existingActors = root.groupTypingByConv[typingConv] || ({})
        for (var existingActor in existingActors)
          actors[existingActor] = existingActors[existingActor]
        if (ev.typing)
          actors[actor] = Date.now() + 4500
        else
          delete actors[actor]
        if (Object.keys(actors).length > 0)
          groupTypingNext[typingConv] = actors
        else
          delete groupTypingNext[typingConv]
        root.groupTypingByConv = groupTypingNext
      } else {
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
      }
      root.typingTick = root.typingTick + 1
    }
    if (ev.event === "surface") {
      root.lastSurface = ev
      root.applySurfaceEvent(ev)
    }
    if (ev.event === "surfaces") {
      root.surfaces = ev.items || []
      root.surfacesTick = root.surfacesTick + 1
    }
    if (ev.event === "settings.auto-open" ||
        ev.event === "settings.auto-open.failed") {
      root.lastAutoOpenItems = ev.event === "settings.auto-open" ? (ev.items || []) : []
      root.lastAutoOpenDirectDefault = ev.event === "settings.auto-open"
        ? !!ev.directDefault : false
      root.lastAutoOpenRequest = String(ev.request || "")
      root.lastAutoOpenCode = String(ev.code || "")
      root.lastAutoOpenSucceeded = ev.event === "settings.auto-open"
      root.autoOpenTick = root.autoOpenTick + 1
    }
    if (ev.event === "sound.list" || ev.event === "sound.failed") {
      if (root.helperCompatibility !== "compatible" || !root.supportsCustomSounds)
        return
      if (typeof ev.request !== "string" || ev.request === "")
        return
      var soundRequest = ev.request
      var soundOperation = typeof ev.op === "string" ? ev.op : ""
      var pendingSound = root.pendingSoundRequests[soundRequest] || ({})
      var expectedSoundOperation = String(pendingSound.operation || "")
      if (!expectedSoundOperation)
        return
      var remainingSoundRequests = ({})
      for (var pendingSoundRequest in root.pendingSoundRequests)
        if (pendingSoundRequest !== soundRequest)
          remainingSoundRequests[pendingSoundRequest] =
            root.pendingSoundRequests[pendingSoundRequest]
      root.pendingSoundRequests = remainingSoundRequests
      root.lastSoundRequest = soundRequest
      root.lastSoundOperation = expectedSoundOperation
      root.lastSoundSelected = ""
      root.lastSoundCode = String(ev.code || "")
      root.lastSoundSucceeded = ev.event === "sound.list" &&
        soundOperation === expectedSoundOperation
      if (soundOperation !== expectedSoundOperation) {
        if (expectedSoundOperation === "list")
          root.customSounds = []
        root.lastSoundCode = "sound_state_failed"
        root.soundTick = root.soundTick + 1
        return
      }
      if (ev.event === "sound.failed" && soundOperation === "list")
        root.customSounds = []
      if (ev.event === "sound.list") {
        var soundItems = ev.items
        var validatedSounds = []
        var soundIds = ({})
        var validSounds = Array.isArray(soundItems) && soundItems.length <= 16 &&
          typeof ev.selected === "string"
        for (var soundIndex = 0; validSounds && soundIndex < soundItems.length;
             soundIndex++) {
          var soundItem = soundItems[soundIndex]
          if (!soundItem || Array.isArray(soundItem) ||
              typeof soundItem.id !== "string" ||
              typeof soundItem.label !== "string" ||
              typeof soundItem.path !== "string" ||
              typeof soundItem.size !== "number") {
            validSounds = false
            break
          }
          var soundId = soundItem.id
          var soundLabel = soundItem.label
          var soundPath = soundItem.path
          var soundSize = soundItem.size
          var expectedSoundPath = root.homeDir + "/custom-sounds/" +
            soundId + ".audio"
          if (!/^[0-9a-f]{32}$/.test(soundId) || soundIds[soundId] ||
              soundLabel.length < 1 || soundLabel.length > 96 ||
              /[\u0000-\u001f\u007f]/.test(soundLabel) ||
              soundPath !== expectedSoundPath || !Number.isInteger(soundSize) ||
              soundSize < 1 || soundSize > 8 * 1024 * 1024) {
            validSounds = false
            break
          }
          soundIds[soundId] = true
          validatedSounds.push({ id: soundId, label: soundLabel,
            path: soundPath, size: soundSize })
        }
        var selectedSound = typeof ev.selected === "string" ? ev.selected : ""
        if ((soundOperation === "import" &&
             (!/^[0-9a-f]{32}$/.test(selectedSound) || !soundIds[selectedSound])) ||
            (soundOperation !== "import" && selectedSound !== ""))
          validSounds = false
        if (validSounds) {
          root.customSounds = validatedSounds
          root.lastSoundSelected = selectedSound
          root.lastSoundCode = ""
        } else {
          root.customSounds = []
          root.lastSoundSucceeded = false
          root.lastSoundCode = "sound_state_failed"
        }
      }
      root.soundTick = root.soundTick + 1
    }
    if (ev.event === "invite") {
      if (ev.url !== undefined)
        root.inviteUrl = String(ev.url || "")
      if (ev.expires !== undefined) {
        var inviteExpiry = Number(ev.expires || 0)
        root.inviteExpiresAt = Number.isFinite(inviteExpiry) && inviteExpiry > 0
          ? Math.floor(inviteExpiry) : 0
      }
      if (ev.qr)
        root.qrPath = ev.qr
      else if (ev.url !== undefined && String(ev.url || "") === "")
        root.qrPath = ""
      if (ev.op && ev.request) {
        root.lastInviteAction = String(ev.op)
        root.lastInviteRequest = String(ev.request)
        root.inviteActionTick = root.inviteActionTick + 1
      }
    }
    if (ev.event === "request") {
      root.pending = true
      root.pendingGroup = ev.kind === "group"
    }
    if (ev.event === "group.list.begin")
      root.beginGroupProjection(ev)
    if (ev.event === "group.info" && root.groupProjectionEventMatches(ev)) {
      var groupId = String(ev.group || "")
      var memberCount = Number(ev.members)
      var groupLimit = Number(ev.limit)
      if (!/^g:[0-9a-f]{64}$/.test(groupId) ||
          !Number.isInteger(memberCount) || !Number.isInteger(groupLimit) ||
          memberCount < 0 || groupLimit < 1 || groupLimit > 10 ||
          memberCount > groupLimit || root.pendingGroupBuild[groupId]) {
        root.pendingGroupInvalid = true
      } else {
        var groupBuild = {}
        var existingGroup
        for (existingGroup in root.pendingGroupBuild)
          groupBuild[existingGroup] = root.pendingGroupBuild[existingGroup]
        groupBuild[groupId] = {
          id: groupId,
          title: String(ev.title || "Group"),
          memberCount: memberCount,
          limit: groupLimit,
          members: []
        }
        root.pendingGroupBuild = groupBuild
        var groupOrder = root.pendingGroupOrder.slice()
        groupOrder.push(groupId)
        root.pendingGroupOrder = groupOrder
      }
    }
    if (ev.event === "group.member" && root.groupProjectionEventMatches(ev)) {
      var memberGroupId = String(ev.group || "")
      var stagedGroup = root.pendingGroupBuild[memberGroupId]
      var peer = String(ev.peer || "")
      var memberKey = String(ev.key || "")
      var friendKey = String(ev.friendKey || "")
      var role = String(ev.role || "member")
      var validMember = !!stagedGroup && /^(0|[1-9][0-9]*)$/.test(peer) &&
        /^[0-9a-f]{64}$/.test(memberKey) &&
        (friendKey === "" || /^[0-9a-f]{64}$/.test(friendKey)) &&
        ["owner", "admin", "member"].indexOf(role) >= 0
      var members = stagedGroup ? (stagedGroup.members || []).slice() : []
      for (var memberIndex = 0; validMember && memberIndex < members.length; memberIndex++)
        if (String(members[memberIndex].peer || "") === peer ||
            String(members[memberIndex].key || "") === memberKey ||
            (!!ev.self && !!members[memberIndex].self))
          validMember = false
      if (!validMember || members.length >= Number(stagedGroup && stagedGroup.memberCount || 0)) {
        root.pendingGroupInvalid = true
      } else {
        var memberBuild = {}
        var stagedKey
        for (stagedKey in root.pendingGroupBuild)
          memberBuild[stagedKey] = root.pendingGroupBuild[stagedKey]
        members.push({
          peer: peer,
          key: memberKey,
          friendKey: friendKey,
          name: String(ev.name || "Member"),
          role: role,
          online: !!ev.online,
          self: !!ev.self
        })
        memberBuild[memberGroupId] = {
          id: stagedGroup.id,
          title: stagedGroup.title,
          memberCount: stagedGroup.memberCount,
          limit: stagedGroup.limit,
          members: members
        }
        root.pendingGroupBuild = memberBuild
        root.pendingGroupReceivedMembers = root.pendingGroupReceivedMembers + 1
      }
    }
    if (ev.event === "group.list.end" && root.groupProjectionEventMatches(ev)) {
      if (!root.groupProjectionComplete(ev)) {
        root.rejectGroupProjection()
      } else {
        var nextGroups = []
        for (var groupIndex = 0; groupIndex < root.pendingGroupOrder.length; groupIndex++) {
          var completedGroup = root.pendingGroupBuild[root.pendingGroupOrder[groupIndex]]
          if (completedGroup)
            nextGroups.push(completedGroup)
        }
        root.groups = nextGroups
        root.groupsReady = true
        root.groupProjectionFailed = false
        root.groupRefreshAttempts = 0
        root.lastGroupGeneration = Number(root.pendingGroupGeneration)
        root.expectedGroupRequest = ""
        groupProjectionTimer.stop()
        root.groupsTick = root.groupsTick + 1
        if (root.lastGroup && !root.groupById(root.lastGroup))
          root.lastGroup = nextGroups.length ? String(nextGroups[0].id) : ""
        root.clearPendingGroupProjection()
      }
    }
    if (ev.event === "group.invite.sent") {
      root.lastGroupInviteSentGroup = String(ev.group || "")
      root.lastGroupInviteSentFriend = String(ev.friend || "")
      root.lastGroupInviteSentRequest = String(ev.request || "")
      root.groupInviteSentTick = root.groupInviteSentTick + 1
    }
    if (ev.event === "group.invite.failed") {
      root.lastGroupInviteFailedGroup = String(ev.group || "")
      root.lastGroupInviteFailedFriend = String(ev.friend || "")
      root.lastGroupInviteFailedRequest = String(ev.request || "")
      root.lastGroupInviteFailedCode = String(ev.code || "forbidden")
      root.groupInviteFailedTick = root.groupInviteFailedTick + 1
    }
    if (ev.event === "group.changed") {
      if ((ev.action === "dissolve" || ev.action === "leave") && ev.group) {
        root.lastRemovedGroup = String(ev.group)
        root.removedGroupTick = root.removedGroupTick + 1
      }
      if ((ev.action === "dissolve" || ev.action === "leave") && ev.group === root.lastGroup &&
          (ev.action === "dissolve" || String(ev.peer || "") === "0"))
        root.lastGroup = ""
      else if ((ev.action === "create" || ev.action === "join") && ev.group)
        root.lastGroup = ev.group
      if (ev.action === "create" || ev.action === "join")
        root.lastConversation = ev.group || root.lastConversation
    }
    if (ev.event === "safety" && ev.conversation && ev.request &&
        String(ev.conversation) === root.selectedDirectId &&
        String(ev.request) === root.safetyRequest) {
      root.safetyCode = String(ev.code || "")
      root.safetyConv = String(ev.conversation)
    }
    if (ev.event === "attachment.discarded") {
      var discardedRequest = String(ev.request || "")
      var remainingCleanup = []
      for (var cleanupIndex = 0;
           cleanupIndex < root.attachmentCleanupDebts.length; cleanupIndex++)
        if (String(root.attachmentCleanupDebts[cleanupIndex].request || "") !==
            discardedRequest)
          remainingCleanup.push(root.attachmentCleanupDebts[cleanupIndex])
      root.attachmentCleanupDebts = remainingCleanup
    }
    if (ev.event === "attachment.stage") {
      root.lastAttachmentStageRequest = String(ev.request || "")
      root.lastAttachmentStagePath = String(ev.path || "")
      root.attachmentStageTick = root.attachmentStageTick + 1
    }
    if (ev.event === "attachment.inspected" || ev.event === "attachment.rejected") {
      root.lastAttachmentInspectionRequest = String(ev.request || "")
      root.lastAttachmentInspectionPath = String(ev.path || "")
      root.lastAttachmentInspectionAccepted = ev.event === "attachment.inspected" &&
        String(ev.kind || "") === "image"
      root.attachmentInspectionTick = root.attachmentInspectionTick + 1
    }
    if (ev.event === "file.offer") {
      if (!root.directEventBindingValid(ev))
        return
      root.lastFileState = "offer"
      root.lastFileDir = "in"
      root.lastFileError = ""
      var offerConv = String(ev.conversation || root.lastConversation)
      var offerKey = String(ev.key || root.friendKeyForConversation(offerConv))
      var existingOffer = root.fileOffer(offerConv)
      if (existingOffer.active && existingOffer.id && ev.id &&
          String(existingOffer.id) !== String(ev.id)) {
        root.sendConversationOp({ op: "file.cancel", conversation: offerConv,
          id: String(ev.id) }, offerKey, false)
        return
      }
      root.dismissFileNotice(offerConv)
      root.setFileOffer(offerConv, { id: ev.id || "", name: ev.name || "", path: "",
        key: offerKey, kind: String(ev.kind || "file"),
        sender: String(ev.sender || ""), pending: true, active: true })
      root.lastFileId = ev.id || ""
      root.lastFileName = ev.name || ""
      root.pendingFile = true
      root.lastConversation = offerConv
      root.lastFileConv = offerConv
      if (offerConv.charAt(0) !== "g") {
        root.lastDirectId = offerConv
        root.lastDirectKey = offerKey
      }
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.sending" && ev.dir === "out") {
      if (!root.directEventBindingValid(ev))
        return
      var sendingConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var sendingOld = root.outgoingFile(sendingConv)
      if (!root.outgoingEventMatches(ev, sendingConv))
        return
      root.dismissFileNotice(sendingConv)
      root.setOutgoingFile(sendingConv, {
        id: String(ev.id || sendingOld.id || ""),
        path: String(sendingOld.path || ""),
        request: String(sendingOld.request || ev.request || ""),
        key: String(sendingOld.key || ""),
        pending: true,
        cancelRequested: !!sendingOld.cancelRequested
      })
      root.lastFileId = String(ev.id || "")
      root.lastFileConv = sendingConv
      root.lastFileDir = "out"
      if (sendingOld.cancelRequested && ev.id)
        root.sendConversationOp({ op: "file.cancel", conversation: sendingConv,
          id: String(ev.id) }, String(sendingOld.key || ""), false)
      else
        root.lastFileState = "sending"
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.done") {
      if (!root.directEventBindingValid(ev))
        return
      var doneConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var doneDir = root.fileEventDirection(ev, doneConv)
      if ((doneDir === "out" && !root.outgoingEventMatches(ev, doneConv)) ||
          (doneDir === "in" && !root.incomingEventMatches(ev, doneConv)))
        return
      root.lastFileState = "done"
      root.lastFileError = String(ev.code || "")
      root.dismissFileNotice(doneConv)
      root.lastFileDir = doneDir
      if (doneDir === "out") {
        var doneOutgoing = root.outgoingFile(doneConv)
        if (doneOutgoing.pending && (!doneOutgoing.id || String(doneOutgoing.id) === String(ev.id || "")))
          root.setOutgoingFile(doneConv, { id: doneOutgoing.id || ev.id || "", path: doneOutgoing.path || "", request: doneOutgoing.request || "", key: doneOutgoing.key || "", pending: false, cancelRequested: false })
        root.lastFilePath = ""
      } else {
        var doneOld = root.fileOffer(doneConv)
        root.setFileOffer(doneConv, { id: ev.id || doneOld.id || "",
          name: doneOld.name || root.lastFileName, path: ev.path || "",
          key: doneOld.key || root.friendKeyForConversation(doneConv),
          kind: String(ev.kind || doneOld.kind || "file"),
          sender: String(ev.sender || doneOld.sender || ""),
          pending: false, active: false })
        root.pendingFile = false
        root.lastFilePath = ev.path || ""
      }
      root.lastConversation = doneConv
      root.lastFileConv = doneConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.canceled") {
      if (!root.directEventBindingValid(ev))
        return
      var canceledConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var canceledDir = root.fileEventDirection(ev, canceledConv)
      if ((canceledDir === "out" && !root.outgoingEventMatches(ev, canceledConv)) ||
          (canceledDir === "in" && !root.incomingEventMatches(ev, canceledConv)))
        return
      root.lastFileState = "canceled"
      root.lastFileError = ""
      root.lastFileDir = canceledDir
      if (canceledDir === "out") {
        var canceledOutgoing = root.outgoingFile(canceledConv)
        if (!canceledOutgoing.id || String(canceledOutgoing.id) === String(ev.id || ""))
          root.setOutgoingFile(canceledConv, { id: canceledOutgoing.id || ev.id || "", path: canceledOutgoing.path || "", request: canceledOutgoing.request || "", key: canceledOutgoing.key || "", pending: false, cancelRequested: false })
      } else {
        var canceledOffer = root.fileOffer(canceledConv)
        root.setFileOffer(canceledConv, { id: canceledOffer.id || ev.id || "",
          name: canceledOffer.name || "", path: "", key: canceledOffer.key || "",
          kind: canceledOffer.kind || "file", sender: canceledOffer.sender || "",
          pending: false, active: false })
        root.pendingFile = false
      }
      root.lastFilePath = ""
      root.lastFileConv = canceledConv
      root.setFileNotice(canceledConv, { state: "canceled", dir: canceledDir,
        key: String(ev.key || root.friendKeyForConversation(canceledConv)) })
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.failed") {
      if (String(ev.key || "") !== "" && !root.directEventBindingValid(ev))
        return
      var failedConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var failedDir = root.fileEventDirection(ev, failedConv)
      if ((failedDir === "out" && !root.outgoingEventMatches(ev, failedConv)) ||
          (failedDir === "in" && !root.incomingEventMatches(ev, failedConv)))
        return
      root.lastFileState = "failed"
      root.lastFileError = ev.code || "file_failed"
      root.dismissFileNotice(failedConv)
      root.lastFileDir = failedDir
      if (failedDir === "out") {
        var failedOutgoing = root.outgoingFile(failedConv)
        if (!failedOutgoing.id || String(failedOutgoing.id) === String(ev.id || ""))
          root.setOutgoingFile(failedConv, { id: failedOutgoing.id || ev.id || "", path: failedOutgoing.path || "", request: failedOutgoing.request || "", key: failedOutgoing.key || "", pending: false, cancelRequested: false })
      } else {
        var failedOld = root.fileOffer(failedConv)
        root.setFileOffer(failedConv, { id: failedOld.id || ev.id || "",
          name: failedOld.name || "", path: "", key: failedOld.key || "",
          kind: failedOld.kind || "file", sender: failedOld.sender || "",
          pending: false, active: false })
        root.pendingFile = false
      }
      root.lastError = "file_failed"
      root.lastErrorConv = failedConv
      root.lastErrorTick = root.lastErrorTick + 1
      root.lastFileConv = failedConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "call.incoming") {
      if (!root.directEventBindingValid(ev))
        return
      root.incomingCall = true
      root.callToneSuppressed = false
      root.callDurationSeconds = 0
      root.lastCallState = "incoming"
      if (ev.conversation)
        root.lastConversation = ev.conversation
      root.lastCallConv = ev.conversation || root.lastConversation
      if (root.lastCallConv && String(root.lastCallConv).charAt(0) !== "g") {
        root.lastCallKey = String(ev.key || root.friendKeyForConversation(root.lastCallConv))
        root.lastDirectId = String(root.lastCallConv)
        root.lastDirectKey = root.lastCallKey
      }
    }
    if (ev.event === "call.state") {
      if (!root.directEventBindingValid(ev))
        return
      var nextCallState = String(ev.state || "")
      if (nextCallState === "active" &&
          (root.lastCallState !== "active" ||
           String(root.lastCallConv || "") !== String(ev.conversation || "")))
        root.callDurationSeconds = 0
      root.lastCallState = nextCallState
      root.callToneSuppressed = nextCallState !== "incoming" &&
        nextCallState !== "ringing"
      if (ev.state === "ended" || ev.state === "") {
        root.incomingCall = false
        root.callDurationSeconds = 0
      } else if (ev.state === "active") {
        root.incomingCall = false
      }
      if (ev.conversation)
        root.lastConversation = ev.conversation
      if (ev.conversation)
        root.lastCallConv = ev.conversation
      if (ev.conversation && String(ev.conversation).charAt(0) !== "g") {
        root.lastCallKey = String(ev.key || root.friendKeyForConversation(ev.conversation))
        root.lastDirectId = String(ev.conversation)
        root.lastDirectKey = root.lastCallKey
      }
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
  function fileNotice(conv) {
    var c = String(conv || "")
    return c && root.fileNotices[c] ? root.fileNotices[c] : ({})
  }
  function setFileNotice(conv, notice) {
    var c = String(conv || "")
    var next = {}
    var key
    if (!c)
      return
    for (key in root.fileNotices)
      if (key !== c)
        next[key] = root.fileNotices[key]
    if (notice && notice.state)
      next[c] = notice
    root.fileNotices = next
    root.fileNoticeTick = root.fileNoticeTick + 1
  }
  function dismissFileNotice(conv) { root.setFileNotice(conv, null) }
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
  function incomingEventMatches(ev, conv) {
    var offer = root.fileOffer(conv)
    if (!offer.id || !ev || !ev.id)
      return true
    return String(offer.id) === String(ev.id)
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

  function failActiveIncomingFiles() {
    var transferIds = {}
    var key
    for (key in root.fileOffers) {
      var offer = root.fileOffers[key]
      if (offer && offer.id)
        transferIds[String(offer.id)] = true
    }
    var keptOps = []
    for (var opIndex = 0; opIndex < root.pendingOps.length; opIndex++) {
      var keep = true
      try {
        var queued = JSON.parse(root.pendingOps[opIndex])
        if ((queued.op === "file.accept" || queued.op === "file.cancel") &&
            transferIds[String(queued.id || "")])
          keep = false
      } catch (e) {
      }
      if (keep)
        keptOps.push(root.pendingOps[opIndex])
    }
    root.pendingOps = keptOps
    root.fileOffers = ({})
    root.pendingFile = false
  }

  function reconcileOutgoingFiles() {
    var key
    for (key in root.outgoingFiles) {
      var transfer = root.outgoingFiles[key]
      if (!transfer || !transfer.pending || !transfer.request)
        continue
      root.sendConversationOp({ op: "file.status", conversation: String(key),
        id: transfer.request }, String(transfer.key || ""), false)
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

  function directConversationOperation(name) {
    return ["msg.send", "history", "search", "history.clear",
      "message.edit", "message.delete", "message.react", "conversation.read",
      "unread.clear", "receipt.send", "typing.set", "surface.set", "surface.get",
      "file.send", "file.status", "file.accept", "file.cancel", "call.start",
      "call.answer", "call.stop"].indexOf(
        String(name || "")) >= 0
  }

  function groupConversationOperation(name) {
    return ["msg.send", "history", "search", "history.clear", "message.edit",
      "message.delete", "message.react", "conversation.read", "unread.clear",
      "receipt.send", "typing.set", "surface.set", "surface.get", "file.send",
      "file.status", "file.accept", "file.cancel"].indexOf(String(name || "")) >= 0
  }

  function operationBindingValid(operation) {
    if (!operation || !root.directConversationOperation(operation.op))
      return true
    var conversation = String(operation.conversation || "")
    if (/^g:[0-9a-f]{64}$/.test(conversation))
      return root.groupConversationOperation(operation.op)
    if (!/^(0|[1-9][0-9]*)$/.test(conversation))
      return false
    return root.directBindingMatches(conversation, String(operation.key || ""))
  }

  function rejectBoundOperation(operation, reason) {
    var op = operation || ({})
    var code = String(reason || "identity_changed")
    var conversation = String(op.conversation || "")
    root.lastError = code
    root.lastErrorConv = conversation
    root.lastErrorRequest = String(op.id || op.request || "")
    root.lastErrorTick = root.lastErrorTick + 1
    if (op.op === "msg.send" && op.id) {
      root.lastMessageFailedConv = conversation
      root.lastMessageFailedRequest = String(op.id)
      root.lastMessageFailedCode = code
      root.lastMessageFailedDelivered = false
      root.messageFailedTick = root.messageFailedTick + 1
    } else if (op.op === "file.send") {
      root.setOutgoingFile(conversation, {})
      root.lastFileConv = conversation
      root.lastFileState = "failed"
      root.lastFileDir = "out"
      root.lastFileError = code
      root.lastFileTick = root.lastFileTick + 1
    }
  }

  function writeQueuedOperations(lines, writer) {
    for (var i = 0; i < lines.length; i++) {
      var operation
      try { operation = JSON.parse(lines[i]) } catch (e) {
        continue
      }
      if (!root.operationBindingValid(operation)) {
        root.rejectBoundOperation(operation, "identity_changed")
        continue
      }
      root.trackInFlightMessage(operation)
      if (operation.op === "surface.set" && root.activeHelperProtocol < 14) {
        delete operation.width
        delete operation.height
        writer(JSON.stringify(operation) + "\n")
      } else {
        writer(lines[i])
      }
    }
  }

  function flushOps() {
    if (root.awaitingHelperInstance || !root.pendingOps.length)
      return
    if (sock.connected) {
      var queued = root.pendingOps
      root.pendingOps = []
      root.writeQueuedOperations(queued, function(line) { sock.write(line) })
    } else if (root.procReady) {
      var pending = root.pendingOps
      root.pendingOps = []
      root.writeQueuedOperations(pending, function(line) { proc.write(line) })
    }
  }

  function failQueuedGroupInvites(reason) {
    var remaining = []
    for (var i = 0; i < root.pendingOps.length; i++) {
      var queued
      try { queued = JSON.parse(root.pendingOps[i]) } catch (e) {
        remaining.push(root.pendingOps[i])
        continue
      }
      if (!queued || queued.op !== "invite.create" || queued.kind !== "group" ||
          !queued.id || !queued.request) {
        remaining.push(root.pendingOps[i])
        continue
      }
      root.lastGroupInviteFailedGroup = String(queued.group || "")
      root.lastGroupInviteFailedFriend = String(queued.id || "")
      root.lastGroupInviteFailedRequest = String(queued.request || "")
      root.lastGroupInviteFailedCode = String(reason || "helper_down")
      root.groupInviteFailedTick = root.groupInviteFailedTick + 1
    }
    root.pendingOps = remaining
  }

  function failPendingSoundRequests(reason) {
    var code = String(reason || "helper_restarted")
    for (var request in root.pendingSoundRequests) {
      root.lastSoundRequest = request
      root.lastSoundOperation = String(
        root.pendingSoundRequests[request].operation || "")
      root.lastSoundSelected = ""
      root.lastSoundCode = code
      root.lastSoundSucceeded = false
      root.soundTick = root.soundTick + 1
    }
    root.pendingSoundRequests = ({})
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
    groupProjectionTimer.stop()
    root.awaitingHelperInstance = false
    root.helperStatusNonce = ""
    root.failActiveOutgoingFiles("helper_incompatible")
    root.failQueuedMessages("helper_incompatible")
    root.failQueuedGroupInvites("helper_incompatible")
    root.failPendingSoundRequests("helper_incompatible")
    root.helperInstanceGeneration = root.helperInstanceGeneration + 1
    root.pendingOps = []
    root.pendingHandshakeEvents = []
    root.pendingHandshakeBytes = 0
    root.handshakeEventOverflow = false
    root.pendingDirectEvents = []
    root.pendingDirectEventBytes = 0
    root.pendingDirectEventOverflow = false
    root.pendingCallSnapshot = null
    root.pendingCallSnapshotSet = false
    root.friendsReady = false
    root.groupsReady = false
    root.groupProjectionFailed = true
    root.helperCompatibility = "incompatible"
    root.activeHelperProtocol = 0
    root.customSounds = []
    root.soundTick = root.soundTick + 1
    root.connectionState = "reconnecting"
    root.selfOnline = false
    root.lastError = "helper_incompatible"
    root.lastErrorConv = ""
    root.lastErrorTick = root.lastErrorTick + 1
  }

  function requestHelperStatus() {
    root.friendsReady = false
    root.groupsReady = false
    root.groupProjectionFailed = false
    root.helperStatusSequence = root.helperStatusSequence + 1
    root.helperStatusNonce = Date.now().toString(36) + "-status-" +
      root.helperStatusSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    root.expectedGroupRequest = root.helperStatusNonce
    root.clearPendingGroupProjection()
    groupProjectionTimer.interval = 8000
    groupProjectionTimer.restart()
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
    if (!root.operationBindingValid(obj)) {
      root.rejectBoundOperation(obj, "identity_changed")
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

  function sendImmediateOp(obj) {
    if (root.helperCompatibility === "incompatible" || root.awaitingHelperInstance)
      return false
    if (!root.operationBindingValid(obj)) {
      root.rejectBoundOperation(obj, "identity_changed")
      return false
    }
    var line = JSON.stringify(obj) + "\n"
    if (sock.connected) {
      sock.write(line)
      return true
    }
    if (root.procReady) {
      proc.write(line)
      return true
    }
    return false
  }

  function sendConversationOp(operation, expectedKey, immediate) {
    var op = operation || ({})
    var conversation = String(op.conversation || "")
    if (/^(0|[1-9][0-9]*)$/.test(conversation)) {
      var key = String(expectedKey || "")
      if (!root.directBindingMatches(conversation, key)) {
        root.rejectBoundOperation(op, "identity_changed")
        return false
      }
      var bound = {}
      for (var field in op)
        bound[field] = op[field]
      bound.key = key
      op = bound
    }
    return immediate ? root.sendImmediateOp(op) : root.sendOp(op)
  }

  function requestAutoOpen(request) {
    var requestId = String(request || "")
    if (!root.supportsStableDirectState || !requestId)
      return false
    return root.sendOp({ op: "settings.auto-open.get", request: requestId })
  }

  function setAutoOpen(conversation, enabled, request) {
    var stable = String(conversation || "")
    var requestId = String(request || "")
    if (!root.supportsStableDirectState || !/^[dg]:[0-9a-f]{64}$/.test(stable) ||
        !requestId)
      return false
    return root.sendOp({ op: "settings.auto-open.set", conversation: stable,
      enabled: !!enabled, request: requestId })
  }

  function nextSoundRequest(operation) {
    root.soundRequestSequence = root.soundRequestSequence + 1
    return "sound-" + String(operation || "list") + "-" +
      Date.now().toString(36) + "-" + root.soundRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }
  function retryPendingSoundRequests() {
    if (root.awaitingHelperInstance || root.helperCompatibility !== "compatible")
      return
    for (var request in root.pendingSoundRequests) {
      var pending = root.pendingSoundRequests[request] || ({})
      if (pending.command)
        root.sendImmediateOp(pending.command)
    }
  }
  function sendSoundOperation(operation, fields, request) {
    var action = String(operation || "")
    var requestId = String(request || "")
    if (!root.supportsCustomSounds || ["list", "import", "remove"].indexOf(action) < 0 ||
        requestId === "" || root.pendingSoundRequests[requestId] !== undefined)
      return false
    var command = { op: "sound." + action, request: requestId }
    var values = fields || ({})
    for (var soundField in values)
      command[soundField] = values[soundField]
    var pending = ({})
    for (var pendingRequest in root.pendingSoundRequests)
      pending[pendingRequest] = root.pendingSoundRequests[pendingRequest]
    pending[requestId] = { operation: action, command: command }
    root.pendingSoundRequests = pending
    if (root.sendImmediateOp(command))
      return true
    delete pending[requestId]
    root.pendingSoundRequests = pending
    return false
  }
  function refreshCustomSounds() {
    if (!root.supportsCustomSounds || root.awaitingHelperInstance ||
        root.helperCompatibility !== "compatible")
      return false
    var request = root.nextSoundRequest("list")
    return root.sendSoundOperation("list", ({}), request)
  }
  function importCustomSound(path, request) {
    var source = String(path || "")
    if (source.charAt(0) !== "/" || source.length > 511)
      return false
    return root.sendSoundOperation("import", { path: source }, request)
  }
  function removeCustomSound(id, request) {
    var soundId = String(id || "")
    if (!/^[0-9a-f]{32}$/.test(soundId))
      return false
    return root.sendSoundOperation("remove", { id: soundId }, request)
  }

  function createInvite(request) {
    return sendImmediateOp({ op: "invite.create", kind: "direct", ttlSec: 86400,
      request: String(request || "") })
  }
  function revokeInvite(request) {
    return sendImmediateOp({ op: "invite.revoke", request: String(request || "") })
  }
  function setAvatar(path) { sendOp({ op: "avatar.set", path: path }) }
  function nicknameValid(value) {
    var nickname = String(value || "").trim()
    var characters = 0
    if (nickname === "")
      return false
    for (var i = 0; i < nickname.length; i++) {
      var code = nickname.charCodeAt(i)
      if (code < 0x20 || (code >= 0x7f && code <= 0x9f))
        return false
      if (code >= 0xd800 && code <= 0xdbff) {
        if (i + 1 >= nickname.length)
          return false
        var low = nickname.charCodeAt(i + 1)
        if (low < 0xdc00 || low > 0xdfff)
          return false
        i++
      } else if (code >= 0xdc00 && code <= 0xdfff) {
        return false
      }
      characters++
      if (characters > 18)
        return false
    }
    return true
  }
  function limitNickname(value, limit) {
    var nickname = String(value || "")
    var maximum = Math.max(0, Number(limit || 0))
    var characters = 0
    var end = 0
    while (end < nickname.length && characters < maximum) {
      var code = nickname.charCodeAt(end)
      if (code >= 0xd800 && code <= 0xdbff && end + 1 < nickname.length) {
        var low = nickname.charCodeAt(end + 1)
        end += low >= 0xdc00 && low <= 0xdfff ? 2 : 1
      } else {
        end++
      }
      characters++
    }
    return nickname.slice(0, end)
  }
  function setNickname(value, request) {
    var nickname = String(value || "").trim()
    var requestId = String(request || "")
    if (!root.supportsIdentityActions || !root.nicknameValid(nickname) || requestId === "")
      return false
    return sendImmediateOp({ op: "nickname.set", nickname: nickname, id: requestId })
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
    var expectedKey = key.charAt(0) === "g" ? "" : root.friendKeyForConversation(key)
    if (!root.sendConversationOp({ op: "unread.clear", conversation: key },
          expectedKey, false)) {
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
  function requestHistory(conv, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var bindingKey = c.charAt(0) === "g" ? "" : String(expectedKey || "")
    if (c.charAt(0) !== "g" && !root.directBindingMatches(c, bindingKey))
      return false
    var existingQueue = root.pendingHistoryUnread[c] || []
    if (existingQueue.length > 0) {
      if (Number(root.historyRetryTickByConversation[c] || 0) < root.reconnectGeneration)
        root.retryHistory(c)
      return true
    }
    var next = {}
    var retryNext = {}
    var requestNext = {}
    var keyNext = {}
    var requestId = root.nextHistoryRequestId()
    var key
    var queue = existingQueue.slice()
    for (key in root.pendingHistoryUnread)
      next[key] = root.pendingHistoryUnread[key]
    for (key in root.historyRetryTickByConversation)
      retryNext[key] = root.historyRetryTickByConversation[key]
    for (key in root.historyRequestByConversation)
      requestNext[key] = root.historyRequestByConversation[key]
    for (key in root.historyKeyByConversation)
      keyNext[key] = root.historyKeyByConversation[key]
    queue.push(root.unreadFor(c))
    next[c] = queue
    retryNext[c] = root.reconnectGeneration
    requestNext[c] = requestId
    keyNext[c] = bindingKey
    root.pendingHistoryUnread = next
    root.historyRetryTickByConversation = retryNext
    root.historyRequestByConversation = requestNext
    root.historyKeyByConversation = keyNext
    return root.sendConversationOp({ op: "history", conversation: c, limit: 50,
      id: requestId }, bindingKey, false)
  }
  function retryHistory(conv) {
    var c = String(conv || root.lastConversation || "")
    if (!c || Number(root.historyRetryTickByConversation[c] || 0) === root.reconnectGeneration)
      return
    var historyQueue = root.pendingHistoryUnread[c] || []
    var unread = historyQueue.length > 0 ? Number(historyQueue[0] || 0) : root.unreadFor(c)
    var bindingKey = String(root.historyKeyByConversation[c] || "")
    if (c.charAt(0) !== "g" && !root.directBindingMatches(c, bindingKey))
      return
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
    root.sendConversationOp({ op: "history", conversation: c, limit: 50,
      id: requestId }, bindingKey, false)
  }
  function editMessage(conv, id, text, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    var value = String(text || "")
    if (!c || !messageId || !value)
      return false
    return root.sendConversationOp({ op: "message.edit", conversation: c,
      id: messageId, text: value }, expectedKey, false)
  }
  function deleteMessage(conv, id, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId)
      return false
    return root.sendConversationOp({ op: "message.delete", conversation: c,
      id: messageId }, expectedKey, false)
  }
  function reactMessage(conv, id, emoji, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId)
      return false
    return root.sendConversationOp({ op: "message.react", conversation: c,
      id: messageId, text: String(emoji || "") }, expectedKey, false)
  }
  function clearHistory(conv, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    if (!c)
      return false
    return root.sendConversationOp({ op: "history.clear", conversation: c },
      expectedKey, false)
  }
  function markConversationRead(conv, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    if (!c)
      return false
    return root.sendConversationOp({ op: "conversation.read", conversation: c },
      expectedKey, false)
  }
  function sendReceipt(conv, id, state, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var messageId = String(id || "")
    if (!c || !messageId || (state !== "delivered" && state !== "read"))
      return false
    return root.sendConversationOp({ op: "receipt.send", conversation: c,
      id: messageId, state: state }, expectedKey, false)
  }
  function setTyping(conv, typing, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var group = /^g:[0-9a-f]{64}$/.test(c)
    if (!c || (c.charAt(0) === "g" && (!group || !root.supportsGroupTyping)))
      return false
    return root.sendConversationOp({ op: "typing.set", conversation: c,
      typing: !!typing }, group ? "" : expectedKey, false)
  }
  function groupTypingActors(conv) {
    var c = String(conv || "")
    var source = root.groupTypingByConv[c] || ({})
    var actors = []
    var now = Date.now()
    for (var actor in source)
      if (/^[0-9a-f]{64}$/.test(actor) && Number(source[actor]) > now)
        actors.push(actor)
    return actors
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
  function redeem(url) {
    root.redeemRequestSequence = root.redeemRequestSequence + 1
    var requestId = Date.now().toString(36) + "-redeem-" +
      root.redeemRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    if (!sendOp({ op: "invite.redeem", payload: String(url || ""), id: requestId }))
      return ""
    return root.supportsRedeemResults ? requestId : "legacy"
  }
  function acknowledgeIdentityPrimary(request) {
    var requestId = String(request || "")
    if (!requestId || !root.supportsDirectRecovery)
      return false
    return sendImmediateOp({ op: "identity.primary.acknowledge", id: requestId })
  }
  function clearDirectReinvite(request) {
    var requestId = String(request || "")
    if (!requestId || !root.supportsDirectRecovery)
      return false
    return sendImmediateOp({ op: "direct.reinvite.clear", id: requestId })
  }
  function decide(ok) {
    sendOp({ op: "contact.decide", id: "x", accept: !!ok })
    root.pending = false
  }
  function removeContact(contactId, expectedKey) {
    var id = String(contactId || "")
    var key = String(expectedKey || "")
    var found = false
    if (!/^(0|[1-9][0-9]*)$/.test(id) || !/^[0-9a-f]{64}$/.test(key))
      return false
    for (var i = 0; i < root.friends.length; i++)
      if (String(root.friends[i].id || "") === id &&
          String(root.friends[i].key || "") === key) {
        found = true
        break
      }
    if (!found)
      return false
    if (!sendOp({ op: "contact.remove", id: id, key: key }))
      return false
    if (String(root.safetyConv || "") === id) {
      root.safetyCode = ""
      root.safetyConv = ""
    }
    return true
  }
  function rotateNospam() {
    sendOp({ op: "nospam.rotate" })
    root.inviteUrl = ""
    root.inviteExpiresAt = 0
    root.qrPath = ""
  }
  function getSafety(conversation) {
    var selected = String(conversation || root.selectedDirectId || "")
    if (selected === "" || selected !== root.selectedDirectId ||
        !/^[0-9a-f]{64}$/.test(root.selectedDirectKey))
      return false
    root.safetyRequestSequence++
    root.safetyRequest = Date.now().toString(36) + "-safety-" +
      root.safetyRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    root.safetyCode = ""
    root.safetyConv = selected
    return sendOp({ op: "safety.get", conversation: selected,
      key: root.selectedDirectKey, id: root.safetyRequest })
  }
  function groupById(groupId) {
    var key = String(groupId || "")
    for (var i = 0; i < root.groups.length; i++)
      if (String(root.groups[i].id || "") === key)
        return root.groups[i]
    return null
  }
  function groupName(groupId) {
    var group = root.groupById(groupId)
    return group ? String(group.title || "Group") : "Group"
  }
  function groupMembers(groupId) {
    var group = root.groupById(groupId)
    return group ? (group.members || []) : []
  }
  function groupInviteCandidates(groupId) {
    var members = root.groupMembers(groupId)
    var memberKeys = ({})
    var candidates = []
    var i
    for (i = 0; i < members.length; i++) {
      var memberKey = String(members[i].friendKey || "")
      if (/^[0-9a-f]{64}$/.test(memberKey))
        memberKeys[memberKey] = true
    }
    for (i = 0; i < root.friends.length; i++) {
      var friendKey = String(root.friends[i].key || "")
      if (/^[0-9a-f]{64}$/.test(friendKey) && !memberKeys[friendKey])
        candidates.push(root.friends[i])
    }
    return candidates
  }
  function groupInviteCandidateMatches(groupId, friendId, expectedKey) {
    var id = String(friendId || "")
    var key = String(expectedKey || "")
    var candidates = root.groupInviteCandidates(groupId)
    for (var i = 0; i < candidates.length; i++)
      if (String(candidates[i].id || "") === id &&
          String(candidates[i].key || "") === key)
        return true
    return false
  }
  function groupSelfRole(groupId) {
    var members = root.groupMembers(groupId)
    for (var i = 0; i < members.length; i++)
      if (members[i].self)
        return String(members[i].role || "member")
    return "member"
  }
  function groupOnlineCount(groupId) {
    var members = root.groupMembers(groupId)
    var count = 0
    for (var i = 0; i < members.length; i++)
      if (members[i].online && !members[i].self)
        count++
    return count
  }
  function selectDirect(friendId) {
    var key = String(friendId || "")
    var foundKey = ""
    for (var i = 0; i < root.friends.length; i++)
      if (String(root.friends[i].id || "") === key) {
        foundKey = String(root.friends[i].key || "")
        break
      }
    if (!/^[0-9a-f]{64}$/.test(foundKey))
      return false
    root.lastConversation = key
    root.lastDirectId = key
    root.lastDirectKey = foundKey
    root.selectedConversation = key
    root.selectedDirectId = key
    root.selectedDirectKey = foundKey
    root.safetyRequest = ""
    root.safetyCode = ""
    root.safetyConv = ""
    return true
  }

  function selectGroup(groupId, userSelected) {
    var key = String(groupId || "")
    if (!root.groupById(key))
      return false
    root.lastGroup = key
    root.lastConversation = key
    if (userSelected === true) {
      root.selectedConversation = key
      root.selectedDirectId = ""
      root.selectedDirectKey = ""
      root.safetyRequest = ""
      root.safetyCode = ""
      root.safetyConv = ""
    }
    return true
  }
  function groupTitleOk(title) {
    var name = String(title || "").trim()
    if (!name)
      return false
    var encoded
    try { encoded = encodeURIComponent(name) } catch (e) { return false }
    var bytes = 0
    for (var i = 0; i < encoded.length; i++) {
      if (encoded.charAt(i) === "%")
        i += 2
      bytes++
    }
    return bytes <= 48
  }
  function createGroup(title) {
    var name = String(title || "").trim()
    if (!root.groupTitleOk(name))
      return false
    return sendOp({ op: "group.create", title: name })
  }
  function nextGroupInviteRequest() {
    root.groupInviteRequestSequence = root.groupInviteRequestSequence + 1
    return "gi-" + Date.now().toString(36) + "-" +
      root.groupInviteRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }
  function inviteToGroup(friendId, expectedKey, groupId, requestId) {
    var group = String(groupId || root.lastGroup || "")
    var friend = String(friendId || root.lastDirectId || "")
    var friendKey = String(expectedKey || "")
    var request = String(requestId || "")
    var selected = root.groupById(group)
    var matches = false
    for (var i = 0; i < root.friends.length; i++)
      if (String(root.friends[i].id || "") === friend &&
          String(root.friends[i].key || "") === friendKey) {
        matches = true
        break
      }
    if (!selected || !matches ||
        !root.groupInviteCandidateMatches(group, friend, friendKey) ||
        !/^[0-9a-f]{64}$/.test(friendKey) ||
        !/^gi-[a-z0-9-]{8,70}$/.test(request) ||
        Number(selected.memberCount || 0) >= Number(selected.limit || 10))
      return false
    root.lastGroup = group
    root.lastDirectId = friend
    return sendOp({ op: "invite.create", kind: "group", group: group,
      role: "member", id: friend, key: friendKey, request: request,
      ttlSec: 86400 })
  }
  function dissolveGroup(groupId) {
    var group = String(groupId || root.lastGroup || "")
    if (!group)
      return false
    return sendOp({ op: "group.dissolve", group: group })
  }
  function leaveGroup(groupId) {
    var group = String(groupId || root.lastGroup || "")
    if (!group)
      return false
    return sendOp({ op: "group.leave", group: group })
  }
  function setGroupMemberRole(groupId, memberKey, role) {
    var group = String(groupId || root.lastGroup || "")
    var key = String(memberKey || "")
    if (!group || !/^[0-9a-f]{64}$/.test(key) ||
        (role !== "admin" && role !== "member"))
      return false
    return sendOp({ op: "group.member.setRole", group: group, member: key, role: role })
  }
  function removeGroupMember(groupId, memberKey) {
    var group = String(groupId || root.lastGroup || "")
    var key = String(memberKey || "")
    if (!group || !/^[0-9a-f]{64}$/.test(key))
      return false
    return sendOp({ op: "group.member.remove", group: group, member: key })
  }
  function openCard() {
    return root.setSurface(root.lastConversation, "", 40, 80, true,
      root.lastDirectKey, 420, 420)
  }
  function surfaceOperation(conv, mon, x, y, pinned, width, height) {
    var operation = { op: "surface.set", conversation: String(conv || ""),
      monitor: mon || "", x: Math.round(Number(x || 0)), y: Math.round(Number(y || 0)),
      pinned: !!pinned }
    if (root.awaitingHelperInstance || root.activeHelperProtocol >= 14) {
      operation.width = Math.max(200,
        Math.min(4096, Math.round(Number(width || 420))))
      operation.height = Math.max(160,
        Math.min(4096, Math.round(Number(height || 420))))
    }
    return operation
  }
  function setSurface(conv, mon, x, y, pinned, expectedKey, width, height) {
    return root.sendConversationOp(root.surfaceOperation(conv, mon, x, y,
      pinned, width, height), expectedKey, false)
  }
  function exportIdentity(path, request) {
    if (!root.supportsIdentityActions)
      return false
    var o = { op: "identity.export", id: String(request || "") }
    if (path)
      o.path = String(path)
    return sendImmediateOp(o)
  }
  function inspectIdentity(path, passphrase, request) {
    if (!root.supportsIdentityActions)
      return false
    var o = { op: "identity.inspect", path: path, id: String(request || "") }
    if (passphrase)
      o.passphrase = String(passphrase)
    return sendImmediateOp(o)
  }
  function importIdentity(path, replace, passphrase, request) {
    if (!root.supportsIdentityActions)
      return false
    var o = { op: "identity.import", path: path, id: String(request || "") }
    if (replace)
      o.replace = true
    if (passphrase)
      o.passphrase = String(passphrase)
    return sendImmediateOp(o)
  }
  function requestChatSearch(q, conversation, expectedKey, request) {
    var target = String(conversation || "")
    var requestId = String(request || "")
    if (target === "" || requestId === "")
      return false
    return root.sendConversationOp({ op: "search", conversation: target,
      text: String(q || ""), limit: 20, id: requestId },
      String(expectedKey || ""), false)
  }
  function unlockIdentity(pass, request) {
    return sendImmediateOp({ op: "identity.unlock", passphrase: pass,
      id: String(request || "") })
  }
  function protectIdentity(pass, request) {
    if (!root.supportsIdentityActions)
      return false
    return sendImmediateOp({ op: "identity.protect", passphrase: pass,
      id: String(request || "") })
  }
  function unprotectIdentity(pass, request) {
    if (!root.supportsIdentityActions)
      return false
    return sendImmediateOp({ op: "identity.unprotect", passphrase: pass,
      id: String(request || "") })
  }
  function createAttachmentStage(request) {
    var requestId = String(request || "")
    if (!requestId || !root.supportsAttachments ||
        root.helperCompatibility === "incompatible")
      return false
    return sendImmediateOp({ op: "attachment.stage.create", id: requestId })
  }
  function commitAttachmentStage(path, request) {
    var attachmentPath = String(path || "")
    var requestId = String(request || "")
    if (!attachmentPath || !requestId || !root.supportsAttachments ||
        root.helperCompatibility === "incompatible")
      return false
    return sendImmediateOp({ op: "attachment.stage.commit", path: attachmentPath,
      id: requestId })
  }
  function rememberAttachmentCleanup(path, request) {
    var requestId = String(request || "")
    var attachmentPath = String(path || "")
    if (!requestId)
      return
    var next = []
    var found = false
    for (var i = 0; i < root.attachmentCleanupDebts.length; i++) {
      var debt = root.attachmentCleanupDebts[i]
      if (String(debt.request || "") === requestId) {
        next.push({ request: requestId, path: attachmentPath || String(debt.path || "") })
        found = true
      } else {
        next.push(debt)
      }
    }
    if (!found)
      next.push({ request: requestId, path: attachmentPath })
    root.attachmentCleanupDebts = next
  }
  function retryAttachmentCleanupDebts() {
    if (root.attachmentCleanupDebts.length === 0 || !root.supportsAttachments ||
        root.helperCompatibility !== "compatible" || root.awaitingHelperInstance)
      return
    for (var i = 0; i < root.attachmentCleanupDebts.length; i++) {
      var debt = root.attachmentCleanupDebts[i]
      sendImmediateOp({ op: "attachment.stage.discard",
        path: String(debt.path || ""), id: String(debt.request || "") })
    }
  }
  function discardAttachmentStage(path, request) {
    var attachmentPath = String(path || "")
    var requestId = String(request || "")
    if (!requestId)
      return false
    root.rememberAttachmentCleanup(attachmentPath, requestId)
    if (!root.supportsAttachments || root.helperCompatibility === "incompatible")
      return false
    return sendImmediateOp({ op: "attachment.stage.discard", path: attachmentPath,
      id: requestId })
  }
  function inspectAttachment(path, request) {
    var attachmentPath = String(path || "")
    var requestId = String(request || "")
    if (!attachmentPath || !requestId || !root.supportsAttachments ||
        root.helperCompatibility === "incompatible")
      return false
    return sendImmediateOp({ op: "attachment.inspect", path: attachmentPath,
      id: requestId })
  }
  function sendFile(path, conv, attachmentKind, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    var attachmentPath = String(path || "")
    var kind = String(attachmentKind || "file")
    var bindingKey = String(expectedKey || "")
    var group = /^g:[0-9a-f]{64}$/.test(c)
    var failure = ""
    if (!c || !attachmentPath || (c.charAt(0) === "g" && !group) ||
        (kind !== "file" && kind !== "image"))
      failure = "invalid_request"
    else if (!group && !root.directBindingMatches(c, bindingKey))
      failure = "identity_changed"
    else if ((group && !root.supportsGroupAttachments) ||
             (kind === "image" && !root.supportsAttachments))
      failure = "helper_update_required"
    else if (root.awaitingHelperInstance || root.helperCompatibility !== "compatible")
      failure = "helper_handshake_pending"
    else if (root.outgoingFile(c).pending)
      failure = "busy"
    if (failure !== "") {
      root.lastError = failure
      root.lastErrorConv = c
      return false
    }
    if (group)
      bindingKey = ""
    root.fileRequestSequence = root.fileRequestSequence + 1
    var requestId = Date.now().toString(36) + "-" + root.fileRequestSequence.toString(36) +
      "-" + Math.floor(Math.random() * 0x100000000).toString(36)
    root.dismissFileNotice(c)
    root.lastFileConv = c
    root.lastFileState = "sending"
    root.lastFileDir = "out"
    root.lastFileError = ""
    root.lastFilePath = attachmentPath
    root.setOutgoingFile(c, { id: "", path: attachmentPath, request: requestId,
      key: bindingKey, pending: true, cancelRequested: false })
    root.lastFileTick = root.lastFileTick + 1
    var operation = { op: "file.send", conversation: c, path: attachmentPath,
      kind: kind, id: requestId }
    var accepted = root.sendConversationOp(operation, bindingKey, kind === "image")
    if (!accepted) {
      root.setOutgoingFile(c, {})
      root.lastFileState = "failed"
      root.lastFileError = root.lastError || "helper_unavailable"
      root.lastFileTick = root.lastFileTick + 1
      return false
    }
    return true
  }
  function cancelOutgoingFile(conv, expectedKey) {
    var c = String(conv || root.lastFileConv || "")
    var transfer = root.outgoingFile(c)
    var group = /^g:[0-9a-f]{64}$/.test(c)
    if ((!group && (!root.directBindingMatches(c, expectedKey) ||
                    String(transfer.key || "") !== String(expectedKey || ""))) ||
        (group && (!root.supportsGroupAttachments ||
                   String(transfer.key || "") !== "")) ||
        !transfer.pending || transfer.cancelRequested)
      return false
    root.setOutgoingFile(c, {
      id: transfer.id || "",
      path: transfer.path || "",
      request: transfer.request || "",
      key: transfer.key || "",
      pending: true,
      cancelRequested: true
    })
    root.lastFileConv = c
    root.lastFileState = "canceling"
    root.lastFileDir = "out"
    root.lastFilePath = ""
    root.lastFileTick = root.lastFileTick + 1
    if (transfer.id)
      root.sendConversationOp({ op: "file.cancel", conversation: c,
        id: transfer.id }, group ? "" : expectedKey, false)
    return true
  }
  function acceptFile(conv, expectedKey) {
    var c = String(conv || root.lastFileConv || "")
    var offer = fileOffer(c)
    var group = /^g:[0-9a-f]{64}$/.test(c)
    if ((!group && (!root.directBindingMatches(c, expectedKey) ||
                    String(offer.key || "") !== String(expectedKey || ""))) ||
        (group && (!root.supportsGroupAttachments ||
                   String(offer.key || "") !== "")) || !offer.id)
      return false
    if (!root.sendConversationOp({ op: "file.accept", conversation: c,
          id: offer.id }, group ? "" : expectedKey, false))
      return false
    setFileOffer(c, { id: offer.id, name: offer.name || "", path: offer.path || "",
      key: offer.key || "", kind: offer.kind || "file",
      sender: offer.sender || "", pending: false, active: true })
    if (c === root.lastFileConv)
      root.pendingFile = false
    return true
  }
  function cancelFile(conv, expectedKey) {
    var c = String(conv || root.lastFileConv || "")
    var offer = fileOffer(c)
    var group = /^g:[0-9a-f]{64}$/.test(c)
    if ((!group && (!root.directBindingMatches(c, expectedKey) ||
                    String(offer.key || "") !== String(expectedKey || ""))) ||
        (group && (!root.supportsGroupAttachments ||
                   String(offer.key || "") !== "")) || !offer.id)
      return false
    if (!root.sendConversationOp({ op: "file.cancel", conversation: c,
          id: offer.id }, group ? "" : expectedKey, false))
      return false
    setFileOffer(c, { id: offer.id, name: offer.name || "", path: "",
      key: offer.key || "", kind: offer.kind || "file",
      sender: offer.sender || "", pending: false, active: true })
    if (c === root.lastFileConv)
      root.pendingFile = false
    return true
  }
  function startCall(conv, expectedKey) {
    var c = String(conv || root.lastConversation || "")
    if (!c || c.charAt(0) === "g")
      return false
    root.callToneSuppressed = false
    return root.sendConversationOp({ op: "call.start", conversation: c },
      expectedKey, false)
  }
  function answerCall(conv, expectedKey) {
    var c = String(conv || root.lastCallConv || root.lastDirectId || "")
    if (!c || c.charAt(0) === "g")
      return false
    if (!root.sendConversationOp({ op: "call.answer", conversation: c },
          expectedKey, false))
      return false
    root.callToneSuppressed = true
    return true
  }
  function stopCall(conv, expectedKey) {
    var c = String(conv || root.lastCallConv || root.lastDirectId || "")
    if (!c || c.charAt(0) === "g")
      return false
    if (!root.sendConversationOp({ op: "call.stop", conversation: c },
          expectedKey, false))
      return false
    root.callToneSuppressed = true
    return true
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

  function resetStateForIdentity(expectedImport) {
    root.failActiveOutgoingFiles("identity_replaced")
    root.failInFlightMessages("delivery_unknown")
    root.failQueuedMessages("identity_changed")
    root.failQueuedGroupInvites("identity_changed")
    root.pendingOps = []
    root.pendingHandshakeEvents = []
    root.pendingHandshakeBytes = 0
    root.handshakeEventOverflow = false
    root.pendingDirectEvents = []
    root.pendingDirectEventBytes = 0
    root.pendingDirectEventOverflow = false
    root.pendingCallSnapshot = null
    root.pendingCallSnapshotSet = false
    root.friendsReady = false
    root.fileOffers = ({})
    root.outgoingFiles = ({})
    root.fileNotices = ({})
    root.fileNoticeTick = root.fileNoticeTick + 1
    root.pendingFile = false
    root.pendingHistoryUnread = ({})
    root.historyRetryTickByConversation = ({})
    root.historyRequestByConversation = ({})
    root.historyKeyByConversation = ({})
    root.lastHistoryItems = []
    root.lastHistoryConv = ""
    root.lastHistoryFailedConv = ""
    root.lastHistoryFailedCode = ""
    root.lastHistoryUnreadConv = ""
    root.lastHistoryUnreadCount = 0
    root.lastNicknameRequest = ""
    root.lastChatText = ""
    root.lastChatId = ""
    root.lastChatReply = ""
    root.lastChatDir = ""
    root.lastChatKind = ""
    root.lastChatSender = ""
    root.lastChatTimestamp = 0
    root.lastChatRequest = ""
    root.lastChatKey = ""
    root.lastChatConv = ""
    root.lastAttachmentInspectionRequest = ""
    root.lastAttachmentInspectionPath = ""
    root.lastAttachmentInspectionAccepted = false
    root.lastAttachmentStageRequest = ""
    root.lastAttachmentStagePath = ""
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
    root.lastReceiptActor = ""
    root.lastReceiptSentConv = ""
    root.lastReceiptSentId = ""
    root.lastReceiptSentState = ""
    root.lastReceiptFailedConv = ""
    root.lastReceiptFailedId = ""
    root.lastReceiptFailedState = ""
    root.lastReceiptFailedCode = ""
    root.peerTyping = false
    root.peerTypingByConv = ({})
    root.groupTypingByConv = ({})
    root.unreadByConversation = ({})
    root.unreadClearPendingByConversation = ({})
    root.unreadClearRetryAfter = ({})
    root.unreadWarning = ""
    root.unreadCount = 0
    root.selfAvatar = ""
    root.lastConversation = ""
    root.lastDirectId = ""
    root.lastDirectKey = ""
    root.selectedConversation = ""
    root.selectedDirectId = ""
    root.selectedDirectKey = ""
    root.safetyRequest = ""
    root.lastAddr = ""
    root.lastGroup = ""
    root.groups = []
    root.groupsReady = false
    root.lastGroupInviteSentGroup = ""
    root.lastGroupInviteSentFriend = ""
    root.lastGroupInviteSentRequest = ""
    root.lastGroupInviteFailedGroup = ""
    root.lastGroupInviteFailedFriend = ""
    root.lastGroupInviteFailedRequest = ""
    root.lastGroupInviteFailedCode = ""
    root.groupInviteRequestSequence = 0
    root.lastRemovedGroup = ""
    root.clearPendingGroupProjection()
    root.expectedGroupRequest = ""
    root.lastGroupGeneration = 0
    root.groupRefreshAttempts = 0
    root.groupProjectionFailed = false
    groupProjectionTimer.stop()
    root.groupsTick = root.groupsTick + 1
    root.pending = false
    root.pendingGroup = false
    root.inviteUrl = ""
    root.inviteExpiresAt = 0
    root.qrPath = ""
    root.safetyCode = ""
    root.safetyConv = ""
    root.directReinviteRequired = false
    root.identityRecoveryDegraded = false
    root.identityPrimaryUncertain = false
    root.lastIdentityPrimaryRequest = ""
    root.lastDirectReinviteRequest = ""
    root.incomingCall = false
    root.everOnline = false
    root.recoveringHelper = false
    root.selfOnline = false
    root.connectionState = "starting"
    root.lastCallState = ""
    root.lastCallConv = ""
    root.lastCallKey = ""
    root.callToneSuppressed = true
    root.callDurationSeconds = 0
    root.friends = []
    root.friendsTick = root.friendsTick + 1
    root.pendingFriendGeneration = ""
    root.pendingFriendBuild = []
    root.surfaces = []
    root.surfacesTick = root.surfacesTick + 1
    root.lastAutoOpenItems = []
    root.lastAutoOpenDirectDefault = true
    root.lastAutoOpenRequest = ""
    root.lastAutoOpenCode = ""
    root.lastAutoOpenSucceeded = false
    root.persistentWarning = ""
    root.lastError = expectedImport ? "" : "identity_changed"
    root.lastErrorRequest = ""
    root.lastErrorConv = ""
    if (!expectedImport)
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
        next[key] = { id: transfer.id || "", path: transfer.path || "",
          request: transfer.request || "", key: transfer.key || "", pending: false,
          cancelRequested: false }
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
    root.failActiveIncomingFiles()
    root.failInFlightMessages("delivery_unknown")
    root.failQueuedGroupInvites("helper_restarted")
    root.reconnectGeneration = root.reconnectGeneration + 1
    root.helperCompatibility = "unknown"
    root.legacyHandshakeAttempts = 0
    root.legacySnapshotSeen = false
    root.awaitingHelperInstance = false
    root.helperStatusNonce = ""
    root.pendingDirectEvents = []
    root.pendingDirectEventBytes = 0
    root.pendingDirectEventOverflow = false
    root.pendingCallSnapshot = null
    root.pendingCallSnapshotSet = false
    root.friendsReady = false
    root.groupsReady = false
    root.expectedGroupRequest = ""
    root.clearPendingGroupProjection()
    groupProjectionTimer.stop()
    root.recoveringHelper = true
    root.connectionState = "reconnecting"
    root.selfOnline = false
    root.applyCallSnapshot(null)
    root.inviteUrl = ""
    root.inviteExpiresAt = 0
    root.qrPath = ""
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

  function launchHelperDetached() {
    root.attached = true
    root.procReady = false
    proc.startDetached()
    helperAttachTimer.interval = 120
    helperAttachTimer.restart()
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
    id: groupProjectionTimer
    interval: 5000
    repeat: false
    onTriggered: {
      root.clearPendingGroupProjection()
      root.groupsReady = false
      if (root.groupRefreshAttempts >= 5) {
        root.groupProjectionFailed = true
        return
      }
      if (root.awaitingHelperInstance || root.helperCompatibility !== "compatible") {
        interval = 1000
        restart()
        return
      }
      root.refreshGroups(false)
    }
  }

  Timer {
    id: helperStatusTimer
    interval: 5000
    repeat: false
    onTriggered: root.retryHelperHandshake()
  }

  Timer {
    interval: 1000
    repeat: true
    running: root.lastCallState === "active"
    onTriggered: root.callDurationSeconds = root.callDurationSeconds + 1
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
      if (changed)
        root.peerTypingByConv = next
      var nextGroups = {}
      var groupsChanged = false
      for (var conversation in root.groupTypingByConv) {
        var currentActors = root.groupTypingByConv[conversation] || ({})
        var nextActors = {}
        for (var actor in currentActors) {
          if (Number(currentActors[actor]) > now)
            nextActors[actor] = currentActors[actor]
          else
            groupsChanged = true
        }
        if (Object.keys(nextActors).length > 0)
          nextGroups[conversation] = nextActors
        else if (Object.keys(currentActors).length > 0)
          groupsChanged = true
      }
      if (groupsChanged)
        root.groupTypingByConv = nextGroups
      if (changed || groupsChanged)
        root.typingTick = root.typingTick + 1
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
      root.launchHelperDetached()
    }
  }

  Timer {
    id: helperAttachTimer
    interval: 120
    repeat: false
    onTriggered: root.attachSocket()
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
    running: false
  }

  Component.onCompleted: root.launchHelperDetached()
}
