import QtQuick
import Quickshell
import Quickshell.Io

Item {
  id: root
  property var settings: ({})
  property bool muted: false
  property int unreadCount: 0
  property var unreadByConversation: ({})
  property string statusText: "OmaQ"
  property string lastError: ""
  property string lastErrorConv: ""
  property int lastErrorTick: 0
  property bool attached: false
  property bool procReady: false
  property var pendingOps: []
  property int backoffMs: 200
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
  property string lastUpdateConv: ""
  property string lastUpdateId: ""
  property string lastUpdateText: ""
  property bool lastUpdateDeleted: false
  property bool lastUpdateEdited: false
  property int updateTick: 0
  property string lastChatConv: ""
  property int messageTick: 0
  property var lastHistoryItems: []
  property bool lastHistoryCleared: false
  property string lastHistoryConv: ""
  property int lastHistoryUnreadCount: 0
  property string lastHistoryUnreadConv: ""
  property int historyTick: 0
  property var pendingHistoryUnread: ({})
  property bool peerTyping: false
  property string lastTypingConv: ""
  property string lastReceiptConv: ""
  property string lastReceiptId: ""
  property string lastReceiptState: ""
  property int receiptTick: 0
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
  property string lastFileError: ""
  property int lastFileTick: 0
  property bool pendingFile: false
  property var fileOffers: ({})
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
  property bool selfOnline: false

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
    try { ev = JSON.parse(line) } catch (e) { return }
    if (ev.event === "snapshot") {
      if (ev.unread !== undefined)
        root.unreadCount = root.localUnreadTotal() > 0 ? root.localUnreadTotal() : ev.unread
      if (ev.addr)
        root.lastAddr = ev.addr
      if (ev.locked !== undefined)
        root.locked = !!ev.locked
      if (ev.protected !== undefined)
        root.saveProtected = !!ev.protected
      if (ev.locked === true)
        root.lastError = "locked"
      else if (root.lastError !== "helper_down")
        root.lastError = ""
      if (ev.online !== undefined)
        root.selfOnline = !!ev.online
      if (ev.nickname !== undefined)
        root.selfNickname = String(ev.nickname || "")
    }
    if (ev.event === "nickname") {
      root.selfNickname = String(ev.value || "")
      if (root.lastError !== "helper_down")
        root.lastError = ""
      root.nicknameTick = root.nicknameTick + 1
    }
    if (ev.event === "error") {
      root.lastError = ev.code || "error"
      root.lastErrorConv = ev.conversation || ""
      root.lastErrorTick = root.lastErrorTick + 1
      if (root.lastFileState === "sending" &&
          (!ev.conversation || String(ev.conversation) === String(root.lastFileConv))) {
        root.lastFileState = "failed"
        root.lastFileError = ev.code || "file_failed"
        root.lastFileTick = root.lastFileTick + 1
      }
      if (ev.code === "locked")
        root.locked = true
    }
    if (ev.event === "friends") {
      if (!root.locked && root.lastError !== "helper_down")
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
      if (ev.op === "unlock") {
        root.locked = false
        root.sendOp({ op: "status" })
      }
      if (ev.protected !== undefined)
        root.saveProtected = !!ev.protected
    }
    if (ev.event === "message") {
      root.lastChatId = String(ev.id || "")
      root.lastChatReply = String(ev.reply || "")
      root.lastChatDir = ev.dir === "out" ? "out" : "in"
      if (root.lastChatDir !== "out") {
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
    if (ev.event === "history") {
      var historyConv = String(ev.conversation || "")
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
    if (ev.event === "file.done") {
      root.lastFileState = "done"
      root.lastFileError = ""
      var doneConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var doneOld = root.fileOffer(doneConv)
      root.setFileOffer(doneConv, { id: ev.id || doneOld.id || "", name: doneOld.name || root.lastFileName, path: ev.path || "", pending: false })
      root.pendingFile = false
      root.lastFilePath = ev.path || ""
      root.lastConversation = doneConv
      root.lastFileConv = doneConv
      root.lastFileTick = root.lastFileTick + 1
    }
    if (ev.event === "file.failed") {
      root.lastFileState = "failed"
      root.lastFileError = ev.code || "file_failed"
      var failedConv = String(ev.conversation || root.lastFileConv || root.lastConversation)
      var failedOld = root.fileOffer(failedConv)
      root.setFileOffer(failedConv, { id: failedOld.id || ev.id || "", name: failedOld.name || "", path: "", pending: false })
      root.pendingFile = false
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

  function flushOps() {
    if (!root.pendingOps.length)
      return
    if (sock.connected) {
      var queued = root.pendingOps
      root.pendingOps = []
      for (var i = 0; i < queued.length; i++)
        sock.write(queued[i])
    } else if (root.procReady) {
      var pending = root.pendingOps
      root.pendingOps = []
      for (var j = 0; j < pending.length; j++)
        proc.write(pending[j])
    }
  }

  function sendOp(obj) {
    var line = JSON.stringify(obj) + "\n"
    if (sock.connected) {
      sock.write(line)
      return
    }
    if (root.procReady) {
      proc.write(line)
      return
    }
    var next = root.pendingOps.slice()
    next.push(line)
    root.pendingOps = next
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
  function toggleMute() {
    root.muted = !root.muted
  }

  function unreadFor(conv) {
    var key = String(conv || "")
    return Number(root.unreadByConversation[key] || 0)
  }

  function clearUnread(conv) {
    var key = String(conv || "")
    if (!key || !root.unreadByConversation[key])
      return
    var next = {}
    var current
    for (current in root.unreadByConversation) {
      if (current !== key)
        next[current] = root.unreadByConversation[current]
    }
    root.unreadCount = Math.max(0, root.unreadCount - Number(root.unreadByConversation[key] || 0))
    root.unreadByConversation = next
  }

  function requestHistory(conv) {
    var c = String(conv || root.lastConversation || "")
    if ((root.pendingHistoryUnread[c] || []).length > 0)
      return
    var next = {}
    var key
    var queue = (root.pendingHistoryUnread[c] || []).slice()
    for (key in root.pendingHistoryUnread)
      next[key] = root.pendingHistoryUnread[key]
    queue.push(root.unreadFor(c))
    next[c] = queue
    root.pendingHistoryUnread = next
    root.clearUnread(c)
    sendOp({ op: "history", conversation: c, limit: 50 })
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
  function importIdentity(path, replace) {
    var o = { op: "identity.import", path: path }
    if (replace)
      o.replace = true
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
    root.lastFileConv = c
    root.lastFileState = "sending"
    root.lastFileError = ""
    root.lastFilePath = String(path || "")
    root.lastFileTick = root.lastFileTick + 1
    sendOp({ op: "file.send", conversation: c, path: path })
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

  function scheduleRestart() {
    root.lastError = "helper_down"
    root.pendingHistoryUnread = ({})
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
        root.flushOps()
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
      "OMAQ_STATE": root.stateDir
    })
    running: true
    stdinEnabled: true
    stdout: SplitParser {
      onRead: function(line) { root.handleLine(line) }
    }
    onStarted: {
      root.procReady = true
      root.resetBackoff()
      root.flushOps()
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
