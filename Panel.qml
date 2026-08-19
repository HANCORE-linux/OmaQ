import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import "Model.js" as Model

Panel {
  id: root
  moduleName: "hancore.omaq"
  ipcTarget: "hancore.omaq"
  manageIpc: false

  property string redeemDraft: ""
  property bool nospamConfirm: false

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  Service {
    id: omaq
    settings: root.settings
  }

  ChatSurface {
    id: chatSurface
    visible: !omaq.attached
    service: omaq
    bar: root.bar
    settings: root.settings
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  onOpenedChanged: if (opened) {
    omaq.sendOp({ op: "status" })
    Qt.callLater(function() { if (keyCatcher) keyCatcher.forceActiveFocus() })
  }

  IpcHandler {
    target: root.ipcTarget
    function open(): void { root.open() }
    function close(): void { root.close() }
    function show(): void { root.open() }
    function hide(): void { root.close() }
    function toggle(): void { root.toggle() }
    function invite(): string { omaq.createInvite(); return "ok" }
    function status(): string { return omaq.statusText }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    iconComponent: Component {
      Text {
        text: omaq.unreadCount > 0 ? String(omaq.unreadCount) : "Q"
        color: root.barForeground
        font.pixelSize: 12
        font.family: root.fontFamily
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
      }
    }
    onPressed: function() { root.toggle() }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(380))
    contentHeight: panel.fittedContentHeight(column.implicitHeight, Style.space(520))

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
          id: column
          width: flick.width
          spacing: Style.space(10)

          Text {
            width: parent.width
            text: "OmaQ"
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.body
          }

          Text {
            visible: omaq.lastError !== ""
            width: parent.width
            text: omaq.lastError
            color: bar ? bar.urgent : Color.urgent
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          Button {
            width: parent.width
            text: "Create one-time invitation"
            onClicked: omaq.createInvite()
          }

          Text {
            visible: omaq.inviteUrl !== ""
            width: parent.width
            text: omaq.inviteUrl
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WrapAnywhere
          }

          Row {
            visible: omaq.inviteUrl !== ""
            spacing: Style.space(8)
            Button { text: "Save QR"; onClicked: omaq.saveQr() }
            Button { text: "Revoke"; onClicked: omaq.revokeInvite() }
          }

          Text {
            visible: omaq.qrPath !== ""
            width: parent.width
            text: "QR: " + omaq.qrPath
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WrapAnywhere
          }

          TextField {
            width: parent.width
            text: root.redeemDraft
            placeholderText: "Paste invite link"
            onTextChanged: root.redeemDraft = text
          }

          Button {
            width: parent.width
            text: "Redeem"
            onClicked: {
              if (Model.parseInvite(root.redeemDraft))
                omaq.redeem(root.redeemDraft)
              else
                omaq.lastError = "unsupported"
            }
          }

          Row {
            visible: omaq.pending
            spacing: Style.space(8)
            Button { text: "Accept"; onClicked: omaq.decide(true) }
            Button { text: "Decline"; onClicked: omaq.decide(false) }
          }

          Text {
            visible: omaq.safetyCode !== ""
            width: parent.width
            text: "Safety code\n" + omaq.safetyCode
            color: root.foreground
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          Button {
            visible: omaq.safetyCode !== "" || omaq.lastConversation !== ""
            width: parent.width
            text: "Show safety code"
            onClicked: omaq.getSafety()
          }

          Button {
            width: parent.width
            text: "Open card"
            onClicked: {
              omaq.openCard()
              if (chatSurface)
                chatSurface.ensureCard(omaq.lastConversation)
            }
          }

          Button {
            width: parent.width
            text: "Create group"
            onClicked: omaq.createGroup()
          }

          Text {
            visible: omaq.lastGroup !== ""
            width: parent.width
            text: "Group " + omaq.lastGroup
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
          }

          Button {
            visible: omaq.lastGroup !== ""
            width: parent.width
            text: "Invite last contact"
            onClicked: omaq.inviteToGroup()
          }

          Button {
            visible: omaq.lastGroup !== ""
            width: parent.width
            text: "Dissolve group"
            onClicked: omaq.dissolveGroup()
          }

          TextField {
            id: searchField
            width: parent.width
            placeholderText: "Search this chat"
          }

          Button {
            width: parent.width
            text: "Search"
            onClicked: omaq.searchChat(searchField.text)
          }

          Button {
            width: parent.width
            text: "Export identity"
            onClicked: omaq.exportIdentity()
          }

          TextField {
            id: passField
            width: parent.width
            echoMode: TextInput.Password
            placeholderText: omaq.locked ? "Passphrase to unlock" : "Passphrase for identity file"
          }

          Button {
            visible: omaq.locked
            width: parent.width
            text: "Unlock identity"
            onClicked: omaq.unlockIdentity(passField.text)
          }

          Button {
            visible: !omaq.locked && !omaq.saveProtected
            width: parent.width
            text: "Protect identity"
            onClicked: omaq.protectIdentity(passField.text)
          }

          Button {
            visible: !omaq.locked && omaq.saveProtected
            width: parent.width
            text: "Remove identity lock"
            onClicked: omaq.unprotectIdentity(passField.text)
          }

          Button {
            width: parent.width
            text: "Remove contact"
            onClicked: omaq.removeContact()
          }

          Button {
            width: parent.width
            text: "Rotate personal ID"
            onClicked: root.nospamConfirm = true
          }

          Text {
            visible: root.nospamConfirm
            width: parent.width
            text: "This voids every open invite."
            color: bar ? bar.urgent : Color.urgent
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          Row {
            visible: root.nospamConfirm
            spacing: Style.space(8)
            Button {
              text: "Cancel"
              onClicked: root.nospamConfirm = false
            }
            Button {
              text: "Rotate"
              onClicked: {
                omaq.rotateNospam()
                root.nospamConfirm = false
              }
            }
          }
        }
      }
    }
  }
}
