import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtMultimedia
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons

FocusScope {
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
  onPeerOnlineChanged: if (root.peerOnline) root.retryReadReceipts()
  onPeerAvatarRevisionChanged: root.peerAvatarFailed = false
  property color peerNameColor: theme.accent || Color.accent
  property color peerStatusColor: theme.accent || Color.accent
  property color receiptSentColor: theme.accent || Color.accent
  property color receiptDeliveredColor: theme.fg || Color.foreground
  property color receiptReadColor: theme.accent || Color.accent
  property bool autoOpenEnabled: true
  property bool readActive: false
  property bool clearConfirm: false
  property bool groupMembersOpen: false
  property string groupActionConfirm: ""
  property string groupActionMemberKey: ""
  property string groupActionName: ""
  signal autoOpenToggled()
  property bool terminalLook: false
  property bool pulseUnread: false
  property bool showFile: false
  property bool followLatest: true
  onShowFileChanged: root.restoreLatestPosition()
  onFileStatusChanged: root.restoreLatestPosition()
  onFileStatusPathChanged: root.restoreLatestPosition()
  property bool emojiOpen: false
  property bool formatToolbarEnabled: false
  signal formatToolbarToggled(bool enabled)
  property bool demo: false
  property string fileStatus: ""
  property string fileStatusPath: ""
  property string reactionStatus: ""
  property bool filePickerClosing: false
  property int filePickerExitCode: -1
  property bool filePickerStreamDone: false
  property string replyToId: ""
  property string replyToText: ""
  property string editingId: ""
  property string deleteConfirmId: ""
  property string activeAudioPath: ""
  property string audioErrorPath: ""
  property string audioError: ""
  property var readReceiptIds: []
  property var pendingReceiptIds: []
  property var failedReceiptIds: []
  property int localMessageSequence: 0
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
  readonly property bool mediaPathInHistory: root.hasFileMessage(root.mediaPath)
  readonly property bool groupConversation: String(root.conversation || "").charAt(0) === "g"
  readonly property var groupMembers: {
    if (!root.groupConversation || !root.service ||
        typeof root.service.groupMembers !== "function")
      return []
    var revision = Number(root.service.groupsTick || 0)
    return revision >= 0 ? root.service.groupMembers(root.conversation) : []
  }
  readonly property int groupPeerCount: {
    var count = 0
    for (var i = 0; i < root.groupMembers.length; i++)
      if (!root.groupMembers[i].self)
        count++
    return count
  }
  readonly property int groupOnlineCount: {
    var count = 0
    for (var i = 0; i < root.groupMembers.length; i++)
      if (!root.groupMembers[i].self && root.groupMembers[i].online)
        count++
    return count
  }
  readonly property string peerConnectionStatus: {
    if (service && service.connectionState && service.connectionState !== "online")
      return service.connectionState === "reconnecting" ? "reconnecting…" : "connecting…"
    if (root.groupConversation)
      return root.groupOnlineCount + "/" + root.groupPeerCount + " online"
    if (root.peerTyping)
      return "typing…"
    return root.peerOnline ? "online" : "offline"
  }
  readonly property bool peerTyping: {
    if (root.demo || !service || typeof service.isPeerTyping !== "function")
      return false
    return service.isPeerTyping(root.conversation)
  }
  readonly property bool directConversation: root.demo || !root.groupConversation
  readonly property bool incoming: {
    if (!root.directConversation)
      return false
    if (root.demo)
      return root.demoIncomingCall
    if (!service || !service.incomingCall)
      return false
    return root.sameConv(service.lastCallConv || service.lastConversation)
  }
  readonly property bool inCall: {
    if (!root.directConversation)
      return false
    if (root.demo)
      return root.demoInCall
    if (!service || service.incomingCall)
      return false
    if (!root.sameConv(service.lastCallConv || service.lastConversation))
      return false
    var s = service.lastCallState || ""
    return s !== "" && s !== "ended"
  }
  readonly property bool callActive: root.directConversation &&
    (root.demo ? root.demoInCall :
      (!!root.service && root.service.lastCallState === "active" &&
       root.sameConv(root.service.lastCallConv || root.service.lastConversation)))
  readonly property int callDurationSeconds: root.demo
    ? root.demoCallDurationSeconds :
      (root.service ? Number(root.service.callDurationSeconds || 0) : 0)
  readonly property string callDurationText: root.formatCallDuration(root.callDurationSeconds)
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
  property int demoCallDurationSeconds: 0
  property int demoReplyIndex: 0
  property bool typingSent: false
  property string typingConversation: ""

  readonly property int smilePx: Math.max(56, Style.font.display * 2)
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

  MediaPlayer {
    id: mediaPlayer
    source: root.activeAudioPath ? root.localFileUrl(root.activeAudioPath) : ""
    audioOutput: AudioOutput { volume: 1.0 }
    onErrorOccurred: function(error, errorString) {
      root.audioErrorPath = root.activeAudioPath
      root.audioError = errorString || "Audio playback failed"
    }
  }

  component OmaqTooltip: Controls.ToolTip {
    id: omaqTooltip
    delay: 450
    timeout: 2500
    padding: Style.space(5)

    background: Rectangle {
      radius: Style.cornerRadius
      color: Qt.darker(root.bg, 1.08)
      border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.24)
      border.width: 1
    }

    contentItem: Text {
      text: omaqTooltip.text
      color: root.fg
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
      renderType: Text.QtRendering
    }
  }

  component ChatBtn: Button {
    id: chatButton
    property string helpText: ""
    property bool suppressHelp: false
    tooltipText: ""
    Accessible.role: Accessible.Button
    Accessible.name: chatButton.helpText !== "" ? chatButton.helpText : chatButton.text
    Accessible.onPressAction: chatButton.clicked()
    foreground: root.fg
    accent: root.accent
    fontFamily: root.fontFamily
    radius: Style.cornerRadius
    iconSize: Style.font.icon
    fontSize: Style.font.body
    horizontalPadding: Style.space(6)
    verticalPadding: Style.space(4)
    focusable: true

    OmaqTooltip {
      visible: !chatButton.suppressHelp &&
        (chatButton.hot || chatButton.activeFocus) && chatButton.helpText !== ""
      text: chatButton.helpText
    }
  }

  component ContextMenuItem: Controls.MenuItem {
    id: contextItem
    property string materialIcon: ""
    implicitWidth: Style.space(220)
    implicitHeight: visible ? Style.space(32) : 0
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

  component ReactionAction: Item {
    id: reactionAction
    property string emoji: ""
    property string materialIcon: ""
    property string tooltipText: ""
    property bool selected: false
    property bool compact: false
    signal clicked()

    implicitWidth: compact ? Style.space(18) : Style.space(24)
    implicitHeight: compact ? Style.space(20) : Style.space(24)
    activeFocusOnTab: visible
    Accessible.role: Accessible.Button
    Accessible.name: reactionAction.tooltipText
    Accessible.onPressAction: reactionAction.clicked()
    Keys.onReturnPressed: reactionAction.clicked()
    Keys.onEnterPressed: reactionAction.clicked()
    Keys.onSpacePressed: reactionAction.clicked()

    Text {
      anchors.centerIn: parent
      text: reactionAction.emoji !== "" ? reactionAction.emoji : reactionAction.materialIcon
      color: reactionAction.materialIcon !== ""
        ? (reactionAction.selected || reactionHover.hovered || reactionAction.activeFocus
          ? root.accent : root.fg)
        : root.fg
      font.family: reactionAction.emoji !== "" ? "Noto Color Emoji" : "Material Symbols Rounded"
      font.pixelSize: reactionAction.emoji !== ""
        ? (reactionAction.compact ? Style.font.bodySmall : Style.font.body)
        : (reactionAction.compact ? Style.font.body : Style.font.icon)
      font.variableAxes: reactionAction.materialIcon !== "" ? ({ "FILL": 0, "wght": 500 }) : ({})
      renderType: Text.QtRendering
      opacity: reactionAction.selected ? 1.0 : (reactionHover.hovered || reactionAction.activeFocus ? 0.95 : 0.78)
    }

    Rectangle {
      visible: reactionAction.selected || reactionAction.activeFocus
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      height: 1
      color: root.accent
    }

    HoverHandler {
      id: reactionHover
      cursorShape: Qt.PointingHandCursor
    }
    TapHandler { onTapped: reactionAction.clicked() }

    OmaqTooltip {
      visible: reactionHover.hovered && reactionAction.tooltipText !== ""
      text: reactionAction.tooltipText
    }
  }

  component ReceiptMark: Item {
    id: receiptMark
    property int acknowledgement: 0
    property bool failed: false
    property bool uncertain: false
    property string failureCode: ""
    readonly property bool read: !failed && !uncertain && acknowledgement >= 3
    readonly property string label: failed || uncertain
      ? root.messageFailureText(failureCode)
      : (acknowledgement >= 3 ? "Read" :
         (acknowledgement >= 2 ? "Delivered" :
          (acknowledgement >= 1 ? "Sent" : "Sending")))
    readonly property color markColor: failed ? (root.theme.unread || root.accent) :
      (uncertain ? root.receiptDeliveredColor :
       (acknowledgement >= 3 ? root.receiptReadColor :
        (acknowledgement >= 2 ? root.receiptDeliveredColor :
         (acknowledgement >= 1 ? root.receiptSentColor : root.fg))))
    implicitWidth: Math.max(Style.space(18), receiptText.implicitWidth)
    implicitHeight: Math.max(Style.space(18), receiptText.implicitHeight)
    Accessible.role: Accessible.StaticText
    Accessible.name: label

    Text {
      id: receiptText
      anchors.centerIn: parent
      text: receiptMark.failed ? "error" : (receiptMark.uncertain ? "help" :
        (receiptMark.acknowledgement >= 2 ? "✓✓" :
         (receiptMark.acknowledgement >= 1 ? "✓" : "·")))
      color: receiptMark.markColor
      opacity: receiptMark.failed || receiptMark.uncertain ||
        receiptMark.acknowledgement >= 1 ? 1.0 : 0.72
      font.family: receiptMark.failed || receiptMark.uncertain
        ? "Material Symbols Rounded" : root.fontFamily
      font.pixelSize: receiptMark.failed || receiptMark.uncertain
        ? Style.font.body : Style.font.caption
      font.variableAxes: receiptMark.failed || receiptMark.uncertain
        ? ({ "FILL": 0, "wght": 500 }) : ({})
      font.bold: !receiptMark.failed && !receiptMark.uncertain
    }

    HoverHandler { id: receiptHover }
    OmaqTooltip {
      visible: receiptHover.hovered
      text: receiptMark.label
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

  function defaultDownloadDir() {
    var base = Quickshell.env("OMAQ_DOWNLOAD_DIR")
    if (!base || base.charAt(0) !== "/")
      base = Quickshell.env("XDG_DOWNLOAD_DIR")
    if (!base || base.charAt(0) !== "/")
      base = (Quickshell.env("HOME") || "") + "/Downloads"
    return base + "/omaq"
  }

  function isAudioPath(path) {
    return /\.(mp3|wav|ogg|oga|opus|flac|m4a|aac)(?:\.\d+)?$/i.test(String(path || ""))
  }

  function hasFileMessage(path) {
    var target = String(path || "")
    if (!target)
      return false
    for (var i = 0; i < lines.count; i++) {
      var item = lines.get(i)
      if (item && item.dir === "in" && String(item.text || "") === target &&
          (item.kind === "file" || target === root.mediaPath))
        return true
    }
    return false
  }

  function filePathForConversation() {
    if (!service || !conversation)
      return ""
    return String(service.filePathFor(conversation) || "")
  }

  function fileDisplayName(path) {
    var value = String(path || "")
    var slash = value.lastIndexOf("/")
    return slash >= 0 ? value.slice(slash + 1) : value
  }

  function fileFolder(path) {
    var value = String(path || "")
    var slash = value.lastIndexOf("/")
    return slash >= 0 ? value.slice(0, slash) : defaultDownloadDir()
  }

  function openFileFolder(path) {
    var folder = root.fileFolder(path || root.filePathForConversation())
    if (folder)
      Quickshell.execDetached(["xdg-open", folder])
  }

  function copyFilePath(path) {
    var value = String(path || root.filePathForConversation())
    if (!value)
      return
    root.copyText(value)
    root.fileStatus = "Path copied"
    fileStatusTimer.interval = 1800
    fileStatusTimer.restart()
  }

  function audioPlaying(path) {
    return String(path || "") === root.activeAudioPath &&
      mediaPlayer.playbackState === MediaPlayer.PlayingState
  }

  function toggleAudio(path) {
    var value = String(path || "")
    if (!root.isAudioPath(value))
      return
    root.audioError = ""
    root.audioErrorPath = ""
    if (root.activeAudioPath === value) {
      if (mediaPlayer.playbackState === MediaPlayer.PlayingState)
        mediaPlayer.pause()
      else
        mediaPlayer.play()
      return
    }
    mediaPlayer.stop()
    root.activeAudioPath = value
    Qt.callLater(function() {
      if (root.activeAudioPath === value)
        mediaPlayer.play()
    })
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
    root.clearDeleteConfirm()
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
    root.clearDeleteConfirm()
    root.editingId = String(id)
    root.clearReply()
    input.text = String(text || "")
    input.forceActiveFocus()
  }

  function clearEdit() {
    root.editingId = ""
    input.text = ""
  }

  function requestDelete(id) {
    var messageId = String(id || "")
    if (!messageId)
      return
    root.clearReply()
    if (root.editingId)
      root.clearEdit()
    root.deleteConfirmId = messageId
  }

  function clearDeleteConfirm() {
    root.deleteConfirmId = ""
  }

  function confirmDelete() {
    var messageId = root.deleteConfirmId
    root.clearDeleteConfirm()
    if (!root.demo && root.service && messageId)
      root.service.deleteMessage(root.conversation, messageId)
  }

  function clearChat() {
    if (!root.conversation || root.demo || !root.service)
      return
    root.clearConfirm = false
    root.service.clearHistory(root.conversation)
  }

  function newLocalMessageKey() {
    root.localMessageSequence += 1
    return Date.now().toString(36) + "-local-" +
      root.localMessageSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }

  function appendLine(item) {
    var entry = item || {}
    if (entry.ack === undefined)
      entry.ack = -1
    if (entry.sender === undefined)
      entry.sender = ""
    if (entry.newMarker === undefined)
      entry.newMarker = false
    if (entry.live === undefined)
      entry.live = false
    if (entry.kind === undefined)
      entry.kind = ""
    if (entry.reactionMe === undefined)
      entry.reactionMe = ""
    if (entry.reactionPeer === undefined)
      entry.reactionPeer = ""
    if (entry.groupReactions === undefined)
      entry.groupReactions = []
    if (entry.needsReadReceipt === undefined)
      entry.needsReadReceipt = false
    if (entry.failed === undefined)
      entry.failed = false
    if (entry.failureCode === undefined)
      entry.failureCode = ""
    if (entry.clientKey === undefined || entry.clientKey === "")
      entry.clientKey = entry.local ? root.newLocalMessageKey() : ""
    lines.append(entry)
  }

  function resendMessage(clientKey) {
    if (root.demo || !root.service || !clientKey)
      return
    var index = -1
    for (var i = 0; i < lines.count; i++) {
      if (String(lines.get(i).clientKey || "") === String(clientKey)) {
        index = i
        break
      }
    }
    if (index < 0)
      return
    var item = lines.get(index)
    if (!item || item.dir !== "out" || !item.local || !item.failed || !item.text)
      return
    var queued = root.service.sendOp({
      op: "msg.send",
      conversation: root.conversation || root.service.lastConversation,
      text: String(item.text),
      reply: String(item.reply || ""),
      id: String(clientKey)
    })
    if (!queued)
      return
    lines.setProperty(index, "failed", false)
    lines.setProperty(index, "failureCode", "")
    lines.setProperty(index, "pending", true)
    lines.setProperty(index, "ack", 0)
  }

  function reactToMessage(id, currentEmoji, emoji) {
    var messageId = String(id || "")
    if (!messageId || root.demo || !root.service)
      return
    var selectedEmoji = String(emoji || "")
    root.service.reactMessage(root.conversation, messageId,
      String(currentEmoji || "") === selectedEmoji ? "" : selectedEmoji)
  }

  function sameConv(conv) {
    if (!root.conversation)
      return true
    if (!conv)
      return false
    return String(conv) === String(root.conversation)
  }

  function groupMemberName(peerValue) {
    var peer = String(peerValue || "").replace(/^(peer:|member:)/, "")
    for (var i = 0; i < root.groupMembers.length; i++)
      if (String(root.groupMembers[i].peer || "") === peer ||
          String(root.groupMembers[i].key || "") === peer)
        return String(root.groupMembers[i].name || "Member")
    return "Member"
  }

  function groupSelfRole() {
    for (var i = 0; i < root.groupMembers.length; i++)
      if (root.groupMembers[i].self)
        return String(root.groupMembers[i].role || "member")
    return "member"
  }

  function mayManageGroupMember(member) {
    if (!member || member.self || !member.online)
      return false
    var selfRole = root.groupSelfRole()
    var targetRole = String(member.role || "member")
    return selfRole === "owner" ? targetRole !== "owner" :
      (selfRole === "admin" && targetRole === "member")
  }

  function requestGroupMemberAction(action, member) {
    if (!root.groupConversation || !member || !root.mayManageGroupMember(member))
      return
    root.groupActionConfirm = String(action || "")
    root.groupActionMemberKey = String(member.key || "")
    root.groupActionName = String(member.name || "Member")
  }

  function confirmGroupMemberAction() {
    if (!root.service || !root.groupActionConfirm || !root.groupActionMemberKey)
      return
    if (root.groupActionConfirm === "remove")
      root.service.removeGroupMember(root.conversation, root.groupActionMemberKey)
    else
      root.service.setGroupMemberRole(root.conversation, root.groupActionMemberKey,
        root.groupActionConfirm)
    root.groupActionConfirm = ""
    root.groupActionMemberKey = ""
    root.groupActionName = ""
  }

  function clearGroupMemberAction() {
    root.groupActionConfirm = ""
    root.groupActionMemberKey = ""
    root.groupActionName = ""
  }

  function messageAt(index) {
    if (index < 0 || index >= lines.count)
      return null
    var item = lines.get(index)
    return item && item.dir !== "sys" && !item.newMarker ? item : null
  }

  function restoreLatestPosition() {
    if (!root.followLatest)
      return
    Qt.callLater(function() {
      if (root.followLatest && list.count)
        list.positionViewAtEnd()
    })
  }

  function selectMessage(direction, fromEdge) {
    if (!lines.count)
      return
    var index
    if (fromEdge === "start")
      index = 0
    else if (fromEdge === "end")
      index = lines.count - 1
    else
      index = list.currentIndex >= 0 ? list.currentIndex + direction : lines.count - 1
    while (index >= 0 && index < lines.count && !root.messageAt(index))
      index += direction
    if (index < 0 || index >= lines.count)
      return
    list.currentIndex = index
    root.followLatest = index === lines.count - 1
    if (root.followLatest)
      list.positionViewAtEnd()
    else
      list.positionViewAtIndex(index, ListView.Contain)
  }

  function ensureMessageSelection() {
    if (!root.messageAt(list.currentIndex))
      root.selectMessage(-1, "end")
  }

  function markRead() {
    if (root.demo || !root.service || !root.conversation)
      return
    root.service.clearUnread(root.conversation)
    if (!root.peerOnline)
      return
    var pending = root.pendingReceiptIds.slice()
    var changed = false
    for (var i = 0; i < lines.count; i++) {
      var item = lines.get(i)
      var messageId = item ? String(item.id || "") : ""
      if (messageId && item.dir === "in" && item.needsReadReceipt &&
          root.readReceiptIds.indexOf(messageId) < 0 &&
          pending.indexOf(messageId) < 0 && root.failedReceiptIds.indexOf(messageId) < 0) {
        root.service.sendReceipt(root.conversation, messageId, "read")
        pending.push(messageId)
        changed = true
      }
    }
    if (changed)
      root.pendingReceiptIds = pending
  }

  function confirmReadReceipt(id) {
    var messageId = String(id || "")
    if (!messageId)
      return
    var confirmed = root.readReceiptIds.slice()
    var pending = []
    var failed = []
    for (var i = 0; i < root.pendingReceiptIds.length; i++) {
      if (String(root.pendingReceiptIds[i]) !== messageId)
        pending.push(root.pendingReceiptIds[i])
    }
    for (var j = 0; j < root.failedReceiptIds.length; j++) {
      if (String(root.failedReceiptIds[j]) !== messageId)
        failed.push(root.failedReceiptIds[j])
    }
    if (confirmed.indexOf(messageId) < 0)
      confirmed.push(messageId)
    root.pendingReceiptIds = pending
    root.failedReceiptIds = failed
    root.readReceiptIds = confirmed
    for (var lineIndex = 0; lineIndex < lines.count; lineIndex++) {
      var confirmedLine = lines.get(lineIndex)
      if (confirmedLine && String(confirmedLine.id || "") === messageId) {
        lines.setProperty(lineIndex, "needsReadReceipt", false)
        break
      }
    }
  }

  function failReadReceipt(id) {
    var messageId = String(id || "")
    var pendingIndex = root.pendingReceiptIds.indexOf(messageId)
    if (!messageId || pendingIndex < 0)
      return
    var pending = root.pendingReceiptIds.slice()
    var failed = root.failedReceiptIds.slice()
    pending.splice(pendingIndex, 1)
    if (failed.indexOf(messageId) < 0)
      failed.push(messageId)
    root.pendingReceiptIds = pending
    root.failedReceiptIds = failed
  }

  function retryReadReceipts() {
    root.pendingReceiptIds = []
    root.failedReceiptIds = []
    if (root.readActive)
      root.markRead()
  }

  function focusComposer() {
    input.forceActiveFocus()
    root.markRead()
  }

  function handleEscape() {
    if (composerMenu.opened) {
      composerMenu.close()
      return true
    }
    if (root.showFile) {
      root.closeFileChooser()
      return true
    }
    if (root.emojiOpen) {
      root.emojiOpen = false
      return true
    }
    if (root.deleteConfirmId) {
      root.clearDeleteConfirm()
      return true
    }
    if (root.editingId) {
      root.clearEdit()
      return true
    }
    if (root.replyToId) {
      root.clearReply()
      return true
    }
    if (root.clearConfirm) {
      root.clearConfirm = false
      return true
    }
    return false
  }

  function historyGroupReactions(item) {
    var reactions = []
    if (!item)
      return reactions
    var key
    for (key in item) {
      if (key.indexOf("reaction_group_") !== 0 || !item[key])
        continue
      reactions.push({ actor: key.slice("reaction_group_".length), emoji: String(item[key]) })
    }
    return reactions
  }

  function modelCount(model) {
    if (!model)
      return 0
    if (typeof model.count === "number")
      return model.count
    return typeof model.length === "number" ? model.length : 0
  }

  function modelEntry(model, index) {
    if (!model)
      return null
    if (typeof model.get === "function")
      return model.get(index)
    return model[index]
  }

  function groupReactionEmojiList(current) {
    var emojis = []
    var count = root.modelCount(current)
    for (var i = 0; i < count; i++) {
      var reaction = root.modelEntry(current, i)
      if (reaction && reaction.emoji)
        emojis.push(String(reaction.emoji))
    }
    return emojis
  }

  function updatedGroupReactions(current, actor, emoji) {
    var next = []
    var actorKey = String(actor || "")
    var count = root.modelCount(current)
    for (var i = 0; i < count; i++) {
      var reaction = root.modelEntry(current, i)
      if (reaction && String(reaction.actor || "") !== actorKey)
        next.push({ actor: String(reaction.actor || ""), emoji: String(reaction.emoji || "") })
    }
    if (actorKey && emoji)
      next.push({ actor: actorKey, emoji: String(emoji) })
    return next
  }

  function applyHistory(items, cleared) {
    var keep = []
    var unreadCount = 0
    var incomingIndexes = []
    var markerAt = -1
    var unreadIndexes = ({})
    var i, j, it, dir, found
    for (i = 0; i < lines.count; i++) {
      var existing = lines.get(i)
      if (!cleared && existing && (existing.local || existing.live))
        keep.push({ id: existing.id || "", reply: existing.reply || "", sender: existing.sender || "", dir: existing.dir, text: existing.text, kind: existing.kind || "", reactionMe: existing.reactionMe || "", reactionPeer: existing.reactionPeer || "", groupReactions: existing.groupReactions || [], needsReadReceipt: !!existing.needsReadReceipt, deleted: !!existing.deleted, edited: !!existing.edited, local: !!existing.local, live: !!existing.live, pending: !!existing.pending, failed: !!existing.failed, failureCode: existing.failureCode || "", clientKey: existing.clientKey || "", ack: existing.ack !== undefined ? existing.ack : -1 })
    }
    lines.clear()
    if (service && String(service.lastHistoryUnreadConv || "") === String(root.conversation || ""))
      unreadCount = Math.max(0, Number(service.lastHistoryUnreadCount || 0))
    for (i = 0; items && i < items.length; i++) {
      it = items[i]
      if (!it || (!it.text && !it.deleted))
        continue
      dir = it.dir === "out" ? "out" : (it.dir === "sys" ? "sys" : "in")
      if (dir === "in")
        incomingIndexes.push(i)
    }
    if (unreadCount > 0 && incomingIndexes.length > 0) {
      var unreadStart = Math.max(0, incomingIndexes.length - unreadCount)
      markerAt = incomingIndexes[unreadStart]
      for (var unreadIndex = unreadStart; unreadIndex < incomingIndexes.length; unreadIndex++)
        unreadIndexes[incomingIndexes[unreadIndex]] = true
    }
    for (i = 0; items && i < items.length; i++) {
      it = items[i]
      if (!it || (!it.text && !it.deleted))
        continue
      dir = it.dir === "out" ? "out" : (it.dir === "sys" ? "sys" : "in")
      if (i === markerAt)
        root.appendLine({ dir: "sys", text: "New messages", newMarker: true, ack: -1 })
      var historyAck = -1
      if (dir === "out")
        historyAck = it.receipt === "read" ? 3 : (it.receipt === "delivered" ? 2 : 1)
      root.appendLine({ id: it.id || "", reply: it.reply || "", sender: it.from || "", dir: dir, text: it.deleted ? "Message deleted" : it.text, kind: it.kind || "", reactionMe: it.reaction_me || "", reactionPeer: it.reaction_peer || "", groupReactions: root.historyGroupReactions(it), needsReadReceipt: dir === "in" && !!unreadIndexes[i], deleted: !!it.deleted, edited: !!it.edited, local: false, pending: false, ack: historyAck })
    }
    if (root.readActive)
      root.markRead()
    for (i = 0; i < keep.length; i++) {
      found = false
      for (j = 0; j < lines.count; j++) {
        var historyLine = lines.get(j)
        if (historyLine && keep[i].id && historyLine.id === keep[i].id) {
          found = true
          break
        }
      }
      if (!found)
        root.appendLine(keep[i])
    }
    root.restoreLatestPosition()
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
      estimated += Style.space(28)
    var minimum = hasCode ? Style.space(180) : Style.space(52)
    return Math.min(Math.max(minimum, estimated), availableWidth * 0.82)
  }

  function fileBubbleWidth(path, audio, availableWidth) {
    var name = root.fileDisplayName(path)
    var textWidth = Math.max(Style.space(40), name.length * Style.font.bodySmall * 0.62)
    var leadingWidth = audio ? Style.space(30) : Style.font.icon
    var estimated = textWidth + leadingWidth + Style.space(30) + Style.space(24)
    return Math.min(Math.max(Style.space(108), estimated), availableWidth * 0.82)
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
    root.appendLine({ dir: "in", text: "Yes. Type below. Paperclip attaches, the formatting toggle reveals extra tools, and the handset is a call. Long wrap should stay inside the bubble.", ack: -1 })
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
    var request = String(service.lastChatRequest || "")
    var followLatest = root.followLatest
    var i
    var hasNewMarker = false
    for (i = 0; i < lines.count; i++) {
      var existingMarker = lines.get(i)
      if (existingMarker && existingMarker.newMarker) {
        hasNewMarker = true
        break
      }
    }
    for (i = lines.count - 1; i >= 0; i--) {
      var pending = lines.get(i)
      var requestMatches = !!pending && dir === "out" && request !== "" &&
        String(pending.clientKey || "") === request
      if (pending && pending.local && requestMatches) {
        lines.setProperty(i, "pending", false)
        lines.setProperty(i, "failed", false)
        lines.setProperty(i, "failureCode", "")
        if (service.lastChatId)
          lines.setProperty(i, "id", service.lastChatId)
        lines.setProperty(i, "reply", service.lastChatReply || "")
        lines.setProperty(i, "ack", 1)
        return
      }
    }
    if (dir === "in" && !hasNewMarker)
      root.appendLine({ dir: "sys", text: "New messages", newMarker: true, ack: -1 })
    root.appendLine({ id: service.lastChatId || "", reply: service.lastChatReply || "", sender: service.lastChatSender || "", dir: dir, text: t, kind: service.lastChatKind || "", needsReadReceipt: dir === "in", deleted: false, edited: false, local: false, live: true, pending: false, failed: false, failureCode: "", clientKey: dir === "out" ? request : "", ack: dir === "out" ? 1 : -1 })
    if (root.readActive && dir === "in" && service.lastChatId)
      root.markRead()
    if (followLatest)
      root.restoreLatestPosition()
  }

  function messageFailureText(code) {
    var failure = String(code || "error")
    if (failure === "offline")
      return "Contact is offline"
    if (failure === "ratchet_pending")
      return "Secure session is being established"
    if (failure === "no_ratchet")
      return "Secure session unavailable; pair this contact again"
    if (failure === "history_failed")
      return "Message could not be saved or sent"
    if (failure === "busy")
      return "Helper is busy"
    if (failure === "helper_incompatible")
      return "Helper update required"
    if (failure === "locked")
      return "Identity is locked"
    if (failure === "identity_changed")
      return "Identity changed; try again"
    if (failure === "delivery_unknown")
      return "Delivery status unknown; check with the recipient"
    if (failure === "forbidden")
      return "Message was rejected"
    return "Message was not sent"
  }

  function applyMessageFailure(request, code, delivered) {
    var requestKey = String(request || "")
    if (!requestKey)
      return
    var uncertain = String(code || "") === "delivery_unknown"
    for (var i = 0; i < lines.count; i++) {
      var item = lines.get(i)
      if (!item || String(item.clientKey || "") !== requestKey)
        continue
      lines.setProperty(i, "pending", false)
      lines.setProperty(i, "failed", !delivered && !uncertain)
      lines.setProperty(i, "failureCode", delivered ? "" : String(code || "error"))
      lines.setProperty(i, "ack", delivered ? 1 : 0)
      if (delivered) {
        root.appendLine({
          dir: "sys",
          text: "Message sent, but could not be saved locally.",
          live: true,
          ack: -1
        })
        root.restoreLatestPosition()
      }
      return
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
    root.followLatest = true
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
    var clientKey = root.newLocalMessageKey()
    root.appendLine({ id: "", reply: replyId, dir: "out", text: t, deleted: false, edited: false, local: true, pending: !root.demo, failed: false, failureCode: "", clientKey: clientKey, ack: root.demo ? 1 : 0 })
    root.restoreLatestPosition()
    if (root.demo) {
      demoReply.restart()
      return
    }
    if (!service)
      return
    if (!service.sendOp({ op: "msg.send", conversation: root.conversation || service.lastConversation, text: t, reply: replyId, id: clientKey }))
      root.applyMessageFailure(clientKey, service.lastError || "helper_incompatible", false)
  }

  function formatCallDuration(value) {
    var seconds = Math.max(0, Math.floor(Number(value || 0)))
    var minutes = Math.floor(seconds / 60)
    var remainder = seconds % 60
    return minutes.toString() + ":" + (remainder < 10 ? "0" : "") + remainder.toString()
  }

  function startCall() {
    if (!root.directConversation)
      return
    if (root.demo) {
      root.demoCallDurationSeconds = 0
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
    if (!root.directConversation)
      return
    if (root.demo) {
      root.demoCallDurationSeconds = 0
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
    if (!root.directConversation)
      return
    if (root.demo) {
      root.demoIncomingCall = false
      root.demoInCall = false
      root.demoCallDurationSeconds = 0
      root.appendLine({ dir: "sys", text: "Call ended (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    if (service)
      service.stopCall(root.conversation)
  }

  function attachFile() {
    if (root.groupConversation)
      return
    if (root.demo) {
      root.demoIncomingFile = true
      root.appendLine({ dir: "sys", text: "File offer: notes.png (demo)", ack: -1 })
      list.positionViewAtEnd()
      return
    }
    if (root.showFile) {
      root.closeFileChooser()
      return
    }
    root.showFile = true
    if (!root.restoreOutgoingFileStatus()) {
      root.fileStatus = ""
      root.fileStatusPath = ""
    }
    filePath.forceActiveFocus()
  }

  function finishFilePicker() {
    if (root.filePickerClosing) {
      root.filePickerClosing = false
      root.filePickerExitCode = -1
      root.filePickerStreamDone = false
      return
    }
    if (root.filePickerExitCode < 0 || !root.filePickerStreamDone)
      return
    if (root.filePickerExitCode === 0) {
      var picked = String(filePickerOutput.text || "").trim()
      if (picked !== "") {
        filePath.text = picked
        if (!root.restoreOutgoingFileStatus()) {
          root.fileStatus = ""
          root.fileStatusPath = ""
        }
      }
    } else if (root.filePickerExitCode === 2) {
      root.fileStatus = "No file picker found — enter a path manually"
    }
    root.filePickerExitCode = -1
    root.filePickerStreamDone = false
  }

  function openFilePicker() {
    if (!filePicker.running) {
      root.filePickerClosing = false
      root.filePickerExitCode = -1
      root.filePickerStreamDone = false
      filePicker.running = true
    }
  }

  function closeFileChooser() {
    root.filePickerClosing = filePicker.running
    if (filePicker.running)
      filePicker.running = false
    root.showFile = false
    root.filePickerExitCode = -1
    root.filePickerStreamDone = false
    filePath.text = ""
    if (!root.restoreOutgoingFileStatus()) {
      root.fileStatus = ""
      root.fileStatusPath = ""
    }
  }

  function insertEmoji(glyph) {
    input.insert(input.cursorPosition, glyph)
    input.forceActiveFocus()
    root.emojiOpen = false
  }

  function sendSelectedFile() {
    var path = String(filePath.text || "").trim()
    fileStatusTimer.stop()
    if (!path) {
      root.fileStatus = "Choose a file first"
      return
    }
    if (!service || !root.conversation) {
      root.fileStatus = "No conversation selected"
      return
    }
    if (String(root.conversation).charAt(0) === "g") {
      root.fileStatus = "Files are available in direct chats only"
      return
    }
    if (path.charAt(0) !== "/" || path.length >= 512 || path.indexOf("..") !== -1) {
      root.fileStatus = "Enter a valid absolute file path"
      return
    }
    if (service.helperCompatibility === "incompatible") {
      root.fileStatus = "Restart the OmaQ helper before sending files"
      fileStatusTimer.interval = 5000
      fileStatusTimer.restart()
      return
    }
    if (!service.sendFile(path, root.conversation)) {
      root.fileStatus = "A file is already sending"
      fileStatusTimer.interval = 3000
      fileStatusTimer.restart()
      return
    }
    root.closeFileChooser()
    root.fileStatus = "Sending…"
    root.fileStatusPath = path
  }

  function restoreOutgoingFileStatus() {
    if (root.demo || !root.service || !root.service.fileSendingFor(root.conversation))
      return false
    var transfer = root.service.outgoingFile(root.conversation)
    fileStatusTimer.stop()
    root.fileStatus = "Sending…"
    root.fileStatusPath = String(transfer.path || "")
    return true
  }

  function cancelOutgoingFile() {
    if (!root.service || !root.service.cancelOutgoingFile(root.conversation))
      return
    root.closeFileChooser()
    root.fileStatus = ""
  }

  Timer {
    id: typingStop
    interval: 3500
    repeat: false
    onTriggered: root.stopTyping()
  }

  Timer {
    id: fileStatusTimer
    interval: 1800
    repeat: false
    onTriggered: {
      if (root.restoreOutgoingFileStatus())
        return
      root.fileStatus = ""
      root.fileStatusPath = ""
    }
  }

  Timer {
    id: reactionStatusTimer
    interval: 4000
    repeat: false
    onTriggered: root.reactionStatus = ""
  }

  Timer {
    id: unreadClearRetry
    interval: 5100
    repeat: false
    onTriggered: {
      if (root.readActive && root.service && root.service.unreadFor(root.conversation) > 0)
        root.markRead()
    }
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
    interval: 1000
    repeat: true
    running: root.demo && root.demoInCall
    onTriggered: root.demoCallDurationSeconds = root.demoCallDurationSeconds + 1
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
      width: Style.space(220)
      padding: Style.space(4)

      background: Rectangle {
        radius: Style.cornerRadius
        color: Qt.darker(root.bg, 1.08)
        border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
        border.width: 1
      }

      contentItem: Column {
        width: parent.width
        spacing: 0

        ContextMenuItem {
          width: parent.width
          text: "Cut"
          materialIcon: "content_cut"
          enabled: input.selectedText !== ""
          onTriggered: {
            input.cut()
            composerMenu.close()
          }
        }
        ContextMenuItem {
          width: parent.width
          text: "Copy"
          materialIcon: "content_copy"
          enabled: input.selectedText !== ""
          onTriggered: {
            input.copy()
            composerMenu.close()
          }
        }
        ContextMenuItem {
          width: parent.width
          text: "Paste"
          materialIcon: "content_paste"
          onTriggered: {
            input.paste()
            composerMenu.close()
          }
        }
        ContextMenuItem {
          width: parent.width
          text: "Select all"
          materialIcon: "select_all"
          enabled: input.text !== ""
          onTriggered: {
            input.selectAll()
            composerMenu.close()
          }
        }
      }
    }

  Connections {
    target: root.service
    enabled: !root.demo && root.service !== null
    function onMessageTickChanged() { root.pushLive() }
    function onMessageFailedTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastMessageFailedConv))
        return
      root.applyMessageFailure(root.service.lastMessageFailedRequest,
        root.service.lastMessageFailedCode, root.service.lastMessageFailedDelivered)
    }
    function onIdentityTickChanged() {
      root.stopTyping()
      root.clearReply()
      root.clearEdit()
      root.clearDeleteConfirm()
      lines.clear()
    }
    function onUnreadTickChanged() {
      if (root.readActive && root.service && root.service.unreadFor(root.conversation) > 0) {
        root.markRead()
        unreadClearRetry.restart()
      }
    }
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
    function onReactionFailedTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastReactionFailedConv))
        return
      var code = String(root.service.lastReactionFailedCode || "forbidden")
      root.reactionStatus = code === "offline" ? "Reaction failed: contact is offline"
        : code === "rate_limited" ? "Reaction failed: try again in a moment"
        : code === "not_found" ? "Reaction failed: message no longer exists"
        : code === "history_failed" ? "Reaction sent, but could not be saved locally"
        : "Reaction failed"
      reactionStatusTimer.restart()
    }
    function onReactionTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastReactionConv) ||
          !root.service.lastReactionId)
        return
      for (var i = 0; i < lines.count; i++) {
        var reactionLine = lines.get(i)
        if (reactionLine && reactionLine.id === root.service.lastReactionId) {
          var reactionActor = String(root.service.lastReactionActor || "peer")
          if (root.groupConversation && reactionActor !== "me" && reactionActor !== "peer")
            lines.setProperty(i, "groupReactions", root.updatedGroupReactions(
              reactionLine.groupReactions || [], reactionActor, root.service.lastReactionEmoji))
          else
            lines.setProperty(i, reactionActor === "me" ? "reactionMe" : "reactionPeer",
              root.service.lastReactionEmoji)
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
    function onReceiptSentTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastReceiptSentConv) ||
          root.service.lastReceiptSentState !== "read")
        return
      root.confirmReadReceipt(root.service.lastReceiptSentId)
    }
    function onReceiptFailedTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastReceiptFailedConv) ||
          root.service.lastReceiptFailedState !== "read")
        return
      root.failReadReceipt(root.service.lastReceiptFailedId)
    }
    function onHelperHandshakeTickChanged() {
      root.retryReadReceipts()
      if (!root.demo && root.service && root.conversation)
        root.service.retryHistory(root.conversation)
    }
    function onHistoryTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastHistoryConv))
        return
      root.applyHistory(root.service.lastHistoryItems, root.service.lastHistoryCleared)
    }
    function onHistoryFailedTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastHistoryFailedConv))
        return
      root.reactionStatus = "Chat history could not be loaded"
      reactionStatusTimer.interval = 6000
      reactionStatusTimer.restart()
    }
    function onLastErrorTickChanged() {
      if (root.service && root.sameConv(root.service.lastErrorConv) &&
          root.service.lastError === "history_failed") {
        root.restoreOutgoingFileStatus()
        root.fileStatus = "File received, but chat history could not be saved"
        root.fileStatusPath = String(root.service.lastFilePath || "")
        fileStatusTimer.interval = 6000
        fileStatusTimer.restart()
      }
    }
    function onLastFileTickChanged() {
      if (!root.service || !root.sameConv(root.service.lastFileConv))
        return
      if (root.service.lastFileDir === "in") {
        root.closeFileChooser()
        if (root.restoreOutgoingFileStatus())
          return
      }
      if (root.service.lastFileState === "offer") {
        root.closeFileChooser()
      } else if (root.service.lastFileState === "sending") {
        fileStatusTimer.stop()
        root.fileStatus = "Sending…"
        var sendingTransfer = root.service.outgoingFile(root.conversation)
        root.fileStatusPath = String(sendingTransfer.path || root.service.lastFilePath || "")
      } else if (root.service.lastFileState === "canceling" || root.service.lastFileState === "canceled") {
        root.closeFileChooser()
      } else if (root.service.lastFileState === "done") {
        var completedTransfer = root.service.outgoingFile(root.conversation)
        var completedPath = root.service.lastFileDir === "out"
          ? String(completedTransfer.path || "") : String(root.service.lastFilePath || "")
        root.closeFileChooser()
        root.fileStatus = "File transfer successful"
        root.fileStatusPath = completedPath
        fileStatusTimer.stop()
      } else if (root.service.lastFileState === "failed") {
        var failedFile = root.service.outgoingFile(root.conversation)
        var failedPath = root.service.lastFileDir === "out"
          ? String(failedFile.path || "") : ""
        root.closeFileChooser()
        root.fileStatus = "File transfer failed: " + (root.service.lastFileError || "file_failed")
        root.fileStatusPath = failedPath
        fileStatusTimer.interval = 6000
        fileStatusTimer.restart()
      }
    }
  }

  onConversationChanged: {
    root.stopTyping()
    fileStatusTimer.stop()
    root.followLatest = true
    root.clearConfirm = false
    root.clearDeleteConfirm()
    root.fileStatus = ""
    root.fileStatusPath = ""
    root.reactionStatus = ""
    root.groupMembersOpen = root.groupConversation
    root.clearGroupMemberAction()
    mediaPlayer.stop()
    root.activeAudioPath = ""
    root.audioErrorPath = ""
    root.audioError = ""
    root.readReceiptIds = []
    root.pendingReceiptIds = []
    root.failedReceiptIds = []
    root.restoreOutgoingFileStatus()
    if (!root.demo && root.service && root.conversation) {
      lines.clear()
      root.service.requestHistory(root.conversation)
    }
  }

  onReadActiveChanged: if (root.readActive) root.markRead()

  Keys.onPressed: function(event) {
    root.markRead()
    if (event.key === Qt.Key_Escape && root.handleEscape()) {
      event.accepted = true
      return
    }
    if (event.key === Qt.Key_O && (event.modifiers & Qt.ControlModifier)) {
      root.attachFile()
      event.accepted = true
    }
  }

  Component.onCompleted: {
    root.groupMembersOpen = root.groupConversation
    if (root.demo)
      root.resetDemo()
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
            text: root.groupConversation ? "group" : "person"
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.72)
            font.family: "Material Symbols Rounded"
            font.pixelSize: Math.round(Style.font.display * 0.64)
            font.variableAxes: ({ "FILL": 0, "wght": 500 })
            renderType: Text.QtRendering
            font.hintingPreference: Font.PreferNoHinting
          }
        }

        Text {
          Layout.fillWidth: true
          text: {
            var name = root.escapeMarkup(root.demo ? "DEMO" : (root.peerName || root.conversation || "chat"))
            if (root.demo)
              return "<font color='" + String(root.peerNameColor) + "'><b>" + name + "</b></font>"
            var status = root.peerConnectionStatus
            return "<font color='" + String(root.peerNameColor) + "'><b>" + name +
              "</b></font> <font color='" + String(root.peerStatusColor) + "'>· " +
              root.escapeMarkup(status) + "</font>"
          }
          textFormat: Text.RichText
          color: root.fg
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.letterSpacing: 1.2
          elide: Text.ElideRight
        }

        FormatBtn {
          visible: root.groupConversation && !root.clearConfirm
          materialIcon: "group"
          helpText: (root.groupMembersOpen ? "Hide" : "Show") + " group members"
          selected: root.groupMembersOpen
          onClicked: root.groupMembersOpen = !root.groupMembersOpen
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
          helpText: "Cancel"
          onClicked: root.clearConfirm = false
        }

        FormatBtn {
          visible: !root.demo && root.clearConfirm
          materialIcon: "check"
          helpText: "Clear this chat"
          selected: true
          onClicked: root.clearChat()
        }

        FormatBtn {
          visible: !root.demo && !root.clearConfirm
          materialIcon: "delete"
          helpText: "Clear messages in this chat"
          onClicked: root.clearConfirm = true
        }
      }

      Item {
        visible: root.groupConversation && root.groupMembersOpen
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? Style.space(32) : 0
        clip: true

        Flickable {
          anchors.fill: parent
          contentWidth: memberRow.width
          contentHeight: height
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.HorizontalFlick
          clip: true

          Row {
            id: memberRow
            height: parent.height
            spacing: Style.space(4)

            Repeater {
              model: root.groupMembers
              delegate: ChatBtn {
                id: memberButton
                required property var modelData
                height: parent ? parent.height : implicitHeight
                text: String(modelData.name || "Member") + " · " +
                  String(modelData.role || "member") + " · " +
                  (modelData.online ? "online" : "offline")
                helpText: String(modelData.name || "Member") + " is " +
                  (modelData.online ? "online" : "offline")
                foreground: modelData.online ? root.accent : root.fg
                bordered: true
                selected: !!modelData.self
                onClicked: memberMenu.popup()
                TapHandler {
                  acceptedButtons: Qt.RightButton
                  onTapped: memberMenu.popup()
                }

                Controls.Menu {
                  id: memberMenu
                  width: Style.space(220)
                  padding: Style.space(4)

                  background: Rectangle {
                    radius: Style.cornerRadius
                    color: Qt.darker(root.bg, 1.08)
                    border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                    border.width: 1
                  }

                  ContextMenuItem {
                    enabled: false
                    text: String(memberButton.modelData.name || "Member") + " · " +
                      (memberButton.modelData.online ? "online" : "offline")
                    materialIcon: memberButton.modelData.online ? "circle" : "circle_outline"
                  }
                  ContextMenuItem {
                    visible: root.groupSelfRole() === "owner" &&
                      !memberButton.modelData.self && memberButton.modelData.online &&
                      memberButton.modelData.role === "member"
                    text: "Make admin…"
                    materialIcon: "shield_person"
                    onTriggered: root.requestGroupMemberAction("admin", memberButton.modelData)
                  }
                  ContextMenuItem {
                    visible: root.groupSelfRole() === "owner" &&
                      !memberButton.modelData.self && memberButton.modelData.online &&
                      memberButton.modelData.role === "admin"
                    text: "Make member…"
                    materialIcon: "person"
                    onTriggered: root.requestGroupMemberAction("member", memberButton.modelData)
                  }
                  ContextMenuItem {
                    visible: root.mayManageGroupMember(memberButton.modelData)
                    text: "Remove member…"
                    materialIcon: "person_remove"
                    onTriggered: root.requestGroupMemberAction("remove", memberButton.modelData)
                  }
                }
              }
            }
          }
        }
      }

      Row {
        visible: root.groupConversation && root.groupActionConfirm !== ""
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? Math.max(groupActionText.implicitHeight,
          cancelGroupAction.implicitHeight) : 0
        spacing: Style.space(4)

        Text {
          id: groupActionText
          width: parent.width - cancelGroupAction.width - confirmGroupAction.width - parent.spacing * 2
          text: root.groupActionConfirm === "remove"
            ? "Remove " + root.groupActionName + " from this group?"
            : (root.groupActionConfirm === "admin"
              ? "Make " + root.groupActionName + " an admin?"
              : "Make " + root.groupActionName + " a member?")
          color: root.groupActionConfirm === "remove" ? (root.theme.unread || root.accent) : root.accent
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          wrapMode: Text.WordWrap
        }
        ChatBtn {
          id: cancelGroupAction
          text: "Cancel"
          onClicked: root.clearGroupMemberAction()
        }
        ChatBtn {
          id: confirmGroupAction
          text: "Confirm"
          bordered: true
          selected: true
          onClicked: root.confirmGroupMemberAction()
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
        activeFocusOnTab: true
        readonly property bool hasVerticalOverflow: contentHeight > height + 1
        readonly property real scrollbarGutter: messageScrollbar.visible
          ? messageScrollbar.width + Style.space(4) : 0
        readonly property real messageLaneWidth: Math.max(0, width - scrollbarGutter)
        Controls.ScrollBar.vertical: Controls.ScrollBar {
          id: messageScrollbar
          visible: list.hasVerticalOverflow
          policy: list.hasVerticalOverflow
            ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
          width: Style.space(5)
          contentItem: Rectangle {
            implicitWidth: Style.space(3)
            radius: width / 2
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.58)
          }
          background: Item {}
        }
        onMovementStarted: root.followLatest = false
        onMovementEnded: root.followLatest = list.atYEnd
        onHeightChanged: root.restoreLatestPosition()
        onContentHeightChanged: root.restoreLatestPosition()
        onActiveFocusChanged: {
          if (activeFocus) {
            root.ensureMessageSelection()
            root.markRead()
          }
        }
        Keys.onPressed: function(event) {
          var item = root.messageAt(list.currentIndex)
          if (event.key === Qt.Key_Up) {
            root.selectMessage(-1, "")
          } else if (event.key === Qt.Key_Down) {
            root.selectMessage(1, "")
          } else if (event.key === Qt.Key_Home) {
            root.selectMessage(1, "start")
          } else if (event.key === Qt.Key_End) {
            root.selectMessage(-1, "end")
          } else if (event.key === Qt.Key_PageUp) {
            for (var up = 0; up < 5; up++) root.selectMessage(-1, "")
          } else if (event.key === Qt.Key_PageDown) {
            for (var down = 0; down < 5; down++) root.selectMessage(1, "")
          } else if (item && item.failed &&
                     (event.key === Qt.Key_R || event.key === Qt.Key_Return ||
                      event.key === Qt.Key_Enter)) {
            root.resendMessage(item.clientKey)
          } else if (item && event.key === Qt.Key_R) {
            root.beginReply(item.id, item.text)
          } else if (item && event.key === Qt.Key_E && item.dir === "out" && !item.deleted) {
            root.beginEdit(item.id, item.text)
          } else if (item && event.key === Qt.Key_Delete && item.dir === "out" && !item.deleted) {
            root.requestDelete(item.id)
          } else if (item && event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
            root.copyText(item.text)
          } else if (item && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            root.beginReply(item.id, item.text)
          } else {
            return
          }
          event.accepted = true
        }
        footer: Column {
          id: fileFooter
          width: list.messageLaneWidth
          spacing: Style.space(6)

          Row {
            visible: root.fileForThis
            height: visible ? implicitHeight : 0
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
            visible: !root.demo && root.fileForThis && service && service.fileNameFor(root.conversation) !== ""
            width: parent.width
            text: service ? service.fileNameFor(root.conversation) : ""
            color: Qt.darker(root.fg, 1.35)
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WrapAnywhere
          }

          Item {
            id: filePathLink
            visible: !root.demo && service && service.filePathFor(root.conversation) !== "" && !root.mediaPathInHistory
            width: parent.width
            implicitHeight: visible ? filePathText.implicitHeight + Style.space(4) : 0
            height: implicitHeight
            activeFocusOnTab: true
            Keys.onReturnPressed: filePathMenu.open()
            Keys.onEnterPressed: filePathMenu.open()
            Keys.onSpacePressed: filePathMenu.open()

            Rectangle {
              anchors.fill: parent
              color: "transparent"
              radius: Style.cornerRadius
              border.color: filePathLink.activeFocus ? root.accent : "transparent"
              border.width: filePathLink.activeFocus ? 1 : 0
            }

            Text {
              id: filePathText
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              anchors.leftMargin: Style.space(2)
              anchors.rightMargin: Style.space(2)
              text: root.filePathForConversation()
              color: root.accent
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              font.underline: true
              wrapMode: Text.WrapAnywhere
            }

            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              scrollGestureEnabled: false
              onClicked: {
                filePathLink.forceActiveFocus()
                filePathMenu.open()
              }
            }

            Controls.Menu {
              id: filePathMenu
              width: Style.space(250)
              padding: Style.space(4)

              function placeMenu() {
                var target = filePathMenu.parent
                if (!target)
                  return
                var point = filePathLink.mapToItem(target, 0, filePathLink.height + Style.space(4))
                var nextX = point.x
                var nextY = point.y
                if (target.width > 0)
                  nextX = Math.max(Style.space(4), Math.min(nextX, target.width - width - Style.space(4)))
                if (target.height > 0)
                  nextY = Math.max(Style.space(4), Math.min(nextY, target.height - height - Style.space(4)))
                x = nextX
                y = nextY
              }

              onOpened: placeMenu()

              background: Rectangle {
                radius: Style.cornerRadius
                color: Qt.darker(root.bg, 1.08)
                border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                border.width: 1
              }

              contentItem: Column {
                width: parent.width
                spacing: 0

                ContextMenuItem {
                  width: parent.width
                  text: "Open containing folder"
                  materialIcon: "folder_open"
                  onTriggered: {
                    root.openFileFolder()
                    filePathMenu.close()
                  }
                }
                ContextMenuItem {
                  width: parent.width
                  text: "Copy full path"
                  materialIcon: "content_copy"
                  onTriggered: {
                    root.copyFilePath()
                    filePathMenu.close()
                  }
                }
              }
            }
          }

        }
        delegate: FocusScope {
          id: line
          width: list.messageLaneWidth
          height: model.newMarker ? newDivider.implicitHeight :
            Math.max(bubble.implicitHeight + (line.hasReaction ? Style.space(12) : 0),
              sysLine.implicitHeight)
          readonly property bool smileOnly: model.dir !== "sys" && root.isSmileOnly(model.text)
          readonly property bool hasCode: model.dir !== "sys" && (String(model.text || "").indexOf("```") !== -1 || new RegExp("\\x60[^\\x60\\n]+\\x60").test(String(model.text || "")))
          readonly property var smileGlyphs: line.smileOnly ? root.splitSmiles(model.text) : []
          readonly property real smileMaxWidth: Math.max(root.smilePx + Style.space(16), list.width * 0.82)
          readonly property real smileReceiptReserve: model.dir === "out" && model.ack !== undefined
            ? Style.space(28) : 0
          readonly property int smileColumns: Math.max(1, Math.floor(
            (line.smileMaxWidth - Style.space(16) - line.smileReceiptReserve + Style.space(2)) /
            (root.smilePx + Style.space(2))))
          readonly property real smileWidth: Math.min(line.smileMaxWidth,
            Math.max(root.smilePx + Style.space(16) + line.smileReceiptReserve,
              Math.min(line.smileGlyphs.length, line.smileColumns) *
                (root.smilePx + Style.space(2)) + Style.space(16) + line.smileReceiptReserve))
          readonly property string contextText: String(model.text || "")
          readonly property string contextId: String(model.id || "")
          readonly property string messageKind: String(model.kind || "")
          readonly property bool fileMessage: model.dir === "in" && !model.deleted &&
            (line.messageKind === "file" || (root.mediaPath !== "" && line.contextText === root.mediaPath))
          readonly property bool audioMessage: line.fileMessage && root.isAudioPath(line.contextText)
          readonly property bool deleted: !!model.deleted
          readonly property bool edited: !!model.edited
          readonly property bool failed: !!model.failed
          readonly property string failureCode: String(model.failureCode || "")
          readonly property string senderPeer: String(model.sender || "")
          readonly property bool showGroupSender: root.groupConversation && model.dir === "in" &&
            line.senderPeer !== ""
          readonly property bool uncertain: line.failureCode === "delivery_unknown"
          readonly property string clientKey: String(model.clientKey || "")
          readonly property bool keyboardSelected: line.ListView.isCurrentItem && list.activeFocus
          readonly property string reactionMe: String(model.reactionMe || "")
          readonly property string reactionPeer: String(model.reactionPeer || "")
          readonly property var groupReactions: model.groupReactions || []
          readonly property var groupReactionEmojis: root.groupReactionEmojiList(
            line.groupReactions)
          readonly property bool messageReactions: line.contextId !== "" &&
            !line.fileMessage && !line.deleted
          readonly property bool hasReaction: !line.deleted &&
            (line.reactionMe !== "" || line.reactionPeer !== "" ||
             line.groupReactionEmojis.length > 0)
          property bool reactionPickerOpen: false
          readonly property bool actionControlsVisible: line.failed ||
            (line.contextId !== "" && !line.deleted && !line.fileMessage &&
             (lineHover.hovered || line.keyboardSelected || line.activeFocus ||
              line.reactionPickerOpen))
          Keys.onEscapePressed: {
            reactionPicker.close()
            line.reactionPickerOpen = false
          }

          HoverHandler {
            id: lineHover
          }

          Rectangle {
            id: bubble
            anchors.left: model.dir === "out" ? undefined : parent.left
            anchors.right: model.dir === "out" ? parent.right : undefined
            width: line.fileMessage
              ? root.fileBubbleWidth(line.contextText, line.audioMessage, parent.width)
              : (line.smileOnly ? line.smileWidth :
                root.bubbleWidth(model.text, line.hasCode,
                  model.dir === "out" && model.ack !== undefined, parent.width))
            implicitHeight: Math.max(
              line.fileMessage ? fileMessageRow.implicitHeight : (line.smileOnly ? smileRow.implicitHeight : label.implicitHeight),
              line.hasCode ? Math.max(codeFooter.implicitHeight, Style.space(30)) : 0) +
              (line.showGroupSender ? groupSenderLabel.implicitHeight + Style.space(3) : 0) +
              Style.space(12)
            radius: Style.cornerRadius
            color: root.bubbleColor(model.dir)
            border.color: line.failed ? (root.theme.unread || root.accent) :
              (line.uncertain ? root.receiptDeliveredColor :
               (line.keyboardSelected ? root.accent : "transparent"))
            border.width: line.failed || line.uncertain || line.keyboardSelected ? 1 : 0
            visible: model.dir !== "sys" && !model.newMarker

            Text {
              id: groupSenderLabel
              visible: line.showGroupSender
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.top: parent.top
              anchors.leftMargin: Style.space(8)
              anchors.rightMargin: Style.space(8)
              anchors.topMargin: Style.space(3)
              text: root.groupMemberName(line.senderPeer)
              color: root.accent
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              elide: Text.ElideRight
            }

            Text {
              id: label
              visible: !line.smileOnly && !line.fileMessage
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.verticalCenter: line.showGroupSender ? undefined : parent.verticalCenter
              anchors.top: line.showGroupSender ? groupSenderLabel.bottom : undefined
              anchors.leftMargin: Style.space(8)
              anchors.rightMargin: line.hasCode ? Style.space(60) :
                (model.dir === "out" && model.ack !== undefined ? Style.space(28) : Style.space(8))
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

            RowLayout {
              id: fileMessageRow
              visible: line.fileMessage
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              anchors.leftMargin: Style.space(6)
              anchors.rightMargin: Style.space(6)
              spacing: Style.space(4)

              FormatBtn {
                id: audioFileAction
                visible: line.audioMessage
                materialIcon: root.audioErrorPath === line.contextText && root.audioError !== ""
                  ? "error" : (root.audioPlaying(line.contextText) ? "pause_circle" : "play_circle")
                helpText: root.audioErrorPath === line.contextText && root.audioError !== ""
                  ? root.audioError : (root.audioPlaying(line.contextText) ? "Pause audio" : "Play audio")
                selected: root.audioPlaying(line.contextText)
                onClicked: root.toggleAudio(line.contextText)
              }

              Text {
                id: genericFileIcon
                visible: !line.audioMessage
                text: "draft"
                color: root.accent
                font.family: "Material Symbols Rounded"
                font.pixelSize: Style.font.icon
                font.variableAxes: ({ "FILL": 0, "wght": 500 })
                renderType: Text.QtRendering
              }

              Item {
                Layout.preferredWidth: Math.min(fileMessageText.implicitWidth,
                  Math.max(Style.space(64), fileMessageRow.width -
                    (line.audioMessage ? audioFileAction.implicitWidth : genericFileIcon.implicitWidth) -
                    fileMoreAction.implicitWidth - fileMessageRow.spacing * 2))
                Layout.maximumWidth: fileMessageRow.width
                implicitHeight: fileMessageText.implicitHeight

                Text {
                  id: fileMessageText
                  anchors.fill: parent
                  text: root.fileDisplayName(line.contextText)
                  color: root.accent
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.bodySmall
                  font.underline: true
                  elide: Text.ElideMiddle
                  verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  scrollGestureEnabled: false
                  onClicked: {
                    root.markRead()
                    messageFileMenu.popup()
                  }
                }
              }

              FormatBtn {
                id: fileMoreAction
                materialIcon: "more_horiz"
                helpText: "File actions"
                suppressHelp: messageFileMenu.visible
                onClicked: messageFileMenu.popup()
              }

              Item { Layout.fillWidth: true }
            }

            Controls.Menu {
              id: messageFileMenu
              width: Style.space(250)
              padding: Style.space(4)

              background: Rectangle {
                radius: Style.cornerRadius
                color: Qt.darker(root.bg, 1.08)
                border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                border.width: 1
              }

              ContextMenuItem {
                text: "Open containing folder"
                materialIcon: "folder_open"
                onTriggered: {
                  root.openFileFolder(line.contextText)
                  messageFileMenu.close()
                }
              }
              ContextMenuItem {
                text: "Copy full path"
                materialIcon: "content_copy"
                onTriggered: {
                  root.copyFilePath(line.contextText)
                  messageFileMenu.close()
                }
              }
              ContextMenuItem {
                visible: line.reactionMe !== ""
                text: "Remove reaction"
                materialIcon: "remove_reaction"
                onTriggered: {
                  root.reactToMessage(line.contextId, line.reactionMe, line.reactionMe)
                  messageFileMenu.close()
                }
              }
            }

            ReceiptMark {
              visible: model.dir === "out" && model.ack !== undefined && !line.hasCode
              anchors.right: parent.right
              anchors.bottom: parent.bottom
              anchors.rightMargin: Style.space(6)
              anchors.bottomMargin: Style.space(2)
              acknowledgement: Number(model.ack || 0)
              failed: line.failed
              uncertain: line.uncertain
              failureCode: line.failureCode
              z: 2
            }

            FormatBtn {
              visible: line.hasCode && !line.smileOnly && !(model.dir === "out" && model.ack !== undefined)
              anchors.top: parent.top
              anchors.right: parent.right
              anchors.topMargin: Style.space(4)
              anchors.rightMargin: Style.space(4)
              z: 2
              materialIcon: "content_copy"
              helpText: "Copy code"
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
                helpText: "Copy code"
                onClicked: root.copyCode(model.text)
              }

              ReceiptMark {
                anchors.verticalCenter: parent.verticalCenter
                acknowledgement: Number(model.ack || 0)
                failed: line.failed
                uncertain: line.uncertain
                failureCode: line.failureCode
              }
            }

            MouseArea {
              id: contextArea
              anchors.fill: parent
              acceptedButtons: Qt.RightButton
              scrollGestureEnabled: false
              z: 1
              onPressed: root.markRead()
              onClicked: messageMenu.popup()
            }

            Controls.Menu {
              id: messageMenu
              width: Style.space(220)
              padding: Style.space(4)
              delegate: ContextMenuItem {}

              background: Rectangle {
                radius: Style.cornerRadius
                color: Qt.darker(root.bg, 1.08)
                border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
                border.width: 1
              }

              ContextMenuItem {
                visible: line.failed
                text: "Resend"
                materialIcon: "refresh"
                onTriggered: root.resendMessage(line.clientKey)
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
                text: "Delete"
                materialIcon: "delete"
                visible: model.dir === "out" && !!line.contextId && !line.deleted
                onTriggered: root.requestDelete(line.contextId)
              }
            }

            Flow {
              id: smileRow
              visible: line.smileOnly
              width: Math.max(root.smilePx,
                line.smileWidth - Style.space(16) - line.smileReceiptReserve)
              height: implicitHeight
              anchors.left: parent.left
              anchors.verticalCenter: line.showGroupSender ? undefined : parent.verticalCenter
              anchors.top: line.showGroupSender ? groupSenderLabel.bottom : undefined
              anchors.leftMargin: Style.space(8)
              spacing: Style.space(2)

              Repeater {
                model: line.smileGlyphs
                delegate: Item {
                  id: smileDelegate
                  required property int index
                  readonly property string glyph: String(line.smileGlyphs[index] || "")
                  width: root.smilePx
                  height: root.smilePx

                  Image {
                    id: smileImage
                    anchors.fill: parent
                    source: root.smileSrc(smileDelegate.glyph)
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: root.smilePx * 2
                    sourceSize.height: root.smilePx * 2
                    smooth: true
                    mipmap: true
                    asynchronous: true
                    cache: true
                  }

                  Text {
                    anchors.fill: parent
                    visible: smileImage.status === Image.Error || smileImage.status === Image.Null
                    text: smileDelegate.glyph
                    color: root.fg
                    font.family: "Noto Color Emoji"
                    font.pixelSize: root.smilePx
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    renderType: Text.QtRendering
                  }
                }
              }
            }
          }

          Rectangle {
            id: reactionBadge
            visible: line.hasReaction && model.dir !== "sys" && !model.newMarker
            anchors.left: bubble.left
            anchors.verticalCenter: bubble.bottom
            anchors.verticalCenterOffset: Style.space(3)
            anchors.leftMargin: Style.space(6)
            width: reactionBadgeRow.implicitWidth + Style.space(8)
            height: Style.space(20)
            radius: height / 2
            color: Qt.darker(root.bg, 1.08)
            border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.2)
            border.width: 1
            z: 4

            Row {
              id: reactionBadgeRow
              anchors.centerIn: parent
              spacing: Style.space(2)

              Repeater {
                id: reactionBadgeRepeater
                model: [line.reactionMe, line.reactionPeer].concat(
                  line.groupReactionEmojis).filter(function(value, index, values) {
                  return value !== "" && values.indexOf(value) === index
                })

                Text {
                  required property int index
                  text: String(reactionBadgeRepeater.model[index] || "")
                  color: root.fg
                  font.family: "Noto Color Emoji"
                  font.pixelSize: Style.font.caption + 2
                  font.hintingPreference: Font.PreferFullHinting
                  renderType: Text.NativeRendering
                }
              }
            }
          }

          Row {
            id: reactionLane
            visible: model.dir !== "sys" && !model.newMarker && line.actionControlsVisible
            anchors.verticalCenter: bubble.verticalCenter
            anchors.left: model.dir === "out" ? undefined : bubble.right
            anchors.right: model.dir === "out" ? bubble.left : undefined
            anchors.leftMargin: model.dir === "out" ? 0 : Style.space(3)
            anchors.rightMargin: model.dir === "out" ? Style.space(3) : 0
            spacing: 0
            z: 5

            ReactionAction {
              visible: line.failed
              compact: true
              materialIcon: "refresh"
              tooltipText: "Resend — " + root.messageFailureText(line.failureCode)
              onClicked: root.resendMessage(line.clientKey)
            }
            ReactionAction {
              id: moreReactionAction
              visible: line.messageReactions
              compact: true
              materialIcon: "add_reaction"
              tooltipText: "React"
              selected: line.reactionPickerOpen || line.reactionMe !== ""
              onClicked: {
                if (reactionPicker.visible)
                  reactionPicker.close()
                else
                  reactionPicker.open()
              }
            }
            ReactionAction {
              visible: model.dir === "out" && line.contextId !== "" && !line.failed
              compact: true
              materialIcon: "edit"
              tooltipText: "Edit message"
              onClicked: root.beginEdit(line.contextId, line.contextText)
            }
          }

          Controls.Popup {
            id: reactionPicker
            width: Style.space(160)
            height: reactionPickerGrid.implicitHeight + padding * 2
            padding: Style.space(4)
            margins: Style.space(3)
            focus: true
            closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
            onOpened: {
              line.reactionPickerOpen = true
              var point = moreReactionAction.mapToItem(line, 0, moreReactionAction.height + Style.space(3))
              x = Math.max(Style.space(3), Math.min(point.x, line.width - width - Style.space(3)))
              var below = moreReactionAction.mapToItem(list, 0,
                moreReactionAction.height + Style.space(3))
              var pickerY = below.y
              if (pickerY + height > list.height - Style.space(3))
                pickerY = moreReactionAction.mapToItem(list, 0, -height - Style.space(3)).y
              pickerY = Math.max(Style.space(3),
                Math.min(pickerY, list.height - height - Style.space(3)))
              y = list.mapToItem(line, 0, pickerY).y
              Qt.callLater(function() {
                var firstReaction = reactionPickerRepeater.itemAt(0)
                if (firstReaction)
                  firstReaction.forceActiveFocus()
              })
            }
            onClosed: {
              line.reactionPickerOpen = false
              if (moreReactionAction.visible)
                moreReactionAction.forceActiveFocus()
            }

            background: Rectangle {
              radius: Style.cornerRadius
              color: Qt.darker(root.bg, 1.08)
              border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
              border.width: 1
            }

            contentItem: Grid {
              id: reactionPickerGrid
              columns: 8
              spacing: 0

              Repeater {
                id: reactionPickerRepeater
                model: root.emojiSet
                delegate: ReactionAction {
                  required property int index
                  readonly property string emojiValue: String(root.emojiSet[index] || "")
                  compact: true
                  emoji: emojiValue
                  selected: line.reactionMe === emojiValue
                  tooltipText: "React with " + emojiValue
                  onClicked: {
                    root.reactToMessage(line.contextId, line.reactionMe, emojiValue)
                    reactionPicker.close()
                  }
                }
              }
            }
          }

          RowLayout {
            id: newDivider
            visible: !!model.newMarker
            anchors.left: parent.left
            anchors.right: parent.right
            implicitHeight: Style.space(24)
            spacing: Style.space(8)

            Rectangle {
              Layout.fillWidth: true
              Layout.alignment: Qt.AlignVCenter
              height: 1
              color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)
            }

            Text {
              text: "New messages"
              color: root.accent
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
            }

            Rectangle {
              Layout.fillWidth: true
              Layout.alignment: Qt.AlignVCenter
              height: 1
              color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.55)
            }
          }

          Text {
            id: sysLine
            visible: model.dir === "sys" && !model.newMarker
            width: parent.width
            text: model.dir === "sys" ? model.text : ""
            color: Qt.darker(root.fg, 1.5)
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            wrapMode: Text.Wrap
          }
        }
      }

      ColumnLayout {
        visible: !root.demo && root.showFile
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0
        Layout.minimumHeight: visible ? implicitHeight : 0
        spacing: Style.space(4)

        RowLayout {
          Layout.fillWidth: true
          spacing: Style.space(4)

          Text {
            Layout.fillWidth: true
            text: "File transfer"
            color: root.fg
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            font.bold: true
          }

          FormatBtn {
            materialIcon: "expand_more"
            helpText: "Collapse file transfer"
            onClicked: root.closeFileChooser()
          }
        }

        RowLayout {
          Layout.fillWidth: true
          spacing: Style.space(6)

          TextField {
            id: filePath
            Layout.fillWidth: true
            activeFocusOnTab: true
            enabled: !root.service || !root.service.fileSendingFor(root.conversation)
            foreground: root.fg
            accent: root.accent
            placeholderText: "Absolute file path"
            onTextChanged: {
              if (!root.restoreOutgoingFileStatus()) {
                root.fileStatus = ""
                root.fileStatusPath = ""
              }
            }
            onAccepted: root.sendSelectedFile()
          }
          ChatBtn {
            id: chooseFileBtn
            text: "Choose"
            bordered: true
            enabled: !root.service || !root.service.fileSendingFor(root.conversation)
            onClicked: root.openFilePicker()
          }
          ChatBtn {
            id: sendFileBtn
            text: "Send file"
            bordered: true
            enabled: !root.service || !root.service.fileSendingFor(root.conversation)
            onClicked: root.sendSelectedFile()
          }
        }
      }

      RowLayout {
        visible: !root.demo && (root.reactionStatus !== "" || root.fileStatus !== "" ||
          root.fileStatusPath !== "" ||
          (root.service && root.service.fileSendingFor(root.conversation)))
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? implicitHeight : 0
        Layout.minimumHeight: visible ? implicitHeight : 0
        spacing: Style.space(8)

        ColumnLayout {
          Layout.fillWidth: true
          spacing: Style.space(2)

          Text {
            Layout.fillWidth: true
            text: root.reactionStatus !== "" ? root.reactionStatus : root.fileStatus
            color: root.fileStatus === "Sending…" && root.reactionStatus === ""
              ? root.accent : Qt.darker(root.fg, 1.35)
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.Wrap
          }

          Text {
            id: statusPathText
            visible: root.reactionStatus === "" && root.fileStatusPath !== ""
            Layout.fillWidth: true
            text: root.fileStatusPath
            color: root.accent
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            elide: Text.ElideMiddle

            HoverHandler {
              id: statusPathHover
            }

            OmaqTooltip {
              visible: statusPathHover.hovered && statusPathText.truncated
              text: root.fileStatusPath
            }
          }
        }

        ChatBtn {
          visible: root.service && root.service.fileSendingFor(root.conversation)
          text: "Cancel"
          bordered: true
          onClicked: root.cancelOutgoingFile()
        }

        FormatBtn {
          visible: (!root.service || !root.service.fileSendingFor(root.conversation)) &&
            root.reactionStatus === "" && root.fileStatus !== ""
          materialIcon: "close"
          helpText: "Dismiss file status"
          onClicked: {
            fileStatusTimer.stop()
            root.fileStatus = ""
            root.fileStatusPath = ""
          }
        }
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
          x: inputBox.x
          width: inputBox.width
          visible: root.replyToId !== "" || root.editingId !== "" || root.deleteConfirmId !== ""
          spacing: Style.space(6)
          height: visible ? Math.max(replyPreview.implicitHeight, clearReplyBtn.implicitHeight) : 0

          Text {
            id: replyPreview
            width: parent.width - clearReplyBtn.implicitWidth - confirmDeleteBtn.width - parent.spacing * 2
            text: root.deleteConfirmId !== "" ? "Delete this message?" :
              (root.editingId !== "" ? "Editing message" : ("Reply: " + (root.replyToText || root.replyToId)))
            color: root.accent
            font.family: root.fontFamily
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
          }

          FormatBtn {
            id: confirmDeleteBtn
            visible: root.deleteConfirmId !== ""
            width: visible ? implicitWidth : 0
            materialIcon: "delete"
            helpText: "Delete message"
            selected: true
            onClicked: root.confirmDelete()
          }

          FormatBtn {
            id: clearReplyBtn
            materialIcon: "close"
            helpText: "Cancel"
            onClicked: root.deleteConfirmId !== "" ? root.clearDeleteConfirm() :
              (root.editingId !== "" ? root.clearEdit() : root.clearReply())
          }
        }

        Flow {
          id: emojiFlow
          x: inputBox.x
          width: inputBox.width
          height: visible ? implicitHeight : 0
          spacing: Style.space(3)
          visible: root.emojiOpen

          Repeater {
            model: root.emojiSet
            ChatBtn {
              required property int index
              readonly property string emojiValue: String(root.emojiSet[index] || "")
              text: emojiValue
              helpText: emojiValue
              onClicked: root.insertEmoji(emojiValue)
            }
          }
        }

        Item {
          id: formatFlow
          x: inputBox.x
          width: inputBox.width
          height: visible ? Style.space(30) : 0
          visible: root.formatToolbarEnabled
          clip: true
          onVisibleChanged: if (!visible) formatFlick.contentX = 0

          Flickable {
            id: formatFlick
            anchors.fill: parent
            clip: true
            contentWidth: formatRow.width
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            interactive: true

            Row {
              id: formatRow
              height: parent.height
              spacing: Style.space(3)

              FormatBtn {
                materialIcon: "format_h1"
                helpText: "Heading"
                onClicked: root.prefixLine("# ")
              }
              FormatBtn {
                materialIcon: "format_bold"
                helpText: "Bold"
                onClicked: root.wrapSelection("**", "**", "bold")
              }
              FormatBtn {
                materialIcon: "format_italic"
                helpText: "Italic"
                onClicked: root.wrapSelection("*", "*", "italic")
              }
              FormatBtn {
                materialIcon: "format_quote"
                helpText: "Quote"
                onClicked: root.prefixLine("> ")
              }
              FormatBtn {
                materialIcon: "code"
                helpText: "Code"
                onClicked: root.formatCode()
              }
              FormatBtn {
                materialIcon: "link"
                helpText: "Link"
                onClicked: root.insertLink()
              }
              FormatBtn {
                materialIcon: "format_list_bulleted"
                helpText: "Unordered list"
                onClicked: root.prefixLine("- ")
              }
              FormatBtn {
                materialIcon: "format_list_numbered"
                helpText: "Numbered list"
                onClicked: root.prefixLine("1. ")
              }
              FormatBtn {
                materialIcon: "checklist"
                helpText: "Task list"
                onClicked: root.prefixLine("- [ ] ")
              }
            }
          }

          Rectangle {
            visible: formatFlick.contentX > 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Style.space(34)
            color: root.bg
            z: 1
          }

          FormatBtn {
            visible: formatFlick.contentX > 0
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            materialIcon: "chevron_left"
            helpText: "Previous formatting tools"
            z: 2
            onClicked: formatFlick.contentX = Math.max(0, formatFlick.contentX - Style.space(90))
          }

          Rectangle {
            visible: formatFlick.contentX < formatFlick.contentWidth - formatFlick.width - 1
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Style.space(34)
            color: root.bg
            z: 1
          }

          FormatBtn {
            visible: formatFlick.contentX < formatFlick.contentWidth - formatFlick.width - 1
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            materialIcon: "chevron_right"
            helpText: "More formatting tools"
            z: 2
            onClicked: formatFlick.contentX = Math.min(
              Math.max(0, formatFlick.contentWidth - formatFlick.width),
              formatFlick.contentX + Style.space(90))
          }
        }

        RowLayout {
          id: composerRow
          width: parent.width
          implicitHeight: input.height
          spacing: Style.space(4)

            FormatBtn {
              visible: !root.groupConversation
              materialIcon: "attach_file"
              helpText: "File (Ctrl+O)"
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
                verticalAlignment: Text.AlignVCenter
                activeFocusOnTab: true
                placeholderText: root.demo ? "Demo message" : "Message (Ctrl+Enter to send)"
                onTextChanged: root.updateTyping()
                persistentSelection: true
                background: BorderSurface {
                  readonly property var stateBorder: Border.controlSpec(
                    input.activeFocus ? "focus" : "normal", root.fg, root.accent)
                  color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.06)
                  borderSpec: Border.withWidth(stateBorder, Style.normalBorderWidth)
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

              HoverHandler {
                cursorShape: Qt.IBeamCursor
                onHoveredChanged: if (hovered && !input.activeFocus)
                  input.forceActiveFocus()
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
              materialIcon: "text_format"
              helpText: root.formatToolbarEnabled
                ? "Hide formatting tools" : "Show formatting tools"
              selected: root.formatToolbarEnabled
              Accessible.role: Accessible.CheckBox
              Accessible.checked: root.formatToolbarEnabled
              onClicked: root.formatToolbarToggled(!root.formatToolbarEnabled)
            }

            FormatBtn {
              materialIcon: "mood"
              helpText: "Emoji"
              selected: root.emojiOpen
              onClicked: root.emojiOpen = !root.emojiOpen
            }

            FormatBtn {
              materialIcon: "send"
              helpText: "Send (Ctrl+Enter)"
              bordered: true
              onClicked: root.send()
            }
        }
      }
    }
  }
}
