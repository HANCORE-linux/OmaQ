import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons

Item {
  id: root
  property var service: null
  property var theme: ({ bg: "", fg: "", accent: "", unread: "" })
  property string conversation: ""
  property bool terminalLook: false
  property bool pulseUnread: false
  property bool showFile: false
  property bool demo: false
  property bool emojiOpen: false

  readonly property color fg: theme.fg || Color.foreground
  readonly property color bg: theme.bg || Color.background
  readonly property color accent: theme.accent || Color.accent
  readonly property string fontFamily: terminalLook ? "monospace" : Style.font.family
  readonly property bool imageFile: {
    if (!service)
      return false
    var p = service.lastFilePath || ""
    return /\.(png|jpe?g|gif|webp)$/i.test(p)
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
    if (!service || !service.pendingFile)
      return false
    return root.sameConv(service.lastFileConv || service.lastConversation)
  }

  property bool demoIncomingFile: false
  property bool demoIncomingCall: false
  property bool demoInCall: false
  property int demoReplyIndex: 0
  property var recentEmojis: []

  readonly property int recentLimit: 6
  readonly property int smilePx: 24
  readonly property int smileTextPx: Style.font.body
  readonly property int smilePad: Style.space(6)
  readonly property int smileGap: Style.space(2)
  readonly property int smileCell: root.smilePx + root.smilePad
  readonly property string recentsDir: {
    if (service && service.stateDir)
      return service.stateDir
    return (Quickshell.env("HOME") || "") + "/.local/state/omaq"
  }
  readonly property string recentsPath: root.recentsDir + "/recent-emoji.json"

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

  component Smile: Item {
    id: cell
    property string glyph: ""
    property int px: root.smilePx
    signal clicked()

    implicitWidth: root.smileCell
    implicitHeight: root.smileCell

    Rectangle {
      anchors.fill: parent
      radius: Style.cornerRadius
      color: cellMouse.containsMouse ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.28) : Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.10)
      border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.16)
      border.width: 1
    }

    Image {
      anchors.centerIn: parent
      width: cell.px
      height: cell.px
      source: root.smileSrc(cell.glyph)
      fillMode: Image.PreserveAspectFit
      sourceSize.width: 64
      sourceSize.height: 64
      smooth: true
      mipmap: true
      asynchronous: true
      cache: true
    }

    MouseArea {
      id: cellMouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: cell.clicked()
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

  function sameConv(conv) {
    if (!root.conversation)
      return true
    if (!conv)
      return false
    return String(conv) === String(root.conversation)
  }

  function applyHistory(items) {
    lines.clear()
    if (!items || !items.length)
      return
    var i
    for (i = 0; i < items.length; i++) {
      var it = items[i]
      if (!it || !it.text)
        continue
      var dir = it.dir === "out" ? "out" : (it.dir === "sys" ? "sys" : "in")
      lines.append({ dir: dir, text: it.text })
    }
    list.positionViewAtEnd()
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
    root.emojiOpen = false
    root.demoReplyIndex = 0
    lines.append({ dir: "sys", text: "Local demo. Nothing is sent." })
    lines.append({ dir: "in", text: "Invite-only chat. This window is how a 1:1 looks." })
    lines.append({ dir: "out", text: "Can I type here?" })
    lines.append({ dir: "in", text: "Yes. Type below. Paperclip attaches, the smile adds emoji, the handset is a call. Long wrap should stay inside the bubble." })
    lines.append({ dir: "in", text: "Safety-looking sample\n8A2F 91C0 44BE 110D" })
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
    if (lines.count > 0) {
      var last = lines.get(lines.count - 1)
      if (last && last.text === t && last.dir === dir)
        return
    }
    lines.append({ dir: dir, text: t })
    list.positionViewAtEnd()
  }

  function send() {
    var t = input.text
    if (!t)
      return
    input.text = ""
    root.emojiOpen = false
    lines.append({ dir: "out", text: t })
    list.positionViewAtEnd()
    if (root.demo) {
      demoReply.restart()
      return
    }
    if (!service)
      return
    service.sendOp({ op: "msg.send", conversation: root.conversation || service.lastConversation, text: t })
  }

  function seedRecents() {
    var out = []
    var i
    for (i = 0; i < root.emojiSet.length && out.length < root.recentLimit; i++)
      out.push(root.emojiSet[i])
    root.recentEmojis = out
  }

  function rememberEmoji(glyph) {
    if (!glyph)
      return
    var next = [glyph]
    var cur = root.recentEmojis
    var i
    for (i = 0; i < cur.length; i++) {
      if (cur[i] !== glyph)
        next.push(cur[i])
      if (next.length >= root.recentLimit)
        break
    }
    root.recentEmojis = next
    recentsMkdir.running = false
    recentsMkdir.running = true
    recentsFile.setText(JSON.stringify(next) + "\n")
  }

  function insertEmoji(glyph) {
    input.insert(input.cursorPosition, glyph)
    root.rememberEmoji(glyph)
    root.emojiOpen = false
    input.forceActiveFocus()
  }

  function startCall() {
    if (root.demo) {
      root.demoIncomingCall = false
      root.demoInCall = true
      lines.append({ dir: "sys", text: "Call started (demo)" })
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
      lines.append({ dir: "sys", text: "Call answered (demo)" })
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
      lines.append({ dir: "sys", text: "Call ended (demo)" })
      list.positionViewAtEnd()
      return
    }
    if (service)
      service.stopCall(root.conversation)
  }

  function attachFile() {
    if (root.demo) {
      root.demoIncomingFile = true
      lines.append({ dir: "sys", text: "File offer: notes.png (demo)" })
      list.positionViewAtEnd()
      return
    }
    root.showFile = !root.showFile
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
      lines.append({ dir: "in", text: pool[i] })
      list.positionViewAtEnd()
    }
  }

  Connections {
    target: root.service
    enabled: !root.demo && root.service !== null
    function onLastChatTextChanged() { root.pushLive() }
    function onHistoryTickChanged() {
      if (!root.service)
        return
      if (!root.sameConv(root.service.lastHistoryConv))
        return
      if (lines.count > 0)
        return
      root.applyHistory(root.service.lastHistoryItems)
    }
  }

  Component.onCompleted: {
    recentsMkdir.running = true
    if (root.recentEmojis.length === 0)
      root.seedRecents()
    if (root.demo)
      root.resetDemo()
    else if (service)
      service.requestHistory(root.conversation || service.lastConversation)
  }

  Process {
    id: recentsMkdir
    command: ["mkdir", "-p", root.recentsDir]
    running: false
  }

  FileView {
    id: recentsFile
    path: root.recentsPath
    printErrors: false
    watchChanges: true
    onFileChanged: reload()
    onLoaded: {
      try {
        var parsed = JSON.parse(text())
        if (parsed && parsed.length)
          root.recentEmojis = parsed
      } catch (e) {
      }
    }
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

        Text {
          Layout.fillWidth: true
          text: root.demo ? "DEMO" : (root.conversation || "chat").toUpperCase()
          color: root.demo ? root.accent : Qt.darker(root.fg, 1.4)
          font.family: root.fontFamily
          font.pixelSize: Style.font.caption
          font.bold: true
          font.letterSpacing: 1.2
          elide: Text.ElideRight
        }

        ChatBtn {
          visible: !root.inCall && !root.incoming
          iconText: "󰏲"
          tooltipText: "Call"
          onClicked: root.startCall()
        }

        ChatBtn {
          visible: root.incoming && !root.inCall
          iconText: "󰏴"
          tooltipText: "Answer"
          bordered: true
          selected: true
          onClicked: root.answerCall()
        }

        ChatBtn {
          visible: root.inCall
          iconText: "󰖂"
          tooltipText: "Hang up"
          bordered: true
          selected: true
          onClicked: root.hangUp()
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

        delegate: Item {
          id: line
          width: list.width
          height: Math.max(bubble.implicitHeight, sysLine.implicitHeight)
          readonly property bool smileOnly: model.dir !== "sys" && root.isSmileOnly(model.text)
          readonly property var smileGlyphs: line.smileOnly ? root.splitSmiles(model.text) : []

          Rectangle {
            id: bubble
            anchors.left: model.dir === "out" ? undefined : parent.left
            anchors.right: model.dir === "out" ? parent.right : undefined
            width: Math.min((line.smileOnly ? smileRow.implicitWidth : label.implicitWidth) + Style.space(16), parent.width * 0.82)
            implicitHeight: (line.smileOnly ? smileRow.implicitHeight : label.implicitHeight) + Style.space(12)
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
              anchors.rightMargin: Style.space(8)
              text: !line.smileOnly && model.dir !== "sys" ? model.text : ""
              color: root.fg
              font.family: root.fontFamily
              font.pixelSize: root.smileTextPx
              font.hintingPreference: Font.PreferNoHinting
              renderType: Text.QtRendering
              wrapMode: Text.Wrap
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
              lines.append({ dir: "sys", text: "Accepted notes.png (demo)" })
              list.positionViewAtEnd()
            } else if (service) {
              service.acceptFile()
            }
          }
        }
        ChatBtn {
          text: "Decline"
          onClicked: {
            if (root.demo) {
              root.demoIncomingFile = false
              lines.append({ dir: "sys", text: "Declined file (demo)" })
              list.positionViewAtEnd()
            } else if (service) {
              service.cancelFile()
            }
          }
        }
      }

      Text {
        visible: !root.demo && service && service.lastFileName !== ""
        Layout.fillWidth: true
        text: {
          if (!service)
            return ""
          return service.lastFilePath || service.lastFileName || ""
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
        source: {
          if (!service)
            return ""
          var p = service.lastFilePath || ""
          return p ? ("file://" + p) : ""
        }
      }

      Row {
        visible: !root.demo && root.showFile
        Layout.fillWidth: true
        spacing: Style.space(8)
        TextField {
          id: filePath
          width: parent.width - sendFileBtn.implicitWidth - parent.spacing
          foreground: root.fg
          accent: root.accent
          placeholderText: "Absolute file path"
          onAccepted: sendFileBtn.clicked()
        }
        ChatBtn {
          id: sendFileBtn
          text: "Send file"
          bordered: true
          onClicked: {
            if (service && filePath.text)
              service.sendFile(filePath.text, root.conversation)
          }
        }
      }

      Column {
        id: composerCol
        Layout.fillWidth: true
        spacing: Style.space(4)
        z: 2

        Flow {
          id: pickerFlow
          width: parent.width
          spacing: root.smileGap
          visible: root.emojiOpen

          Repeater {
            model: root.emojiSet
            Smile {
              required property string modelData
              glyph: modelData
              px: root.smilePx
              onClicked: root.insertEmoji(modelData)
            }
          }
        }

        Item {
          id: recentsDock
          width: parent.width
          implicitHeight: root.smileCell + Style.space(8)
          visible: !root.emojiOpen

          Item {
            id: recentsBar
            x: input.x
            width: Math.max(root.smileCell, input.width)
            height: parent.height

            property int page: 0
            readonly property int arrowSlot: root.smileCell + root.smileGap
            readonly property int fitCount: {
              var inner = width
              var cell = root.smileCell + root.smileGap
              var nAll = Math.floor((inner + root.smileGap) / cell)
              if (nAll < 1)
                nAll = 1
              var more = root.recentEmojis.length > nAll
              var inner2 = more ? inner - recentsBar.arrowSlot : inner
              var n = Math.floor((inner2 + root.smileGap) / cell)
              if (n < 1)
                n = 1
              if (n > root.recentLimit)
                n = root.recentLimit
              return n
            }
            readonly property int pageCount: Math.max(1, Math.ceil(root.recentEmojis.length / Math.max(1, recentsBar.fitCount)))
            readonly property bool hasMore: recentsBar.pageCount > 1
            readonly property var visibleRecents: {
              var src = root.recentEmojis
              var n = recentsBar.fitCount
              var start = recentsBar.page * n
              if (!src || n <= 0)
                return []
              if (start >= src.length)
                return src.slice(0, n)
              return src.slice(start, start + n)
            }

            function nextPage() {
              recentsBar.page = (recentsBar.page + 1) % recentsBar.pageCount
            }

            onFitCountChanged: recentsBar.page = 0

            Row {
              id: recentsRow
              anchors.left: parent.left
              anchors.verticalCenter: parent.verticalCenter
              spacing: root.smileGap

              Repeater {
                model: recentsBar.visibleRecents
                Smile {
                  required property string modelData
                  glyph: modelData
                  px: root.smilePx
                  onClicked: root.insertEmoji(modelData)
                }
              }
            }

            Item {
              visible: recentsBar.hasMore
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              width: root.smileCell
              height: root.smileCell

              Rectangle {
                anchors.fill: parent
                radius: Style.cornerRadius
                color: moreMouse.containsMouse ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.28) : Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.10)
                border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.16)
                border.width: 1
              }

              Text {
                anchors.centerIn: parent
                text: "›"
                color: root.fg
                font.family: root.fontFamily
                font.pixelSize: root.smilePx
                font.bold: true
                renderType: Text.NativeRendering
              }

              MouseArea {
                id: moreMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: recentsBar.nextPage()
              }
            }
          }
        }

        RowLayout {
          width: parent.width
          spacing: Style.space(4)

            ChatBtn {
              iconText: "󰁦"
              tooltipText: "File"
              selected: root.showFile
              onClicked: root.attachFile()
            }

            TextField {
              id: input
              Layout.fillWidth: true
              foreground: root.fg
              accent: root.accent
              font.family: root.fontFamily
              font.pixelSize: root.smileTextPx
              font.hintingPreference: Font.PreferNoHinting
              placeholderText: root.demo ? "Demo message" : "Message"
              onAccepted: root.send()
            }

            ChatBtn {
              iconText: "󰇷"
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
