import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import qs.Ui
import qs.Commons
import "Model.js" as Model

BarWidget {
  id: root
  moduleName: "hancore.omaq"

  property bool opened: false
  property string redeemDraft: ""
  property bool nospamConfirm: false
  property bool showJoin: false
  property bool inviteOpen: false
  property bool moreOpen: false
  property bool themeOpen: false
  property bool copied: false
  property var systemColors: ["#101315", "#565d60", "#9fa5a9", "#d9dbdc", "#798186", "#aeaeae", "#707070", "#cbc2be"]
  property string systemThemeName: "System"
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color barForeground: bar && "barForeground" in bar ? bar.barForeground : foreground
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property real btnGap: Style.space(8)
  readonly property int pad: Style.spacing.popupPadding
  readonly property int cardWidth: Style.space(340)
  readonly property string barPos: bar && bar.position ? String(bar.position) : "top"
  readonly property real barThickness: {
    var n = bar && bar.barSize !== undefined ? Number(bar.barSize) : NaN
    return isFinite(n) && n > 0 ? n : Style.bar.sizeHorizontal
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  component ActionButton: Button {
    foreground: root.foreground
    accent: Color.accent
    fontFamily: root.fontFamily
    radius: Style.cornerRadius
    bordered: true
    focusable: true
    iconSize: Math.round(Style.font.subtitle * 1.5)
    fontSize: Style.font.body
    horizontalPadding: Style.space(8)
    verticalPadding: Style.space(4)
  }

  function open() {
    if (root.opened)
      return
    root.opened = true
    omaq.sendOp({ op: "status" })
    if (bar && typeof bar.requestPopout === "function")
      bar.requestPopout(root)
  }

  function close() {
    if (!root.opened)
      return
    root.showJoin = false
    root.moreOpen = false
    root.themeOpen = false
    root.nospamConfirm = false
    root.opened = false
    if (bar && typeof bar.releasePopout === "function" && bar.activePopout === root)
      bar.releasePopout(root)
  }

  function toggle() {
    if (root.opened)
      close()
    else
      open()
  }

  function errorText(code) {
    if (code === "locked")
      return "Unlock your identity to continue."
    if (code === "unsupported")
      return "That is not an OmaQ invite."
    if (code === "helper_down")
      return "OmaQ is restarting."
    if (code === "file_failed")
      return "File transfer failed."
    return code
  }

  function shortInvite(u) {
    if (!u)
      return ""
    if (u.length <= 40)
      return u
    return u.slice(0, 20) + "…" + u.slice(-12)
  }

  function copyInvite() {
    if (!omaq.inviteUrl)
      return
    Quickshell.execDetached(["wl-copy", "-n", omaq.inviteUrl])
    root.copied = true
    copiedTimer.restart()
  }

  function toggleInvite() {
    if (root.inviteOpen) {
      root.inviteOpen = false
      return
    }
    root.showJoin = false
    root.inviteOpen = true
    if (!omaq.inviteUrl)
      omaq.createInvite()
  }

  function openChat() {
    if (chatSurface)
      chatSurface.ensureCard(omaq.lastConversation)
    root.close()
  }

  function openDemo() {
    if (chatSurface)
      chatSurface.openDemo()
    root.close()
  }

  readonly property string chatTheme: {
    var v = root.settings && root.settings.chatTheme
    return v ? String(v) : "system"
  }

  function persistSettings(values) {
    var entry = { id: root.moduleName }
    var existing
    for (existing in root.settings)
      if (existing !== "id")
        entry[existing] = root.settings[existing]
    var key
    for (key in values)
      entry[key] = values[key]
    root.settings = entry
    if (root.bar && root.bar.shell && typeof root.bar.shell.updateEntryInline === "function")
      root.bar.shell.updateEntryInline(root.moduleName, entry)
  }

  function setTheme(name) {
    root.persistSettings({ chatTheme: name })
  }

  function openRepo() {
    Quickshell.execDetached(["xdg-open", "https://github.com/HANCORE-linux/OmaQ"])
  }

  function parseSystemColors(raw) {
    var found = ["", "", "", "", "", "", "", ""]
    var lines = String(raw || "").split("\n")
    var i
    for (i = 0; i < lines.length; i++) {
      var match = lines[i].match(/^\s*color([0-7])\s*=\s*["']?(#[0-9A-Fa-f]{6})/)
      if (match)
        found[Number(match[1])] = match[2]
    }
    var n
    for (n = 0; n < 8; n++) {
      if (!found[n])
        found[n] = n === 0 ? "#101315" : (n === 7 ? "#cacccc" : "#888888")
    }
    root.systemColors = found
  }

  function paletteColors(id) {
    if (id === "system")
      return root.systemColors
    return Model.themeColors(id)
  }

  function paletteLabel(id) {
    if (id === "system") {
      if (root.systemThemeName && root.systemThemeName !== "theme" && root.systemThemeName !== "System")
        return "System · " + root.systemThemeName
      return "System"
    }
    return Model.themeName(id)
  }

  function placeCard() {
    var win = button.QsWindow ? button.QsWindow.window : null
    if (!win || !button || !popup.screen)
      return
    var p = button.mapToItem(win.contentItem, 0, 0)
    var gap = Style.gapsOut
    var x = p.x + button.width / 2 - card.width / 2
    var y = p.y + button.height + gap
    if (root.barPos === "bottom")
      y = p.y - card.height - gap
    else if (root.barPos === "left") {
      x = p.x + button.width + gap
      y = p.y
    } else if (root.barPos === "right") {
      x = p.x - card.width - gap
      y = p.y
    }
    var sw = popup.screen.width
    var sh = popup.screen.height
    var m = gap
    x = Math.max(m, Math.min(x, sw - card.width - m))
    y = Math.max(m, Math.min(y, sh - card.height - m))
    card.x = Math.round(x)
    card.y = Math.round(y)
  }

  Service {
    id: omaq
    settings: root.settings
  }

  FileView {
    path: Quickshell.env("HOME") + "/.local/state/omarchy/current/theme/colors.toml"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: root.parseSystemColors(text())
  }

  FileView {
    path: Quickshell.env("HOME") + "/.local/state/omarchy/current/theme.name"
    watchChanges: true
    printErrors: false
    onFileChanged: reload()
    onLoaded: {
      var n = String(text()).replace(/^\s+|\s+$/g, "")
      if (n)
        root.systemThemeName = n
    }
  }

  Process {
    command: ["readlink", "-f", Quickshell.env("HOME") + "/.local/state/omarchy/current/theme"]
    running: true
    stdout: SplitParser {
      onRead: function(line) {
        var parts = String(line).replace(/\s+$/, "").split("/")
        var name = parts.length ? parts[parts.length - 1] : ""
        if (name && name !== "theme" && root.systemThemeName === "System")
          root.systemThemeName = name
      }
    }
  }

  ChatSurface {
    id: chatSurface
    service: omaq
    bar: root.bar
    settings: root.settings
  }

  Timer {
    id: copiedTimer
    interval: 1400
    onTriggered: root.copied = false
  }

  Connections {
    target: omaq
    function onInviteUrlChanged() {
      if (omaq.inviteUrl !== "")
        omaq.saveQr()
    }
  }

  Connections {
    target: bar
    ignoreUnknownSignals: true
    function onActivePopoutChanged() {
      if (root.opened && bar.activePopout && bar.activePopout !== root)
        root.close()
    }
  }

  IpcHandler {
    target: "hancore.omaq"
    function open(): void { root.open() }
    function close(): void { root.close() }
    function show(): void { root.open() }
    function hide(): void { root.close() }
    function toggle(): void { root.toggle() }
    function invite(): string { omaq.createInvite(); return "ok" }
    function demo(): string { root.openDemo(); return "ok" }
    function status(): string { return omaq.statusText }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    iconComponent: Component {
      Item {
        implicitWidth: 18
        implicitHeight: 18
        Image {
          anchors.fill: parent
          source: Qt.resolvedUrl("assets/mark.png")
          fillMode: Image.PreserveAspectFit
          visible: omaq.unreadCount <= 0
          sourceSize.width: width * 2
          sourceSize.height: height * 2
          smooth: true
          mipmap: true
          cache: false
          asynchronous: true
        }
        Text {
          anchors.fill: parent
          visible: omaq.unreadCount > 0
          text: String(omaq.unreadCount)
          color: root.barForeground
          font.pixelSize: 12
          font.family: root.fontFamily
          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }
      }
    }
    onPressed: function(b) {
      if (b === Qt.RightButton)
        return
      root.toggle()
    }
  }

  PanelWindow {
    id: popup
    visible: root.opened
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    screen: button.QsWindow && button.QsWindow.window ? button.QsWindow.window.screen : null

    WlrLayershell.namespace: "omaq-panel"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: root.opened ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None

    anchors { top: true; bottom: true; left: true; right: true }

    readonly property rect passThroughBar: {
      var t = root.barThickness
      var w = popup.screen ? popup.screen.width : 0
      var h = popup.screen ? popup.screen.height : 0
      if (root.barPos === "bottom")
        return Qt.rect(0, 0, w, Math.max(0, h - t))
      if (root.barPos === "left")
        return Qt.rect(t, 0, Math.max(0, w - t), h)
      if (root.barPos === "right")
        return Qt.rect(0, 0, Math.max(0, w - t), h)
      return Qt.rect(0, t, w, Math.max(0, h - t))
    }

    mask: Region {
      x: popup.passThroughBar.x
      y: popup.passThroughBar.y
      width: popup.passThroughBar.width
      height: popup.passThroughBar.height
    }

    onVisibleChanged: if (visible) {
      Qt.callLater(function() {
        root.placeCard()
        panelFocus.forceActiveFocus()
      })
    }

    MouseArea {
      anchors.fill: parent
      enabled: root.opened
      acceptedButtons: Qt.AllButtons
      onClicked: root.close()
    }

    BorderSurface {
      id: card
      width: root.cardWidth
      height: column.implicitHeight + root.pad * 2
      color: Color.popups.background
      borderSpec: Border.surfaceSpec("popups", "border", Color.popups.border, Math.max(1, Style.space(1)))
      radius: Style.cornerRadius

      onHeightChanged: if (root.opened) root.placeCard()

      MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
      }

      FocusScope {
        id: panelFocus
        anchors.fill: parent
        focus: root.opened
        Keys.onEscapePressed: root.close()

        Column {
          id: column
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.top: parent.top
          anchors.margins: root.pad
          spacing: Style.space(12)

          Row {
            width: parent.width
            spacing: Style.space(12)

            Image {
              id: heroMark
              width: Style.font.display
              height: Style.font.display
              anchors.verticalCenter: parent.verticalCenter
              source: Qt.resolvedUrl("assets/mark.png")
              fillMode: Image.PreserveAspectFit
              sourceSize.width: width * 2
              sourceSize.height: height * 2
              smooth: true
              mipmap: true
              cache: false
              asynchronous: true
            }

            Image {
              id: heroLockup
              width: parent.width - heroMark.width - parent.spacing
              height: Math.round(Style.font.display * 2.4)
              anchors.verticalCenter: parent.verticalCenter
              source: Qt.resolvedUrl("assets/OmaQ_lockup.svg")
              fillMode: Image.PreserveAspectFit
              horizontalAlignment: Image.AlignLeft
              sourceSize.width: 749
              sourceSize.height: 322
              smooth: true
              mipmap: true
              cache: false
              asynchronous: true
            }

            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: root.openRepo()
            }
          }

          Flow {
            id: heroActions
            width: parent.width
            spacing: Style.space(8)

              ActionButton {
                visible: !omaq.locked
                iconText: "󰐲"
                text: "Invite"
                selected: root.inviteOpen
                onClicked: root.toggleInvite()
              }
              ActionButton {
                visible: !omaq.locked
                iconText: "󰌆"
                text: "Join"
                selected: root.showJoin
                onClicked: root.showJoin = !root.showJoin
              }
              ActionButton {
                visible: !omaq.locked
                iconText: "󰍩"
                text: "Chat"
                onClicked: root.openChat()
              }
              ActionButton {
                iconText: "󰙨"
                text: "Demo"
                selected: chatSurface && chatSurface.demoOpen
                onClicked: root.openDemo()
              }
              ActionButton {
                iconText: "󰏘"
                text: "Theme"
                selected: root.themeOpen
                accent: root.chatTheme === "system" ? Color.accent : Model.themeFor(root.chatTheme).accent
                onClicked: root.themeOpen = !root.themeOpen
              }
            }

          Column {
            visible: root.themeOpen
            width: parent.width
            spacing: Style.space(8)

            Repeater {
              model: Model.CHAT_THEME_IDS
              delegate: Item {
                id: pal
                required property string modelData
                width: parent ? parent.width : 0
                implicitHeight: palCol.implicitHeight

                Column {
                  id: palCol
                  width: parent.width
                  spacing: Style.space(4)

                  Text {
                    width: parent.width
                    text: root.paletteLabel(pal.modelData)
                    color: root.chatTheme === pal.modelData ? Color.accent : root.foreground
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                    font.bold: true
                    elide: Text.ElideRight
                  }

                  Row {
                    width: parent.width
                    spacing: Style.space(3)

                    Repeater {
                      model: root.paletteColors(pal.modelData)
                      Rectangle {
                        required property string modelData
                        width: Math.max(Style.space(12), (pal.width - Style.space(3) * 7) / 8)
                        height: Style.space(16)
                        radius: Style.cornerRadius
                        color: modelData
                        border.width: 1
                        border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.2)
                      }
                    }
                  }
                }

                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.setTheme(pal.modelData)
                }
              }
            }
          }

          Text {
            visible: omaq.lastError !== "" && !(omaq.locked && omaq.lastError === "locked")
            width: parent.width
            text: root.errorText(omaq.lastError)
            color: root.urgent
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          Column {
            visible: omaq.locked
            width: parent.width
            spacing: Style.space(8)

            Text {
              width: parent.width
              text: "Identity is locked."
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.body
              wrapMode: Text.WordWrap
            }

            TextField {
              id: unlockField
              width: parent.width
              foreground: root.foreground
              password: true
              placeholderText: "Passphrase"
            }

            Button {
              width: parent.width
              text: "Unlock"
              bordered: true
              focusable: true
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: omaq.unlockIdentity(unlockField.text)
            }
          }

          Column {
            visible: !omaq.locked
            width: parent.width
            spacing: Style.space(12)

            BorderSurface {
              visible: omaq.pending
              width: parent.width
              implicitHeight: pendingCol.implicitHeight + Style.space(20)
              color: Style.hoverFillFor(root.foreground, Color.accent)
              radius: Style.cornerRadius

              Column {
                id: pendingCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Style.space(12)
                anchors.rightMargin: Style.space(12)
                spacing: Style.space(8)

                Text {
                  width: parent.width
                  text: omaq.pendingGroup ? "Group invite received" : "Someone wants to chat"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                  wrapMode: Text.WordWrap
                }

                Row {
                  spacing: root.btnGap
                  Button {
                    text: "Accept"
                    bordered: true
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: omaq.decide(true)
                  }
                  Button {
                    text: "Decline"
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: omaq.decide(false)
                  }
                }
              }
            }

            Row {
              visible: omaq.incomingCall
              spacing: root.btnGap
              Button {
                text: "Answer"
                bordered: true
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.answerCall()
              }
              Button {
                text: "Decline call"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.stopCall()
              }
            }

            Column {
              visible: root.inviteOpen && omaq.inviteUrl !== ""
              width: parent.width
              spacing: Style.space(8)

              PanelSectionHeader {
                text: "YOUR INVITE"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Image {
                visible: omaq.qrPath !== ""
                width: 148
                height: 148
                fillMode: Image.PreserveAspectFit
                source: omaq.qrPath !== "" ? ("file://" + omaq.qrPath) : ""
                asynchronous: true
                smooth: false
              }

              Text {
                width: parent.width
                text: root.shortInvite(omaq.inviteUrl)
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              Row {
                spacing: root.btnGap
                Button {
                  text: root.copied ? "Copied" : "Copy link"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: root.copyInvite()
                }
                Button {
                  text: "Revoke"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: {
                    omaq.revokeInvite()
                    root.inviteOpen = false
                    root.copied = false
                  }
                }
              }
            }

            Column {
              visible: root.showJoin
              width: parent.width
              spacing: Style.space(8)

              PanelSectionHeader {
                text: "JOIN"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              TextField {
                id: redeemField
                width: parent.width
                foreground: root.foreground
                placeholderText: "Paste omaq:// invite"
                text: root.redeemDraft
                onTextChanged: root.redeemDraft = text
                onAccepted: joinBtn.clicked()
              }

              Button {
                id: joinBtn
                width: parent.width
                text: "Join chat"
                bordered: true
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: {
                  if (Model.parseInvite(root.redeemDraft))
                    omaq.redeem(root.redeemDraft)
                  else
                    omaq.lastError = "unsupported"
                }
              }
            }

            Column {
              visible: omaq.safetyCode !== ""
              width: parent.width
              spacing: Style.space(6)

              PanelSectionHeader {
                text: "SAFETY CODE"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Text {
                width: parent.width
                text: omaq.safetyCode
                color: root.foreground
                font.family: "monospace"
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }
            }

            PanelSeparator {
              foreground: root.foreground
            }

            Button {
              text: root.moreOpen ? "Less" : "More"
              focusable: true
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: root.moreOpen = !root.moreOpen
            }

            Column {
              visible: root.moreOpen
              width: parent.width
              spacing: Style.space(10)

              PanelSectionHeader {
                text: "CHAT"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Row {
                width: parent.width
                spacing: root.btnGap
                TextField {
                  id: searchField
                  width: parent.width - searchBtn.implicitWidth - root.btnGap
                  foreground: root.foreground
                  placeholderText: "Search this chat"
                  onAccepted: omaq.searchChat(searchField.text)
                }
                Button {
                  id: searchBtn
                  text: "Search"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.searchChat(searchField.text)
                }
              }

              Button {
                visible: omaq.safetyCode === "" && omaq.lastConversation !== ""
                text: "Show safety code"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.getSafety()
              }

              PanelSeparator {
                foreground: root.foreground
              }

              PanelSectionHeader {
                text: "GROUP"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Row {
                spacing: root.btnGap
                Button {
                  text: "Create group"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.createGroup()
                }
                Button {
                  visible: omaq.lastGroup !== ""
                  text: "Invite last contact"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.inviteToGroup()
                }
              }

              Text {
                visible: omaq.lastGroup !== ""
                width: parent.width
                text: omaq.lastGroup
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              Button {
                visible: omaq.lastGroup !== ""
                text: "Dissolve group"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.dissolveGroup()
              }

              PanelSeparator {
                foreground: root.foreground
              }

              PanelSectionHeader {
                text: "IDENTITY"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              TextField {
                id: passField
                width: parent.width
                foreground: root.foreground
                password: true
                placeholderText: "Passphrase for identity file"
              }

              Row {
                spacing: root.btnGap
                Button {
                  visible: !omaq.saveProtected
                  text: "Protect"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.protectIdentity(passField.text)
                }
                Button {
                  visible: omaq.saveProtected
                  text: "Remove lock"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.unprotectIdentity(passField.text)
                }
                Button {
                  text: "Export"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.exportIdentity()
                }
              }

              PanelSeparator {
                foreground: root.foreground
              }

              PanelSectionHeader {
                text: "DANGER"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Button {
                text: "Remove contact"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.removeContact()
              }

              Button {
                visible: !root.nospamConfirm
                text: "Rotate personal ID"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: root.nospamConfirm = true
              }

              Text {
                visible: root.nospamConfirm
                width: parent.width
                text: "This voids every open invite."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              Row {
                visible: root.nospamConfirm
                spacing: root.btnGap
                Button {
                  text: "Cancel"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: root.nospamConfirm = false
                }
                Button {
                  text: "Rotate"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
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
  }
}
