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

  readonly property int recentLimit: 8
  readonly property int smilePx: Style.font.displayLarge
  // Noto Color Emoji ships a single 109px CBDT strike on this box.
  readonly property int smileStrike: 109
  readonly property string smileFont: "Noto Color Emoji"
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

    implicitWidth: px + Style.space(12)
    implicitHeight: px + Style.space(12)

    Rectangle {
      anchors.fill: parent
      radius: Style.cornerRadius
      color: cellMouse.containsMouse ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.22) : "transparent"
    }

    Item {
      width: cell.px
      height: cell.px
      anchors.centerIn: parent
      clip: true

      Text {
        text: cell.glyph
        font.family: root.smileFont
        font.pixelSize: root.smileStrike
        font.hintingPreference: Font.PreferNoHinting
        renderType: Text.NativeRendering
        width: root.smileStrike
        height: root.smileStrike
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        anchors.centerIn: parent
        scale: cell.px / root.smileStrike
        transformOrigin: Item.Center
      }
    }

    MouseArea {
      id: cellMouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: cell.clicked()
    }
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
          width: list.width
          height: Math.max(bubble.implicitHeight, sysLine.implicitHeight)

          Rectangle {
            id: bubble
            anchors.left: model.dir === "out" ? undefined : parent.left
            anchors.right: model.dir === "out" ? parent.right : undefined
            width: Math.min(label.implicitWidth + Style.space(16), parent.width * 0.82)
            implicitHeight: label.implicitHeight + Style.space(12)
            radius: Style.cornerRadius
            color: root.bubbleColor(model.dir)
            visible: model.dir !== "sys"

            Text {
              id: label
              anchors.left: parent.left
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              anchors.leftMargin: Style.space(8)
              anchors.rightMargin: Style.space(8)
              text: model.dir !== "sys" ? model.text : ""
              color: root.fg
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
              font.hintingPreference: Font.PreferNoHinting
              renderType: Text.NativeRendering
              wrapMode: Text.Wrap
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

      Flow {
        visible: root.emojiOpen
        Layout.fillWidth: true
        spacing: Style.space(2)

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

      Item {
        id: composerBlock
        Layout.fillWidth: true
        implicitHeight: composerCol.implicitHeight
        z: 2

        HoverHandler {
          id: composerHover
        }

        readonly property bool recentsVisible: composerHover.hovered && !root.emojiOpen

        Column {
          id: composerCol
          width: parent.width
          spacing: Style.space(4)

          Rectangle {
            visible: composerBlock.recentsVisible
            width: parent.width
            implicitHeight: recentsRow.implicitHeight + Style.space(6)
            radius: Style.cornerRadius
            color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.06)
            border.color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.12)
            border.width: 1
            clip: true

            Row {
              id: recentsRow
              anchors.verticalCenter: parent.verticalCenter
              anchors.left: parent.left
              anchors.leftMargin: Style.space(4)
              spacing: Style.space(2)

              Repeater {
                model: root.recentEmojis
                Smile {
                  required property string modelData
                  glyph: modelData
                  px: root.smilePx
                  onClicked: root.insertEmoji(modelData)
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
}
