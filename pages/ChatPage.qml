import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtMultimedia
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons

Item {
  // Keep the live plugin parser cache tied to the current source revision.
  id: root
  property var service: null
  property var theme: ({ bg: "", fg: "", accent: "", unread: "" })
  property string conversation: ""
  property string peerName: ""
  property string peerAvatar: ""
  property int peerAvatarRevision: 0
  property bool peerAvatarFailed: false
  property bool peerOnline: false
  onPeerAvatarRevisionChanged: root.peerAvatarFailed = false
  property color peerNameColor: theme.accent || Color.accent
  property color peerStatusColor: theme.accent || Color.accent
  property bool autoOpenEnabled: true
  property bool clearConfirm: false
  signal autoOpenToggled()
  property bool terminalLook: false
  property bool pulseUnread: false
  property bool showFile: false
  property bool emojiOpen: false
  property bool demo: false
  property string fileStatus: ""
  property int filePickerExitCode: -1
  property bool filePickerStreamDone: false
  property string replyToId: ""
  property string replyToText: ""
  property string editingId: ""
  property string forwardText: ""
  onPeerAvatarChanged: root.peerAvatarFailed = false

  readonly property color fg: theme.fg || Color.foreground
  readonly property color bg: theme.bg || Color.background
  readonly property color accent: theme.accent || Color.accent
  readonly property string fontFamily: terminalLook ? "monospace" : Style.font.family
  readonly property string mediaPath: {
    if (!service)
      return ""
    return String(service.filePathFor(root.conversation) || "")
  }
  readonly property bool imageFile: {
    if (!service)
      return false
    var p = root.mediaPath
    return /\.(png|jpe?g|gif|webp)$/i.test(p)
  }
  readonly property bool audioFile: /\.(mp3|wav|ogg|flac|m4a|aac)$/i.test(root.mediaPath)
  readonly property bool videoFile: /\.(mp4|webm|mkv|mov|avi)$/i.test(root.mediaPath)
  readonly property bool peerTyping: {
    if (root.demo || !service || typeof service.isPeerTyping !== "function")
      return false
    return service.isPeerTyping(root.conversation)
  }
  readonly property bool incoming: {
    if (root.demo)
      return root.demoIncomingCall
    if (!service || !service.incomingCall)
      return false
    return root.sameConv(service.lastCallConv || service.lastConversation)
  }
  readonly property bool inCall: {
    if (root.demo)
      return root.demoInCall
    if (!service || service.incomingCall)
      return false
    if (!root.sameConv(service.lastCallConv || service.lastConversation))
      return false
    var s = service.lastCallState || ""
    return s !== "" && s !== "ended"
  }
  readonly property bool fileForThis: {
    if (root.demo)
      return root.demoIncomingFile
    if (!service)
      return false
    return service.filePending(root.conversation)
  }

  property bool demoIncomingFile: false
  property bool demoIncomingCall: false
  property bool demoInCall: false
  property int demoReplyIndex: 0
  property bool typingSent: false
  property string typingConversation: ""

  readonly property int smilePx: 24
  readonly property int smileTextPx: Style.font.body
  readonly property string filePickerScript:
    "if command -v zenity >/dev/null 2>&1; then\n" +
    "  exec zenity --file-selection --title='Send file'\n" +
    "elif command -v kdialog >/dev/null 2>&1; then\n" +
    "  exec kdialog --getopenfilename \"$HOME\" '*|All files'\n" +
    "elif command -v yad >/dev/null 2>&1; then\n" +
    "  exec yad --file --title='Send file'\n" +
    "fi\n" +
    "exit 2\n"

  readonly property var emojiSet: [
    "😀", "🙂", "😉", "😍", "😂", "😅", "🙌", "👍",
    "👎", "❤️", "🔥", "✨", "🎉", "🙏", "😮", "😢",
    "😡", "🤔", "👀", "✅", "👋", "💯"
  ]

  ListModel {
    id: lines
  }

  component ChatBtn: Button {
    foreground: root.fg
    accent: root.accent
    fontFamily: root.fontFamily
    radius: Style.cornerRadius
    iconSize: Style.font.icon
    fontSize: Style.font.body
    horizontalPadding: Style.space(6)
    verticalPadding: Style.space(4)
  }

  component ContextMenuItem: Controls.MenuItem {
    id: contextItem
    property string materialIcon: ""
    implicitHeight: Style.space(32)
    leftPadding: Style.space(8)
    rightPadding: Style.space(8)
    topPadding: Style.space(4)
    bottomPadding: Style.space(4)

    background: Rectangle {
      radius: Style.cornerRadius
      color: contextItem.highlighted
        ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)
        : "transparent"
      border.color: contextItem.highlighted
        ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.45)
        : "transparent"
      border.width: contextItem.highlighted ? 1 : 0
    }

    contentItem: RowLayout {
      spacing: Style.space(8)

      Text {
        Layout.preferredWidth: Style.font.icon
        horizontalAlignment: Text.AlignHCenter
        text: contextItem.materialIcon
        visible: contextItem.materialIcon !== ""
        color: !contextItem.enabled ? Qt.darker(root.fg, 1.6) :
          (contextItem.highlighted ? root.accent : root.fg)
        font.family: "Material Symbols Rounded"
        font.pixelSize: Style.font.icon
        font.variableAxes: ({ "FILL": 0, "wght": 500 })
        renderType: Text.QtRendering
      }

      Text {
        Layout.fillWidth: true
        text: contextItem.text
        color: !contextItem.enabled ? Qt.darker(root.fg, 1.6) :
          (contextItem.highlighted ? root.accent : root.fg)
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        elide: Text.ElideRight
      }

      Text {
        visible: !!contextItem.subMenu
        text: "chevron_right"
        color: contextItem.highlighted ? root.accent : Qt.darker(root.fg, 1.35)
        font.family: "Material Symbols Rounded"
        font.pixelSize: Style.font.icon
        font.variableAxes: ({ "FILL": 0, "wght": 500 })
        renderType: Text.QtRendering
      }
    }
  }

  component FormatBtn: ChatBtn {
    id: formatButton
    property string materialIcon: ""
    fontFamily: root.fontFamily
    iconText: ""
    text: ""
    horizontalPadding: 0
    verticalPadding: 0
    implicitWidth: Style.space(30)
    implicitHeight: Style.space(30)

    Text {
      anchors.centerIn: parent
      text: formatButton.materialIcon
      color: formatButton.hot || formatButton.selected ? formatButton.accent : formatButton.foreground
      font.family: "Material Symbols Rounded"
      font.pixelSize: Style.font.icon + Style.space(2)
      font.variableAxes: ({ "FILL": 0, "wght": 500 })
      renderType: Text.QtRendering
      font.hintingPreference: Font.PreferNoHinting
    }
  }

  function splitSmiles(t) {
    var s = String(t || "")
    var out = []
    var i = 0
    while (i < s.length) {
      var code = s.charCodeAt(i)
      if (code === 32 || code === 9 || code === 10 || code === 13) {
        i++
        continue
      }
      var matched = ""
      var k
      for (k = 0; k < root.emojiSet.length; k++) {
        var g = root.emojiSet[k]
        if (s.indexOf(g, i) === i && g.length >= matched.length)
          matched = g
      }
      if (!matched)
        return []
      out.push(matched)
      i += matched.length
    }
    return out
  }

  function isSmileOnly(t) {
    var s = String(t || "").replace(/\s+/g, "")
    if (!s)
      return false
    var glyphs = root.splitSmiles(t)
    if (!glyphs.length)
      return false
    return glyphs.join("") === s
  }

  function selectedRange() {
    var start = input.selectionStart
    var end = input.selectionEnd
    if (start < 0 || end < 0 || start > end) {
      start = input.cursorPosition
      end = start
    }
    return { start: start, end: end, text: input.text.slice(start, end) }
  }

  function wrapSelection(before, after, placeholder) {
    var range = root.selectedRange()
    var selected = range.text || placeholder
    input.remove(range.start, range.end)
    input.insert(range.start, before + selected + after)
    input.select(range.start + before.length, range.start + before.length + selected.length)
    input.forceActiveFocus()
  }

  function prefixLine(prefix) {
    var pos = input.cursorPosition
    var start = input.text.lastIndexOf("\n", Math.max(0, pos - 1)) + 1
    if (input.text.slice(start, start + prefix.length) === prefix) {
      input.remove(start, start + prefix.length)
      input.cursorPosition = Math.max(start, pos - prefix.length)
    } else {
      input.insert(start, prefix)
      input.cursorPosition = pos + prefix.length
    }
    input.forceActiveFocus()
  }

  function insertLink() {
    var range = root.selectedRange()
    var label = range.text || "text"
    var value = "[" + label + "](url)"
    input.remove(range.start, range.end)
    input.insert(range.start, value)
    input.select(range.start + 1, range.start + 1 + label.length)
    input.forceActiveFocus()
  }

  function formatCode() {
    var range = root.selectedRange()
    if (range.text.indexOf("\n") !== -1)
      root.wrapSelection("```bash\n", "\n```", "code")
    else
      root.wrapSelection("`", "`", "code")
  }

  function codeToCopy(value) {
    var text = String(value || "")
    var blocks = []
    var match
    var fenced = /```[^\n]*\n([\s\S]*?)```/g
    while ((match = fenced.exec(text)) !== null)
      blocks.push(match[1].replace(/^\n|\n$/g, ""))
    if (blocks.length)
      return blocks.join("\n\n")
    if (text.length >= 2 && text.charAt(0) === "`" && text.charAt(text.length - 1) === "`")
      return text.slice(1, -1)
    return text
  }

  function localFileUrl(path) {
    var parts = String(path || "").split("/")
    var i
    for (i = 0; i < parts.length; i++)
      parts[i] = encodeURIComponent(parts[i])
    return "file://" + parts.join("/")
  }

  function copyText(value) {
    var text = String(value || "")
    if (!text)
      return
    Quickshell.execDetached([
      "bash", "-c",
      "if command -v wl-copy >/dev/null 2>&1; then printf '%s' \"$1\" | wl-copy; elif command -v xclip >/dev/null 2>&1; then printf '%s' \"$1\" | xclip -selection clipboard; fi",
      "omaq-copy-message", text
    ])
  }

  function copyCode(value) {
    var code = root.codeToCopy(value)
    if (!code)
      return
    Quickshell.execDetached([
      "bash", "-c",
      "if command -v wl-copy >/dev/null 2>&1; then printf '%s' \"$1\" | wl-copy; elif command -v xclip >/dev/null 2>&1; then printf '%s' \"$1\" | xclip -selection clipboard; fi",
      "omaq-copy-code", code
    ])
  }

  function escapeMarkup(value) {
    return String(value || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/\"/g, "&quot;")
  }

  function markdownInline(value) {
    var text = root.escapeMarkup(value)
    var protectedParts = []

    function token(index) {
      return String.fromCharCode(1) + index + String.fromCharCode(2)
    }

    function protect(value) {
      var marker = token(protectedParts.length)
      protectedParts.push(value)
      return marker
    }

    text = text.replace(/\[([^\]]+)\]\(((?:https?:\/\/|mailto:)[^)\s"'<>]+)\)/gi,
      function(match, label, href) {
        return protect("<a href='" + href + "'>" + label + "</a>")
      })
    text = text.replace(new RegExp("\\x60([^\\x60\\n]+)\\x60", "g"),
      function(match, code) {
        return protect("<font color='" + String(root.accent) + "'><b>" + code + "</b></font>")
      })
    text = text.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>")
    text = text.replace(/__([^_\n]+)__/g, "<b>$1</b>")
    text = text.replace(/(^|[^*])\*([^*\n]+)\*/g, "$1<i>$2</i>")
    text = text.replace(/(^|[^\w])_([^_\n]+)_/g, "$1<i>$2</i>")

    for (var i = 0; i < protectedParts.length; i++)
      text = text.replace(token(i), protectedParts[i])
    return text
  }

  function markdownText(value) {
    var sourceLines = String(value || "").split("\n")
    var rendered = []
    var accentColor = String(root.accent)
    var fenced = false

    for (var i = 0; i < sourceLines.length; i++) {
      var line = sourceLines[i]
      var match
      if ((match = line.match(new RegExp("^\\x60\\x60\\x60(.*)$")))) {
        if (!fenced) {
          var language = match[1].trim() || "code"
          rendered.push("<font color='" + accentColor + "'><b>" + root.escapeMarkup(language))
          fenced = true
        } else {
          rendered.push("</b></font>")
          fenced = false
        }
        continue
      }
      if (fenced) {
        rendered.push(root.escapeMarkup(line))
        continue
      }
      if ((match = line.match(/^#{1,6}\s+(.+)$/))) {
        rendered.push("<b><font color='" + accentColor + "'>" + root.markdownInline(match[1]) + "</font></b>")
      } else if ((match = line.match(/^>\s?(.*)$/))) {
        rendered.push("│ &gt; " + root.markdownInline(match[1]))
      } else if ((match = line.match(/^[-*+]\s+\[([ xX])\]\s*(.*)$/))) {
        var checked = match[1].toLowerCase() === "x"
        rendered.push((checked ? "☑" : "☐") + " " + root.markdownInline(match[2]))
      } else if ((match = line.match(/^[-*+]\s+(.*)$/))) {
        rendered.push("• " + root.markdownInline(match[1]))
      } else if ((match = line.match(/^(\d+)\.\s+(.*)$/))) {
        rendered.push(match[1] + ". " + root.markdownInline(match[2]))
      } else {
        rendered.push(root.markdownInline(line))
      }
    }
    if (fenced)
      rendered.push("</b></font>")
    return rendered.join("<br/>")
  }

  function messageMarkup(value, replyId, edited) {
    var reply = root.replyTextFor(replyId)
    var main = root.markdownText(value)
    if (edited)
      main += " <font color='" + String(Qt.darker(root.fg, 1.35)) + "'>(edited)</font>"
    if (!reply)
      return main
    var preview = root.escapeMarkup(reply).replace(/\n/g, "<br/>")
    return "<font color='" + String(root.accent) + "'><b>↩ " + preview + "</b></font><br/>" + main
  }

  function smileSrc(glyph) {
    if (!glyph)
      return ""
    var cps = []
    var i = 0
    while (i < glyph.length) {
      var c = glyph.codePointAt(i)
      if (c !== 0xFE0F)
        cps.push(c.toString(16))
      i += c > 0xFFFF ? 2 : 1
    }
    return Qt.resolvedUrl("../assets/emoji/" + cps.join("-") + ".png")
  }

  function replyTextFor(id) {
    var target = String(id || "")
    if (!target)
      return ""
    for (var i = 0; i < lines.count; i++) {
      var item = lines.get(i)
      if (item && String(item.id || "") === target)
        return String(item.text || "")
    }
    return ""
  }

  function beginReply(id, text) {
    root.replyToId = String(id || "")
    root.replyToText = String(text || root.replyTextFor(id) || "")
    input.forceActiveFocus()
  }

  function clearReply() {
    root.replyToId = ""
    root.replyToText = ""
  }

  function beginEdit(id, text) {
    if (!id)
      return
    root.editingId = String(id)
    root.clearReply()
    input.text = String(text || "")
    input.forceActiveFocus()
  }

  function clearEdit() {
    root.editingId = ""
    input.text = ""
  }

  function clearChat() {
    if (!root.conversation || root.demo || !root.service)
      return
    root.clearConfirm = false
    root.service.clearHistory(root.conversation)
  }

  function forwardMessage(target, text) {
    var conversation = String(target || "")
    var value = String(text || "")
    if (root.demo || !service || !conversation || !value)
      return
    service.sendOp({ op: "msg.send", conversation: conversation, text: value })
    root.appendLine({ dir: "sys", text: "Forward queued", ack: -1 })
    list.positionViewAtEnd()
  }

  function appendLine(item) {
    var entry = item || {}
    if (entry.ack === undefined)
      entry.ack = -1
    lines.append(entry)
  }

  function sameConv(conv) {
    if (!root.conversation)
      return true
    if (!conv)
      return false
    return String(conv) === String(root.conversation)
  }

  function applyHistory(items, cleared) {
    var keep = []
    var i, j, it, dir, found
    for (i = 0; i < lines.count; i++) {
      var existing = lines.get(i)
      if (!cleared && existing && existing.local)
        keep.push({ id: existing.id || "", reply: existing.reply || "", dir: existing.dir, text: existing.text, deleted: !!existing.deleted, edited: !!existing.edited, local: true, pending: !!existing.pending, ack: existing.ack !== undefined ? existing.ack : -1 })
    }
    lines.clear()
    for (i = 0; items && i < items.length; i++) {
      it = items[i]
      if (!it || (!it.text && !it.deleted))
        continue
      dir = it.dir === "out" ? "out" : (it.dir === "sys" ? "sys" : "in")
      var historyAck = -1
      if (dir === "out")
        historyAck = it.receipt === "read" ? 3 : (it.receipt === "delivered" ? 2 : 1)
      root.appendLine({ id: it.id || "", reply: it.reply || "", dir: dir, text: it.deleted ? "Message deleted" : it.text, deleted: !!it.deleted, edited: !!it.edited, local: false, pending: false, ack: historyAck })
    }
    if (!root.demo && service && String(root.conversation || "").charAt(0) !== "g") {
      for (i = 0; items && i < items.length; i++) {
        it = items[i]
        if (it && it.id && it.dir !== "out" && it.dir !== "sys")
          service.sendReceipt(root.conversation, it.id, "read")
      }
    }
    for (i = 0; i < keep.length; i++) {
      found = false
      for (j = 0; j < lines.count; j++) {
        var historyLine = lines.get(j)
        if (historyLine && ((keep[i].id && historyLine.id === keep[i].id) ||
                    (keep[i].pending && !keep[i].id && historyLine.dir === keep[i].dir && historyLine.text === keep[i].text))) {
          found = true
          break
        }
      }
      if (!found)
        root.appendLine(keep[i])
    }
    list.positionViewAtEnd()
  }

  function bubbleWidth(value, hasCode, withReceipt, availableWidth) {
    var sourceLines = String(value || "").split("\n")
    var longest = 0
    for (var i = 0; i < sourceLines.length; i++)
      longest = Math.max(longest, sourceLines[i].length)
    // Size from the complete logical line, not a single-word minimum. This
    // keeps short three-word messages on one line when the window allows it.
    var estimated = longest * root.smileTextPx * 0.72 + Style.space(24)
    if (withReceipt)
      estimated += Style.space(18)
    var minimum = hasCode ? Style.space(180) : Style.space(52)
    return Math.min(Math.max(minimum, estimated), availableWidth * 0.82)
  }

  function bubbleColor(dir) {
    if (dir === "out")
      return Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.22)
    if (dir === "sys")
      return "transparent"
    return Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.08)
  }

  function resetDemo() {
    lines.clear()
    root.demoIncomingFile = false
    root.demoIncomingCall = false
    root.demoInCall = false
    root.demoReplyIndex = 0
    root.emojiOpen = false
    root.fileStatus = ""
    root.appendLine({ dir: "sys", text: "Local demo. Nothing is sent.", ack: -1 })
    root.appendLine({ dir: "in", text: "Invite-only chat. This window is how a 1:1 looks.", ack: -1 })
    root.appendLine({ dir: "out", text: "Can I type here?", ack: 1 })
    root.appendLine({ dir: "in", text: "Yes. Type below. Paperclip attaches, the formatting buttons appear while you type, and the handset is a call. Long wrap should stay inside the bubble.", ack: -1 })
    root.appendLine({ dir: "in", text: "Safety-looking sample\n8A2F 91C0 44BE 110D", ack: -1 })
    root.appendLine({ dir: "in", text: "# Markdown preview\n**bold** and *italic* with `code`\n> quoted text\n- unordered item\n1. numbered item\n- [ ] task item", ack: -1 })
    list.positionViewAtEnd()
  }

  function pushLive() {
    if (root.demo || !service)
      return
    if (!root.sameConv(service.lastChatConv || service.lastConversation))
      return
    var t = service.lastChatText || ""
    if (!t)
      return
    var dir = service.lastChatDir === "out" ? "out" : "in"
    var i
    for (i = lines.count - 1; i >= 0; i--) {
      var pending = lines.get(i)
      if (pending && pending.local && pending.text === t && pending.dir === dir) {
        lines.setProperty(i, "pending", false)
        if (service.lastChatId)
          lines.setProperty(i, "id", service.lastChatId)
        lines.setProperty(i, "reply", service.lastChatReply || "")
        if (dir === "out")
          lines.setProperty(i, "ack", 1)
        list.positionViewAtEnd()
        return
      }
    }
    root.appendLine({ id: service.lastChatId || "", reply: service.lastChatReply || "", dir: dir, text: t, deleted: false, edited: false, local: dir === "out", pending: false, ack: dir === "out" ? 1 : -1 })
    if (dir === "in" && service.lastChatId) {
      service.sendReceipt(root.conversation, service.lastChatId, "read")
      service.clearUnread(root.conversation)
    }
    list.positionViewAtEnd()
  }

  function failPending() {
    if (!service || !service.lastErrorConv || !root.sameConv(service.lastErrorConv))
      return
    var code = String(service.lastError || "error")
    var message = code === "ratchet_pending"
      ? "Secure session is being established. Send again in a moment."
      : code === "no_ratchet"
        ? "Secure session unavailable. Re-pair this contact with a fresh invite."
        : "Message failed: " + code
    for (var i = lines.count - 1; i >= 0; i--) {
      var item = lines.get(i)
      if (item && item.local && item.pending) {
        lines.remove(i)
        root.appendLine({ dir: "sys", text: message, ack: -1 })
        list.positionViewAtEnd()
        return
      }
    }
  }

  function stopTyping() {
    typingStop.stop()
    if (!root.typingSent)
      return
    var conv = root.typingConversation || root.conversation
    root.typingSent = false
    root.typingConversation = ""
    if (!root.demo && service)
      service.setTyping(conv, false)
  }

  function updateTyping() {
    if (root.demo || !service)
      return
    if (!input.text) {
      root.stopTyping()
      return
    }
    if (!root.typingSent) {
      root.typingSent = true
      root.typingConversation = String(root.conversation || service.lastConversation || "")
      service.setTyping(root.typingConversation, true)
    }
    typingStop.restart()
  }

  function send() {
    var t = input.text
    if (!t)
      return
    if (!root.demo && !service)
      return
    root.stopTyping()
    if (root.editingId) {
      var editId = root.editingId
      var editText = t
      root.editingId = ""
      input.text = ""
      if (!root.demo && service)
        service.editMessage(root.conversation, editId, editText)
      return
    }
    input.text = ""
    root.emojiOpen = false
    var replyId = root.replyToId
    root.clearReply()
    root.appendLine({ id: "", reply: replyId, dir: "out", text: t, deleted: false, edited: false, local: true, pending: !root.demo, ack: root.demo ? 1 : 0 })
    list.positionViewAtEnd()
    if (root.demo) {
      demoReply.restart()
      return
    }
    if (!service)
      return
    service.sendOp({ op: "msg.send", conversation: root.conversation || service.lastConversation, text: t, reply: replyId })
  }

  function startCall() {
    if (root.demo) {
      root.demoIncomingCall = false
      root.demoInCall = true
      root.appendLine({ dir: "sys", text: "Call started (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    if (service)
      service.startCall(root.conversation)
  }

  function answerCall() {
    if (root.demo) {
      root.demoIncomingCall = false
      root.demoInCall = true
      root.appendLine({ dir: "sys", text: "Call answered (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    if (service)
      service.answerCall(root.conversation)
  }

  function hangUp() {
    if (root.demo) {
      root.demoIncomingCall = false
      root.demoInCall = false
      root.appendLine({ dir: "sys", text: "Call ended (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    if (service)
      service.stopCall(root.conversation)
  }

  function attachFile() {
    if (root.demo) {
      root.demoIncomingFile = true
      root.appendLine({ dir: "sys", text: "File offer: notes.png (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    root.showFile = true
    root.fileStatus = ""
    root.openFilePicker()
  }

  function finishFilePicker() {
    if (root.filePickerExitCode < 0 || !root.filePickerStreamDone)
      return
    if (root.filePickerExitCode === 0) {
      var picked = String(filePickerOutput.text || "").trim()
      if (picked !== "") {
        filePath.text = picked
        root.fileStatus = ""
      }
    } else if (root.filePickerExitCode === 2) {
      root.fileStatus = "No file picker found — enter a path manually"
    }
    root.filePickerExitCode = -1
    root.filePickerStreamDone = false
  }

  function openFilePicker() {
    if (!filePicker.running) {
      root.filePickerExitCode = -1
      root.filePickerStreamDone = false
      filePicker.running = true
    }
  }

  function insertEmoji(glyph) {
    input.insert(input.cursorPosition, glyph)
    input.forceActiveFocus()
    root.emojiOpen = false
  }

  function sendSelectedFile() {
    var path = String(filePath.text || "").trim()
    if (!path) {
      root.fileStatus = "Choose a file first"
      return
    }
    if (!service || !root.conversation) {
      root.fileStatus = "No conversation selected"
      return
    }
    root.fileStatus = "Sending…"
    service.sendFile(path, root.conversation)
  }

  Timer {
    id: typingStop
    interval: 3500
    repeat: false
    onTriggered: root.stopTyping()
  }

  Process {
    id: filePicker
    command: ["bash", "-c", root.filePickerScript, "omaq-file-picker"]
    running: false
    stdout: StdioCollector {
      id: filePickerOutput
      waitForEnd: true
      onStreamFinished: {
        root.filePickerStreamDone = true
        root.finishFilePicker()
      }
    }
    onExited: function(code) {
      root.filePickerExitCode = code
      root.finishFilePicker()
    }
  }

  Timer {
    id: demoReply
    interval: 650
    onTriggered: {
      var pool = [
        "Still demo — that line never left this machine.",
        "The composer should stay a single row.",
        "Hang up only appears during a call.",
        "Long incoming reply to check wrap and scroll: the list should pin to the latest line without covering the composer."
      ]
      var i = root.demoReplyIndex % pool.length
      root.demoReplyIndex += 1
      root.appendLine({ dir: "in", text: pool[i], ack: -1 })
      list.positionViewAtEnd()
    }
  }

  Controls.Menu {
    id: composerMenu
    padding: Style.space(4)
    delegate: ContextMenuItem {}

    background: Rectangle {
      radius: Style.cornerRadius
      color: Qt.darker(root.bg, 1.08)
      border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
      border.width: 1
    }

    ContextMenuItem {
      text: "Cut"
      materialIcon: "content_cut"
      enabled: input.selectedText !== ""
      onTriggered: input.cut()
    }
    ContextMenuItem {
      text: "Copy"
      materialIcon: "content_copy"
      enabled: input.selectedText !== ""
      onTriggered: input.copy()
    }
    ContextMenuItem {
      text: "Paste"
      materialIcon: "content_paste"
      onTriggered: input.paste()
    }
    ContextMenuItem {
      text: "Select all"
      materialIcon: "select_all"
      enabled: input.text !== ""
      onTriggered: input.selectAll()
    }
  }

  Connections {
    target: root.service
    enabled: !root.demo && root.service !== null
    function onMessageTickChanged() { root.pushLive() }
    function onUpdateTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastUpdateConv) || !root.service.lastUpdateId)
        return
      for (var i = 0; i < lines.count; i++) {
        var updated = lines.get(i)
        if (updated && updated.id === root.service.lastUpdateId) {
          lines.setProperty(i, "deleted", root.service.lastUpdateDeleted)
          lines.setProperty(i, "edited", root.service.lastUpdateEdited)
          lines.setProperty(i, "text", root.service.lastUpdateDeleted ? "Message deleted" : root.service.lastUpdateText)
          break
        }
      }
    }
    function onReceiptTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastReceiptConv) || !root.service.lastReceiptId)
        return
      for (var i = lines.count - 1; i >= 0; i--) {
        var receiptLine = lines.get(i)
        if (receiptLine && receiptLine.id === root.service.lastReceiptId) {
          lines.setProperty(i, "ack", root.service.lastReceiptState === "read" ? 3 : 2)
          break
        }
      }
    }
    function onHistoryTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastHistoryConv))
        return
      root.applyHistory(root.service.lastHistoryItems, root.service.lastHistoryCleared)
    }
    function onLastErrorTickChanged() {
      root.failPending()
      if (root.service && root.sameConv(root.service.lastFileConv) && root.service.lastFileState === "failed")
        root.fileStatus = "File could not be sent: " + root.service.lastFileError
    }
    function onLastFileTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastFileConv))
        return
      if (root.service.lastFileState === "done")
        root.fileStatus = "File sent"
      else if (root.service.lastFileState === "failed")
        root.fileStatus = "File could not be sent: " + (root.service.lastFileError || "file_failed")
    }
  }

  onConversationChanged: {
    root.stopTyping()
    root.clearConfirm = false
    if (!root.demo && root.service && root.conversation) {
      lines.clear()
      root.service.requestHistory(root.conversation)
    }
  }

  Component.onCompleted: {
    if (root.demo)
      root.resetDemo()
    else if (service)
      service.requestHistory(root.conversation || service.lastConversation)
  }

  Rectangle {
    anchors.fill: parent
    color: root.bg
    radius: Style.cornerRadius
    clip: true
    border.color: root.pulseUnread ? (root.theme.unread || Color.accent) : Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.12)
    border.width: root.pulseUnread ? 2 : 1

    ColumnLayout {
      anchors.fill: parent
      anchors.margins: Style.space(10)
      spacing: Style.space(6)

      RowLayout {
        Layout.fillWidth: true
        spacing: Style.space(4)

        Item {
          visible: !root.demo
          Layout.preferredWidth: Style.font.display
          Layout.preferredHeight: Style.font.display

          Image {
            anchors.fill: parent
            visible: root.peerAvatar !== "" && !root.peerAvatarFailed
            source: root.peerAvatar !== "" ? root.localFileUrl(root.peerAvatar) + "?v=" + root.peerAvatarRevision : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            smooth: true
            onStatusChanged: if (status === Image.Error)
              root.peerAvatarFailed = true
          }

          Text {
            anchors.centerIn: parent
            visible: root.peerAvatar === "" || root.peerAvatarFailed
            text: "person"
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.72)
            font.family: "Material Symbols Rounded"
            font.pixelSize: Math.round(Style.font.display * 0.64)
            font.variableAxes: ({ "FILL": 0, "wght": 500 })
            renderType: Text.QtRendering
            font.hintingPreference: Font.PreferNoHinting
          }
        }

        Text {
          visible: root.demo
          Layout.fillWidth: true
          text: "DEMO"
          color: root.peerNameColor
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
          font.letterSpacing: 1.2
          elide: Text.ElideRight
        }

        Text {
          visible: !root.demo
          Layout.fillWidth: true
          text: root.peerName || root.conversation || "chat"
          color: root.peerNameColor
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
          font.letterSpacing: 1.2
          elide: Text.ElideRight
        }

        Text {
          visible: !root.demo
          text: root.peerTyping ? "typing…" : (root.peerOnline ? "online" : "offline")
          color: root.peerStatusColor
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
          elide: Text.ElideRight
        }

        FormatBtn {
          visible: !root.demo && String(root.conversation || "").charAt(0) !== "g"
          materialIcon: root.autoOpenEnabled ? "notifications" : "notifications_off"
          tooltipText: root.autoOpenEnabled
            ? "Open automatically on new messages"
            : "Badge and sound only"
          selected: !root.autoOpenEnabled
          onClicked: root.autoOpenToggled()
        }

        Text {
          visible: root.clearConfirm
          text: "Clear this chat?"
          color: root.accent
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
        }

        FormatBtn {
          visible: !root.demo && root.clearConfirm
          materialIcon: "close"
          tooltipText: "Cancel"
          onClicked: root.clearConfirm = false
        }

        FormatBtn {
          visible: !root.demo && root.clearConfirm
          materialIcon: "check"
          tooltipText: "Clear this chat"
          selected: true
          onClicked: root.clearChat()
        }

        FormatBtn {
          visible: !root.demo && !root.clearConfirm
          materialIcon: "delete_sweep"
          tooltipText: "Clear messages in this chat"
          onClicked: root.clearConfirm = true
        }
      }

      ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: Style.space(6)
        boundsBehavior: Flickable.StopAtBounds
        model: lines
        onHeightChanged: Qt.callLater(function() {
          if (list.count)
            list.positionViewAtEnd()
        })

        delegate: Item {
          id: line
          width: list.width
          height: Math.max(bubble.implicitHeight, sysLine.implicitHeight)
          readonly property bool smileOnly: model.dir !== "sys" && root.isSmileOnly(model.text)
          readonly property bool hasCode: model.dir !== "sys" && (String(model.text || "").indexOf("```") !== -1 || new RegExp("\\x60[^\\x60\\n]+\\x60").test(String(model.text || "")))
          readonly property var smileGlyphs: line.smileOnly ? root.splitSmiles(model.text) : []
          readonly property string contextText: String(model.text || "")
          readonly property string contextId: String(model.id || "")
          readonly property bool deleted: !!model.deleted
          readonly property bool edited: !!model.edited

          Rectangle {
            id: bubble
            anchors.left: model.dir === "out" ? undefined : parent.left
            anchors.right: model.dir === "out" ? parent.right : undefined
            width: line.smileOnly ? Math.min(smileRow.implicitWidth + Style.space(16), parent.width * 0.82) : root.bubbleWidth(model.text, line.hasCode, model.dir === "out" && model.ack !== undefined, parent.width)
            implicitHeight: Math.max(line.smileOnly ? smileRow.implicitHeight : label.implicitHeight, line.hasCode ? Math.max(codeFooter.implicitHeight, Style.space(30)) : 0) + Style.space(12)
            radius: Style.cornerRadius
            color: root.bubbleColor(model.dir)
            visible: model.dir !== "sys"

            Text {
              id: label
              visible: !line.smileOnly
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              anchors.leftMargin: Style.space(8)
              anchors.rightMargin: line.hasCode ? Style.space(60) : (model.dir === "out" && model.ack !== undefined ? Style.space(28) : Style.space(8))
              text: !line.smileOnly && model.dir !== "sys" ? root.messageMarkup(model.text, model.reply, line.edited) : ""
              textFormat: Text.RichText
              linkColor: root.accent
              color: root.fg
              font.family: root.fontFamily
              font.pixelSize: root.smileTextPx
              font.hintingPreference: Font.PreferNoHinting
              renderType: Text.QtRendering
              wrapMode: Text.Wrap
              onLinkActivated: Qt.openUrlExternally(link)
            }

            Text {
              visible: model.dir === "out" && model.ack !== undefined && !line.hasCode
              anchors.right: parent.right
              anchors.bottom: parent.bottom
              anchors.rightMargin: Style.space(8)
              anchors.bottomMargin: Style.space(4)
              text: model.ack >= 3 ? "✓✓" : (model.ack >= 2 ? "✓✓" : (model.ack >= 1 ? "✓" : "·"))
              color: model.ack >= 3 ? root.accent : Qt.darker(root.fg, 1.25)
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
            }

            FormatBtn {
              visible: line.hasCode && !line.smileOnly && !(model.dir === "out" && model.ack !== undefined)
              anchors.top: parent.top
              anchors.right: parent.right
              anchors.topMargin: Style.space(4)
              anchors.rightMargin: Style.space(4)
              z: 2
              materialIcon: "content_copy"
              tooltipText: "Copy code"
              onClicked: root.copyCode(model.text)
            }

            Row {
              id: codeFooter
              visible: line.hasCode && !line.smileOnly && model.dir === "out" && model.ack !== undefined
              anchors.right: parent.right
              anchors.bottom: parent.bottom
              anchors.rightMargin: Style.space(4)
              anchors.bottomMargin: Style.space(4)
              spacing: Style.space(8)
              z: 2

              FormatBtn {
                materialIcon: "content_copy"
                tooltipText: "Copy code"
                onClicked: root.copyCode(model.text)
              }

              Text {
                anchors.verticalCenter: parent.verticalCenter
                text: model.ack >= 3 ? "✓✓" : (model.ack >= 2 ? "✓✓" : (model.ack >= 1 ? "✓" : "·"))
                color: model.ack >= 3 ? root.accent : Qt.darker(root.fg, 1.25)
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
              }
            }

            MouseArea {
              id: contextArea
              anchors.fill: parent
              acceptedButtons: Qt.RightButton
              z: 1
              onClicked: messageMenu.popup()
            }

            Controls.Menu {
              id: messageMenu
              padding: Style.space(4)
              delegate: ContextMenuItem {}

              background: Rectangle {
                radius: Style.cornerRadius
                color: Qt.darker(root.bg, 1.08)
                border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                border.width: 1
              }

              ContextMenuItem {
                text: "Copy"
                materialIcon: "content_copy"
                onTriggered: root.copyText(line.contextText)
              }
              ContextMenuItem {
                text: "Reply"
                materialIcon: "reply"
                enabled: !!line.contextId && !line.deleted
                onTriggered: root.beginReply(line.contextId, line.contextText)
              }
              ContextMenuItem {
                text: "Edit"
                materialIcon: "edit"
                visible: model.dir === "out" && !!line.contextId && !line.deleted
                onTriggered: root.beginEdit(line.contextId, line.contextText)
              }
              ContextMenuItem {
                text: "Delete"
                materialIcon: "delete"
                visible: model.dir === "out" && !!line.contextId && !line.deleted
                onTriggered: {
                  if (root.service)
                    root.service.deleteMessage(root.conversation, line.contextId)
                }
              }
              Controls.Menu {
                title: "Forward"
                enabled: root.service && root.service.friends && root.service.friends.length > 0
                padding: Style.space(4)
                delegate: ContextMenuItem {
                  materialIcon: "send"
                }
                background: Rectangle {
                  radius: Style.cornerRadius
                  color: Qt.darker(root.bg, 1.08)
                  border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                  border.width: 1
                }
                Repeater {
                  model: root.service ? root.service.friends : []
                  delegate: ContextMenuItem {
                    required property var modelData
                    materialIcon: "person"
                    text: modelData && modelData.name ? String(modelData.name) : ("Friend " + String(modelData ? modelData.id : ""))
                    onTriggered: {
                      if (modelData)
                        root.forwardMessage(modelData.id, line.contextText)
                    }
                  }
                }
              }
            }

            Row {
              id: smileRow
              visible: line.smileOnly
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
              anchors.leftMargin: Style.space(8)
              spacing: Style.space(2)

              Repeater {
                model: line.smileGlyphs
                Image {
                  required property string modelData
                  width: root.smileTextPx
                  height: root.smileTextPx
                  source: root.smileSrc(modelData)
                  fillMode: Image.PreserveAspectFit
                  sourceSize.width: 64
                  sourceSize.height: 64
                  smooth: true
                  mipmap: true
                  asynchronous: true
                  cache: true
                }
              }
            }
          }

          Text {
            id: sysLine
            visible: model.dir === "sys"
            width: parent.width
            text: model.dir === "sys" ? model.text : ""
            color: Qt.darker(root.fg, 1.5)
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            wrapMode: Text.Wrap
          }
        }
      }

      Row {
        visible: root.fileForThis
        spacing: Style.space(8)
        ChatBtn {
          text: "Accept file"
          bordered: true
          onClicked: {
            if (root.demo) {
              root.demoIncomingFile = false
              root.appendLine({ dir: "sys", text: "Accepted notes.png (demo)", ack: -1 })
              list.positionViewAtEnd()
            } else if (service) {
              service.acceptFile(root.conversation)
            }
          }
        }
        ChatBtn {
          text: "Decline"
          onClicked: {
            if (root.demo) {
              root.demoIncomingFile = false
              root.appendLine({ dir: "sys", text: "Declined file (demo)", ack: -1 })
              list.positionViewAtEnd()
            } else if (service) {
              service.cancelFile(root.conversation)
            }
          }
        }
      }

      Text {
        visible: !root.demo && service && service.fileNameFor(root.conversation) !== ""
        Layout.fillWidth: true
        text: {
          if (!service)
            return ""
          return service.filePathFor(root.conversation) || service.fileNameFor(root.conversation) || ""
        }
        color: Qt.darker(root.fg, 1.5)
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.WrapAnywhere
      }

          Image {
        visible: !root.demo && root.imageFile
        Layout.preferredWidth: Math.min(root.width, 160)
        Layout.preferredHeight: visible ? 80 : 0
        fillMode: Image.PreserveAspectFit
        source: root.mediaPath ? root.localFileUrl(root.mediaPath) : ""
      }

      MediaPlayer {
        id: mediaPlayer
        source: root.mediaPath ? root.localFileUrl(root.mediaPath) : ""
        audioOutput: AudioOutput {
          volume: 1.0
        }
        videoOutput: videoOutput
        onSourceChanged: stop()
      }

      Row {
        visible: !root.demo && root.audioFile
        spacing: Style.space(8)
        ChatBtn {
          text: mediaPlayer.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"
          bordered: true
          onClicked: mediaPlayer.playbackState === MediaPlayer.PlayingState ? mediaPlayer.pause() : mediaPlayer.play()
        }
        Text {
          anchors.verticalCenter: parent.verticalCenter
          text: root.mediaPath
          color: Qt.darker(root.fg, 1.5)
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideMiddle
          width: Math.max(0, root.width - Style.space(90))
        }
      }

      VideoOutput {
        id: videoOutput
        visible: !root.demo && root.videoFile
        Layout.preferredWidth: Math.min(root.width, 280)
        Layout.preferredHeight: visible ? 158 : 0
        fillMode: VideoOutput.PreserveAspectFit
      }

      RowLayout {
        visible: !root.demo && root.showFile
        Layout.fillWidth: true
        spacing: Style.space(8)
        TextField {
          id: filePath
          Layout.fillWidth: true
          foreground: root.fg
          accent: root.accent
          placeholderText: "Absolute file path"
          onTextChanged: root.fileStatus = ""
          onAccepted: root.sendSelectedFile()
        }
        ChatBtn {
          id: chooseFileBtn
          text: "Choose"
          onClicked: root.openFilePicker()
        }
        ChatBtn {
          id: sendFileBtn
          text: "Send file"
          bordered: true
          onClicked: root.sendSelectedFile()
        }
      }

      Text {
        visible: !root.demo && root.fileStatus !== ""
        Layout.fillWidth: true
        text: root.fileStatus
        color: root.fileStatus === "Sending…" ? root.accent : Qt.darker(root.fg, 1.35)
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.Wrap
      }

      Column {
        id: composerCol
        readonly property real composerHeight: replyBar.height + emojiFlow.height + formatFlow.height + composerRow.implicitHeight + spacing * 3
        Layout.fillWidth: true
        Layout.preferredHeight: composerHeight
        spacing: Style.space(4)
        z: 2

        Row {
          id: replyBar
          x: input.x
          width: input.width
          visible: root.replyToId !== "" || root.editingId !== ""
          spacing: Style.space(6)
          height: visible ? Math.max(replyPreview.implicitHeight, clearReplyBtn.implicitHeight) : 0

          Text {
            id: replyPreview
            width: parent.width - clearReplyBtn.implicitWidth - parent.spacing
            text: root.editingId !== "" ? "✎ Editing message" : ("↩ " + (root.replyToText || root.replyToId))
            color: root.accent
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
          }

          ChatBtn {
            id: clearReplyBtn
            text: "×"
            tooltipText: "Cancel reply"
            onClicked: root.editingId !== "" ? root.clearEdit() : root.clearReply()
          }
        }

        Flow {
          id: emojiFlow
          x: input.x
          width: input.width
          height: visible ? implicitHeight : 0
          spacing: Style.space(3)
          visible: root.emojiOpen

          Repeater {
            model: root.emojiSet
            ChatBtn {
              required property string modelData
              text: modelData
              tooltipText: modelData
              onClicked: root.insertEmoji(modelData)
            }
          }
        }

        Flow {
          id: formatFlow
          x: input.x
          width: input.width
          height: visible ? implicitHeight : 0
          spacing: Style.space(3)
          visible: input.text.length > 0

          FormatBtn {
            materialIcon: "format_h1"
            tooltipText: "Heading"
            onClicked: root.prefixLine("# ")
          }
          FormatBtn {
            materialIcon: "format_bold"
            tooltipText: "Bold"
            onClicked: root.wrapSelection("**", "**", "bold")
          }
          FormatBtn {
            materialIcon: "format_italic"
            tooltipText: "Italic"
            onClicked: root.wrapSelection("*", "*", "italic")
          }
          FormatBtn {
            materialIcon: "format_quote"
            tooltipText: "Quote"
            onClicked: root.prefixLine("> ")
          }
          FormatBtn {
            materialIcon: "code"
            tooltipText: "Code"
            onClicked: root.formatCode()
          }
          FormatBtn {
            materialIcon: "link"
            tooltipText: "Link"
            onClicked: root.insertLink()
          }
          FormatBtn {
            materialIcon: "format_list_bulleted"
            tooltipText: "Unordered list"
            onClicked: root.prefixLine("- ")
          }
          FormatBtn {
            materialIcon: "format_list_numbered"
            tooltipText: "Numbered list"
            onClicked: root.prefixLine("1. ")
          }
          FormatBtn {
            materialIcon: "checklist"
            tooltipText: "Task list"
            onClicked: root.prefixLine("- [ ] ")
          }
        }

        RowLayout {
          id: composerRow
          width: parent.width
          implicitHeight: input.height
          spacing: Style.space(4)

            ChatBtn {
              iconText: "󰁦"
              tooltipText: "File"
              selected: root.showFile
              onClicked: root.attachFile()
            }

            Item {
              id: inputBox
              Layout.fillWidth: true
              Layout.minimumHeight: Style.space(30)
              Layout.preferredHeight: Math.min(Style.space(84), Math.max(Style.space(30), input.contentHeight + Style.space(12)))

              Controls.TextArea {
                id: input
                anchors.fill: parent
                color: root.fg
                selectionColor: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.35)
                selectedTextColor: root.fg
                placeholderTextColor: Qt.darker(root.fg, 1.6)
                font.family: root.fontFamily
                font.pixelSize: root.smileTextPx
                font.hintingPreference: Font.PreferNoHinting
                wrapMode: TextEdit.Wrap
                placeholderText: root.demo ? "Demo message" : "Message (Ctrl+Enter to send)"
                onTextChanged: root.updateTyping()
                persistentSelection: true
                background: BorderSurface {
                  color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.06)
                  borderSpec: Border.controlSpec(input.activeFocus ? "focus" : "normal", root.fg, root.accent)
                  radius: Style.cornerRadius
                }
                Keys.onPressed: function(event) {
                  if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                      (event.modifiers & Qt.ControlModifier)) {
                    root.send()
                    event.accepted = true
                  }
                }
              }

              MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                z: 10
                onPressed: {
                  input.forceActiveFocus()
                  composerMenu.popup()
                }
              }
            }

            FormatBtn {
              materialIcon: "mood"
              tooltipText: "Emoji"
              selected: root.emojiOpen
              onClicked: root.emojiOpen = !root.emojiOpen
            }

            ChatBtn {
              iconText: "󰒊"
              tooltipText: "Send"
              bordered: true
              onClicked: root.send()
            }
        }
      }
    }
  }
}
