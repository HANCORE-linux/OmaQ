import QtQuick
import qs.Ui
import qs.Commons

Item {
  id: root
  property var service: null
  property var theme: ({ bg: "", fg: "", accent: "", unread: "" })
  property string conversation: ""
  property bool terminalLook: false
  property bool pulseUnread: false

  readonly property color fg: theme.fg || (Color.foreground)
  readonly property color bg: theme.bg || (Color.background)
  readonly property string fontFamily: terminalLook ? "monospace" : Style.font.family

  function send() {
    var t = input.text
    if (!t || !service)
      return
    service.sendOp({ op: "msg.send", conversation: root.conversation || service.lastConversation, text: t })
    input.text = ""
  }

  Rectangle {
    anchors.fill: parent
    color: root.bg
    border.color: root.pulseUnread ? (root.theme.unread || Color.accent) : "transparent"
    border.width: root.pulseUnread ? 2 : 0

    Column {
      anchors.fill: parent
      anchors.margins: Style.space(8)
      spacing: Style.space(6)

      Text {
        width: parent.width
        text: root.conversation || "chat"
        color: root.fg
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
      }

      Text {
        width: parent.width
        height: parent.height - Style.space(70)
        text: service && service.lastChatText ? service.lastChatText : ""
        color: root.fg
        font.family: root.fontFamily
        font.pixelSize: Style.font.body
        wrapMode: Text.Wrap
        elide: Text.ElideNone
        clip: true
      }

      Row {
        width: parent.width
        spacing: Style.space(6)
        TextField {
          id: input
          width: parent.width - Style.space(70)
          onAccepted: root.send()
        }
        Button {
          text: "Send"
          onClicked: root.send()
        }
      }

      Row {
        width: parent.width
        spacing: Style.space(6)
        TextField {
          id: filePath
          width: parent.width - Style.space(90)
          placeholderText: "Absolute file path"
        }
        Button {
          text: "File"
          onClicked: {
            if (service && filePath.text)
              service.sendFile(filePath.text)
          }
        }
      }

      Row {
        visible: service && service.pendingFile
        spacing: Style.space(6)
        Button { text: "Accept file"; onClicked: service.acceptFile() }
        Button { text: "Decline file"; onClicked: service.cancelFile() }
      }

      Text {
        visible: service && service.lastFileName !== ""
        width: parent.width
        text: (service.lastFilePath !== "" ? service.lastFilePath : service.lastFileName)
        color: root.fg
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        wrapMode: Text.WrapAnywhere
      }

      Image {
        visible: {
          var p = service ? service.lastFilePath : ""
          return p && /\.(png|jpe?g|gif|webp)$/i.test(p)
        }
        width: Math.min(parent.width, 160)
        height: visible ? 80 : 0
        fillMode: Image.PreserveAspectFit
        source: (service && service.lastFilePath) ? ("file://" + service.lastFilePath) : ""
      }

      Row {
        spacing: Style.space(6)
        Button { text: "Call"; onClicked: if (service) service.startCall() }
        Button {
          visible: service && service.incomingCall
          text: "Answer"
          onClicked: service.answerCall()
        }
        Button { text: "Hang up"; onClicked: if (service) service.stopCall() }
      }
    }
  }
}
