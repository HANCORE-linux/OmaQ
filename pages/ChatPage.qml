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
    }
  }
}
