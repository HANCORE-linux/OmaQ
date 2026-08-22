import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
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
  property string moreSection: ""
  property bool themeOpen: false
  property bool copied: false
  property bool nicknameEditOpen: false
  property bool nicknameSubmitPending: false
  property bool avatarRestorePending: false
  property bool avatarRestoreMore: false
  property int avatarPickExitCode: -1
  property bool avatarPickStreamDone: false
  property var systemColors: ["#101315", "#565d60", "#9fa5a9", "#d9dbdc", "#798186", "#aeaeae", "#707070", "#cbc2be"]
  property string systemThemeName: "System"
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color barForeground: bar && "barForeground" in bar ? bar.barForeground : foreground
  readonly property var shibumiTokens: bar && "visualTokens" in bar ? bar.visualTokens : null
  readonly property string shellStyle: shibumiTokens && shibumiTokens.shellStyle !== undefined
    ? String(shibumiTokens.shellStyle) : "shibumi"
  readonly property bool connectedSurfaceEnabled: shellStyle !== "shibumi"
    && (barPos === "top" || barPos === "bottom")
  property real connectionReveal: 0
  readonly property color panelBackground: shibumiTokens && shibumiTokens.panelBackground !== undefined
    ? shibumiTokens.panelBackground : Color.popups.background
  readonly property color panelBorder: shibumiTokens && shibumiTokens.panelBorder !== undefined
    ? shibumiTokens.panelBorder : Color.popups.border
  readonly property real panelBorderWidth: shibumiTokens && shibumiTokens.panelBorderWidth !== undefined
    ? Number(shibumiTokens.panelBorderWidth) : Math.max(1, Style.space(1))
  readonly property real panelRadius: shibumiTokens && shibumiTokens.panelRadius !== undefined
    ? Number(shibumiTokens.panelRadius) : Style.cornerRadius
  readonly property color controlForeground: bar ? bar.foreground : Color.popups.text
  readonly property color controlAccent: bar ? bar.urgent : Color.accent
  readonly property real controlRadius: shibumiTokens && shibumiTokens.tileRadius !== undefined
    ? Number(shibumiTokens.tileRadius) : Style.cornerRadius
  readonly property color controlBorder: shibumiTokens && shibumiTokens.separator !== undefined
    ? shibumiTokens.separator : Qt.rgba(controlForeground.r, controlForeground.g, controlForeground.b, 0.18)
  readonly property color controlFill: shibumiTokens && shibumiTokens.fillIdle !== undefined
    ? shibumiTokens.fillIdle : Qt.rgba(0, 0, 0, 0.12)
  readonly property color controlHoverFill: shibumiTokens && shibumiTokens.fillHover !== undefined
    ? shibumiTokens.fillHover : Qt.rgba(controlAccent.r, controlAccent.g, controlAccent.b, 0.10)
  readonly property color controlActiveFill: shibumiTokens && shibumiTokens.fillActive !== undefined
    ? shibumiTokens.fillActive : Qt.rgba(controlAccent.r, controlAccent.g, controlAccent.b, 0.18)
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family
  readonly property real btnGap: Style.space(8)
  readonly property int pad: Style.spacing.popupPadding
  readonly property int cardWidth: Style.space(340)
  readonly property string barPos: bar && bar.position ? String(bar.position) : "top"
  readonly property real caretDepth: 5
  readonly property real caretHalfWidth: 6 * connectionReveal
  readonly property real caretTangentControl: 3.75 * connectionReveal
  readonly property real caretTipControl: 1.75 * connectionReveal
  readonly property real anchorCenterX: {
    var win = button && button.QsWindow ? button.QsWindow.window : null
    if (!win || !button)
      return card.width / 2
    var p = button.mapToItem(win.contentItem, 0, 0)
    return p.x + button.width / 2 - card.x
  }
  readonly property real caretCenterX: Math.max(10, Math.min(card.width - 10,
    Math.round(anchorCenterX)))
  readonly property string avatarPickerScript:
    "if command -v zenity >/dev/null 2>&1; then\n" +
    "  exec zenity --file-selection --title='Set avatar' --file-filter='Images | *.png *.jpg *.jpeg *.webp'\n" +
    "elif command -v kdialog >/dev/null 2>&1; then\n" +
    "  exec kdialog --getopenfilename \"$HOME\" '*.png *.jpg *.jpeg *.webp|Images'\n" +
    "elif command -v yad >/dev/null 2>&1; then\n" +
    "  exec yad --file --title='Set avatar'\n" +
    "fi\n" +
    "exit 2\n"
  readonly property real barThickness: {
    var n = bar && bar.barSize !== undefined ? Number(bar.barSize) : NaN
    return isFinite(n) && n > 0 ? n : Style.bar.sizeHorizontal
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Behavior on connectionReveal {
    NumberAnimation {
      duration: root.opened ? 160 : 120
      easing.type: root.opened ? Easing.OutCubic : Easing.InCubic
    }
  }

  onOpenedChanged: {
    root.connectionReveal = root.opened ? 1 : 0
    Qt.callLater(root.publishConnectedGeometry)
  }
  onConnectionRevealChanged: root.publishConnectedGeometry()

  Component.onDestruction: {
    if (bar && typeof bar.clearConnectedPanel === "function")
      bar.clearConnectedPanel(root)
  }

  component AvatarPic: Item {
    id: av
    property string path: ""
    property int px: Math.round(Style.font.display * 1.2)
    property bool failed: false
    property bool online: false
    signal clicked()

    implicitWidth: px
    implicitHeight: px
    onPathChanged: av.failed = false

    Rectangle {
      anchors.fill: parent
      radius: width / 2
      clip: true
      color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.08)
      border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.16)
      border.width: 1

      Image {
        anchors.fill: parent
        visible: !av.failed && av.path !== ""
        source: av.path !== "" ? root.localFileUrl(av.path) : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
        onStatusChanged: if (status === Image.Error)
          av.failed = true
      }

      Text {
        anchors.centerIn: parent
        visible: av.failed || av.path === ""
        text: "person"
        color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.72)
        font.family: "Material Symbols Rounded"
        font.pixelSize: Math.round(av.px * 0.64)
        font.variableAxes: ({ "FILL": 0, "wght": 500 })
        renderType: Text.QtRendering
        font.hintingPreference: Font.PreferNoHinting
      }
    }

    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked: av.clicked()
    }

    Rectangle {
      width: Math.max(8, Math.round(av.px * 0.28))
      height: width
      radius: width / 2
      anchors.right: parent.right
      anchors.bottom: parent.bottom
      color: av.online ? "#7dce6a" : Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.35)
      border.color: Color.popups.background
      border.width: 1
    }
  }

  component TokenButton: BorderSurface {
    id: tokenButton
    property string text: ""
    property string iconText: ""
    property string tooltipText: ""
    property bool selected: false
    property bool active: false
    property bool focusable: false
    property bool bordered: false
    property color foreground: root.controlForeground
    property color accent: root.controlAccent
    property string fontFamily: root.fontFamily
    property real fontSize: Style.font.body
    property real iconSize: Style.font.icon
    property string iconFontFamily: fontFamily
    property real horizontalPadding: Style.space(6)
    property real verticalPadding: Style.space(4)
    signal clicked()

    readonly property bool hot: mouseArea.containsMouse
    readonly property color actionColor: accent
    readonly property var normalBorder: bordered
      ? Border.flat(root.controlBorder, 1) : Border.none()
    readonly property var activeBorder: Border.flat(actionColor, 1)

    activeFocusOnTab: focusable
    Keys.onReturnPressed: if (focusable) tokenButton.clicked()
    Keys.onEnterPressed: if (focusable) tokenButton.clicked()
    Keys.onSpacePressed: if (focusable) tokenButton.clicked()

    implicitWidth: row.implicitWidth + horizontalPadding * 2
    implicitHeight: row.implicitHeight + verticalPadding * 2
    radius: root.controlRadius
    color: mouseArea.pressed ? root.controlActiveFill
      : activeFocus || selected || active ? root.controlActiveFill
      : hot ? root.controlHoverFill : root.controlFill
    borderSpec: activeFocus || hot || selected || active ? activeBorder : normalBorder

    Behavior on color { ColorAnimation { duration: 100 } }

    Row {
      id: row
      anchors.centerIn: parent
      spacing: iconText !== "" && text !== "" ? Style.space(5) : 0

      Text {
        visible: tokenButton.iconText !== ""
        text: tokenButton.iconText
        color: tokenButton.selected || tokenButton.hot ? tokenButton.actionColor : tokenButton.foreground
        font.family: tokenButton.iconFontFamily
        font.pixelSize: tokenButton.iconSize
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        visible: tokenButton.text !== ""
        text: tokenButton.text
        color: tokenButton.selected || tokenButton.hot ? tokenButton.actionColor : tokenButton.foreground
        font.family: tokenButton.fontFamily
        font.pixelSize: tokenButton.fontSize
        font.bold: tokenButton.selected
        anchors.verticalCenter: parent.verticalCenter
      }
    }

    MouseArea {
      id: mouseArea
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: {
        if (tokenButton.focusable)
          tokenButton.forceActiveFocus()
        tokenButton.clicked()
      }
    }
  }

  component TokenTextField: TextField {
    foreground: root.controlForeground
    accent: root.controlAccent
    font.family: root.fontFamily
    background: BorderSurface {
      anchors.fill: parent
      color: root.controlFill
      borderSpec: Border.flat(
        parent.activeFocus || parent.hovered ? root.controlAccent : root.controlBorder, 1)
      radius: root.controlRadius
    }
  }

  component ActionButton: TokenButton {
    foreground: root.controlForeground
    accent: root.controlAccent
    fontFamily: root.fontFamily
    radius: root.controlRadius
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
    if (bar && typeof bar.requestPopout === "function") {
      var screenName = popup.screen ? String(popup.screen.name || "") : ""
      bar.requestPopout(root, screenName)
    }
  }

  function close() {
    if (!root.opened)
      return
    root.showJoin = false
    root.moreOpen = false
    root.moreSection = ""
    root.themeOpen = false
    root.nospamConfirm = false
    root.opened = false
    if (bar && typeof bar.releasePopout === "function") {
      var screenName = popup.screen ? String(popup.screen.name || "") : ""
      bar.releasePopout(root, screenName)
    }
    if (bar && typeof bar.clearConnectedPanel === "function")
      bar.clearConnectedPanel(root)
  }

  function toggle() {
    if (root.opened)
      close()
    else
      open()
  }

  function toggleMoreSection(section) {
    if (!root.moreOpen)
      root.moreOpen = true
    root.moreSection = root.moreSection === section ? "" : section
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
    if (code === "avatar_failed")
      return "Avatar image is invalid or larger than 512 KiB."
    if (code === "nickname_invalid")
      return "Nickname must be 1–128 bytes without control characters."
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

  function localFileUrl(path) {
    var parts = String(path || "").split("/")
    var i
    for (i = 0; i < parts.length; i++)
      parts[i] = encodeURIComponent(parts[i])
    return "file://" + parts.join("/")
  }

  function finishAvatarPicker() {
    if (root.avatarPickExitCode < 0 || !root.avatarPickStreamDone)
      return
    var p = String(avatarPickOutput.text || "").trim()
    var restore = root.avatarRestorePending
    var restoreMore = root.avatarRestoreMore
    if (root.avatarPickExitCode === 0 && p !== "")
      omaq.setAvatar(p)
    root.avatarPickExitCode = -1
    root.avatarPickStreamDone = false
    root.avatarRestorePending = false
    if (restore) {
      root.opened = false
      if (bar && typeof bar.releasePopout === "function" && bar.activePopout === root)
        bar.releasePopout(root)
      Qt.callLater(function() {
        root.open()
        root.moreOpen = restoreMore
      })
    }
  }

  function pickSelfAvatar() {
    avatarRestorePending = root.opened
    avatarRestoreMore = root.moreOpen
    avatarPickExitCode = -1
    avatarPickStreamDone = false
    avatarPick.running = false
    avatarPick.running = true
  }

  function openFriend(id, name) {
    if (!id)
      return
    omaq.lastConversation = String(id)
    omaq.lastDirectId = String(id)
    if (chatSurface)
      chatSurface.ensureCard(String(id), name || "")
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

  function publishConnectedGeometry() {
    if (!bar || typeof bar.publishConnectedPanel !== "function")
      return
    var screen = popup.screen
    var name = screen ? String(screen.name || "") : ""
    if (!connectedSurfaceEnabled || !opened || !screen || !name) {
      if (typeof bar.clearConnectedPanel === "function")
        bar.clearConnectedPanel(root, name)
      return
    }
    bar.publishConnectedPanel(root, name, card.x + caretCenterX, connectionReveal)
  }

  function placeCard() {
    var win = button.QsWindow ? button.QsWindow.window : null
    if (!win || !button || !popup.screen)
      return
    var p = button.mapToItem(win.contentItem, 0, 0)
    var gap = connectedSurfaceEnabled ? Style.space(6) : Style.gapsOut
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

  Process {
    id: avatarPick
    running: false
    command: ["bash", "-c", root.avatarPickerScript, "omaq-avatar-picker"]
    stdout: StdioCollector {
      id: avatarPickOutput
      waitForEnd: true
      onStreamFinished: {
        root.avatarPickStreamDone = true
        root.finishAvatarPicker()
      }
    }
    onExited: function(code) {
      root.avatarPickExitCode = code
      root.finishAvatarPicker()
    }
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
          sourceSize.width: width * 2
          sourceSize.height: height * 2
          smooth: true
          mipmap: true
          cache: false
          asynchronous: true
        }

        Rectangle {
          id: unreadBadge
          visible: omaq.unreadCount > 0
          anchors.verticalCenter: parent.verticalCenter
          anchors.verticalCenterOffset: -6
          anchors.horizontalCenter: parent.horizontalCenter
          anchors.horizontalCenterOffset: 7
          width: Math.max(12, unreadBadgeText.implicitWidth + 6)
          height: 12
          radius: height / 2
          color: root.urgent
          border.width: 0
          border.color: "transparent"
          z: 10

          Text {
            id: unreadBadgeText
            anchors.centerIn: parent
            text: omaq.unreadCount > 99 ? "99+" : String(omaq.unreadCount)
            color: root.bar && "background" in root.bar ? root.bar.background : Color.background
            font.family: root.fontFamily
            font.pixelSize: Math.max(7, Style.font.caption - 3)
            font.bold: true
          }
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
        root.publishConnectedGeometry()
        panelFocus.forceActiveFocus()
      })
    }

    MouseArea {
      anchors.fill: parent
      enabled: root.opened
      acceptedButtons: Qt.AllButtons
      onClicked: root.close()
    }

    Item {
      id: connectedSurface
      x: card.x
      y: root.barPos === "bottom" ? card.y : card.y - root.caretDepth
      width: card.width
      height: card.height
      visible: root.connectedSurfaceEnabled && root.opened && root.connectionReveal > 0
      z: 0

      Shape {
        anchors.left: parent.left
        anchors.top: parent.top
        width: parent.width
        height: parent.height + root.caretDepth
        visible: root.barPos !== "bottom"
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
          strokeColor: root.panelBorder
          strokeWidth: root.panelBorderWidth
          fillColor: root.panelBackground
          capStyle: ShapePath.FlatCap
          joinStyle: ShapePath.MiterJoin
          startX: root.panelRadius
          startY: root.caretDepth + 0.5
          PathLine {
            x: root.caretCenterX - root.caretHalfWidth
            y: root.caretDepth + 0.5
          }
          PathCubic {
            x: root.caretCenterX
            y: root.caretDepth - root.caretDepth * root.connectionReveal + 0.5
            control1X: root.caretCenterX - root.caretTangentControl
            control1Y: root.caretDepth + 0.5
            control2X: root.caretCenterX - root.caretTipControl
            control2Y: root.caretDepth - root.caretDepth * root.connectionReveal + 0.5
          }
          PathCubic {
            x: root.caretCenterX + root.caretHalfWidth
            y: root.caretDepth + 0.5
            control1X: root.caretCenterX + root.caretTipControl
            control1Y: root.caretDepth - root.caretDepth * root.connectionReveal + 0.5
            control2X: root.caretCenterX + root.caretTangentControl
            control2Y: root.caretDepth + 0.5
          }
          PathLine {
            x: connectedSurface.width - root.panelRadius
            y: root.caretDepth + 0.5
          }
          PathArc {
            x: connectedSurface.width - 0.5
            y: root.panelRadius + root.caretDepth
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: connectedSurface.width - 0.5
            y: connectedSurface.height + root.caretDepth - 0.5 - root.panelRadius
          }
          PathArc {
            x: connectedSurface.width - root.panelRadius
            y: connectedSurface.height + root.caretDepth - 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: root.panelRadius
            y: connectedSurface.height + root.caretDepth - 0.5
          }
          PathArc {
            x: 0.5
            y: connectedSurface.height + root.caretDepth - 0.5 - root.panelRadius
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: 0.5
            y: root.panelRadius + root.caretDepth
          }
          PathArc {
            x: root.panelRadius
            y: root.caretDepth + 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
        }
      }

      Shape {
        anchors.left: parent.left
        anchors.top: parent.top
        width: parent.width
        height: parent.height + root.caretDepth
        visible: root.barPos === "bottom"
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
          strokeColor: root.panelBorder
          strokeWidth: root.panelBorderWidth
          fillColor: root.panelBackground
          capStyle: ShapePath.FlatCap
          joinStyle: ShapePath.MiterJoin
          startX: root.panelRadius
          startY: 0.5
          PathLine {
            x: connectedSurface.width - root.panelRadius
            y: 0.5
          }
          PathArc {
            x: connectedSurface.width - 0.5
            y: root.panelRadius + 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: connectedSurface.width - 0.5
            y: connectedSurface.height - root.panelRadius - 0.5
          }
          PathArc {
            x: connectedSurface.width - root.panelRadius
            y: connectedSurface.height - 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: root.caretCenterX + root.caretHalfWidth
            y: connectedSurface.height - 0.5
          }
          PathCubic {
            x: root.caretCenterX
            y: connectedSurface.height - 0.5 + root.caretDepth * root.connectionReveal
            control1X: root.caretCenterX + root.caretTangentControl
            control1Y: connectedSurface.height - 0.5
            control2X: root.caretCenterX + root.caretTipControl
            control2Y: connectedSurface.height - 0.5 + root.caretDepth * root.connectionReveal
          }
          PathCubic {
            x: root.caretCenterX - root.caretHalfWidth
            y: connectedSurface.height - 0.5
            control1X: root.caretCenterX - root.caretTipControl
            control1Y: connectedSurface.height - 0.5 + root.caretDepth * root.connectionReveal
            control2X: root.caretCenterX - root.caretTangentControl
            control2Y: connectedSurface.height - 0.5
          }
          PathLine {
            x: root.panelRadius
            y: connectedSurface.height - 0.5
          }
          PathArc {
            x: 0.5
            y: connectedSurface.height - root.panelRadius - 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
          PathLine {
            x: 0.5
            y: root.panelRadius + 0.5
          }
          PathArc {
            x: root.panelRadius
            y: 0.5
            radiusX: root.panelRadius
            radiusY: root.panelRadius
          }
        }
      }
    }

    BorderSurface {
      id: card
      width: root.cardWidth
      height: Math.min(column.implicitHeight + root.pad * 2,
                       popup.screen ? Math.max(Style.space(260), popup.screen.height - Style.space(24)) : Style.space(720))
      color: root.connectedSurfaceEnabled ? "transparent" : root.panelBackground
      borderSpec: root.connectedSurfaceEnabled
        ? Border.flat("transparent", 0) : Border.flat(root.panelBorder, root.panelBorderWidth)
      radius: root.panelRadius

      onXChanged: root.publishConnectedGeometry()
      onWidthChanged: root.publishConnectedGeometry()
      onHeightChanged: {
        if (root.opened)
          root.placeCard()
        root.publishConnectedGeometry()
      }

      MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
      }

      FocusScope {
        id: panelFocus
        anchors.fill: parent
        focus: root.opened
        Keys.onEscapePressed: root.close()

        Flickable {
          id: panelScroll
          anchors.fill: parent
          anchors.margins: root.pad
          contentWidth: width
          contentHeight: column.implicitHeight
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.VerticalFlick

          Column {
            id: column
            width: panelScroll.width
            spacing: Style.space(12)

          Item {
            id: heroRow
            width: parent.width
            implicitHeight: heroVisual.height

            Item {
              id: heroVisual
              width: parent.width
              height: Style.space(48)
              clip: true
              property real logoPulse: 0

              Image {
                width: Math.min(parent.width - Style.space(32), Style.space(38) * 751 / 230)
                height: width * 230 / 751
                anchors.centerIn: parent
                source: Qt.resolvedUrl("assets/OmaQ_Final-panel.png")
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 1502
                sourceSize.height: 460
                opacity: 0.93 + heroVisual.logoPulse * 0.07
                scale: 1 + heroVisual.logoPulse * 0.008
                transformOrigin: Item.Center
                smooth: true
                mipmap: false
                cache: false
                asynchronous: true
              }

              SequentialAnimation on logoPulse {
                loops: Animation.Infinite
                NumberAnimation { to: 1; duration: 1400; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0; duration: 1400; easing.type: Easing.InOutSine }
                PauseAnimation { duration: 1800 }
              }

              MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                focus: true
                activeFocusOnTab: true
                Keys.onReturnPressed: root.openRepo()
                Keys.onEnterPressed: root.openRepo()
                onClicked: root.openRepo()
              }
            }
          }

          GridLayout {
            id: heroActions
            width: parent.width
            columns: 2
            columnSpacing: root.btnGap
            rowSpacing: Style.space(6)

            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              iconText: "󰐲"
              text: "Invite"
              selected: root.inviteOpen
              onClicked: root.toggleInvite()
            }
            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              iconText: "󰌆"
              text: "Join"
              selected: root.showJoin
              onClicked: root.showJoin = !root.showJoin
            }
            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              iconText: "󰍩"
              text: "Chat"
              onClicked: root.openChat()
            }
            ActionButton {
              Layout.fillWidth: true
              iconText: "󰙨"
              text: "Demo"
              selected: chatSurface && chatSurface.demoOpen
              onClicked: root.openDemo()
            }
            ActionButton {
              Layout.fillWidth: true
              iconText: "󰏘"
              text: "Theme"
              selected: root.themeOpen
              accent: root.chatTheme === "system" ? Color.accent : Model.themeFor(root.chatTheme).accent
              onClicked: root.themeOpen = !root.themeOpen
            }
          }

          Column {
            width: parent.width
            spacing: Style.space(6)

            Text {
              text: "YOU"
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
            }

            Row {
              spacing: Style.space(8)
              AvatarPic {
                path: omaq.selfAvatar
                online: omaq.selfOnline
                onClicked: root.pickSelfAvatar()
              }
              Column {
                y: (parent.height - height) / 2
                spacing: 0
                Text {
                  visible: omaq.selfNickname !== "" && !root.nicknameEditOpen
                  text: omaq.selfNickname
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                  elide: Text.ElideRight
                  MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                      root.nicknameEditOpen = true
                      Qt.callLater(function() { nicknameField.forceActiveFocus() })
                    }
                  }
                }
                Text {
                  text: omaq.selfOnline ? "Online" : "Offline"
                  color: omaq.selfOnline ? root.foreground : root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                }
                Text {
                  text: "Set avatar"
                  color: root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                  MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pickSelfAvatar()
                  }
                }
              }
            }

            Row {
              visible: omaq.selfNickname === "" || root.nicknameEditOpen
              width: parent.width
              spacing: root.btnGap
              TokenTextField {
                id: nicknameField
                width: parent.width - nicknameButton.implicitWidth - root.btnGap
                foreground: root.controlForeground
                placeholderText: "Set your Nickname"
                maximumLength: 128
                text: omaq.selfNickname
                onAccepted: nicknameButton.clicked()
              }
              TokenButton {
                id: nicknameButton
                iconText: "check"
                iconFontFamily: "Material Symbols Rounded"
                text: "Set"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: nicknameField.text.trim() !== ""
                onClicked: {
                  root.nicknameSubmitPending = omaq.setNickname(nicknameField.text)
                }
              }
            }

            Connections {
              target: omaq
              function onSelfNicknameChanged() {
                nicknameField.text = omaq.selfNickname
                if (root.nicknameSubmitPending && omaq.selfNickname !== "") {
                  root.nicknameSubmitPending = false
                  root.nicknameEditOpen = false
                }
              }
              function onNicknameTickChanged() {
                if (root.nicknameSubmitPending) {
                  root.nicknameSubmitPending = false
                  root.nicknameEditOpen = false
                }
              }
              function onLastErrorTickChanged() {
                if (root.nicknameSubmitPending && omaq.lastError === "nickname_invalid")
                  root.nicknameSubmitPending = false
              }
            }

            Text {
              visible: omaq.friends && omaq.friends.length > 0
              text: "FRIENDS"
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
            }

            Repeater {
              model: omaq.friends
              Row {
                required property var modelData
                spacing: Style.space(8)
                width: parent ? parent.width : 0

                AvatarPic {
                  path: modelData && modelData.avatar ? String(modelData.avatar) : ""
                  online: !!(modelData && modelData.online)
                  onClicked: root.openFriend(modelData ? modelData.id : "", modelData ? modelData.name : "")
                }

                ActionButton {
                  text: {
                    var name = (modelData && modelData.name) ? String(modelData.name) : ("Friend " + (modelData ? modelData.id : ""))
                    return name + (modelData && modelData.online ? " · online" : " · offline")
                  }
                  onClicked: root.openFriend(modelData ? modelData.id : "", modelData ? modelData.name : "")
                }
              }
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

            TokenTextField {
              id: unlockField
              width: parent.width
              foreground: root.controlForeground
              password: true
              placeholderText: "Passphrase"
            }

            TokenButton {
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
                  TokenButton {
                    text: "Accept"
                    bordered: true
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: omaq.decide(true)
                  }
                  TokenButton {
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
              TokenButton {
                text: "Answer"
                bordered: true
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.answerCall(omaq.lastCallConv)
              }
              TokenButton {
                text: "Decline call"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.stopCall(omaq.lastCallConv)
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
                TokenButton {
                  text: root.copied ? "Copied" : "Copy link"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: root.copyInvite()
                }
                TokenButton {
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

              TokenTextField {
                id: redeemField
                width: parent.width
                foreground: root.controlForeground
                placeholderText: "Paste omaq:// invite"
                text: root.redeemDraft
                onTextChanged: root.redeemDraft = text
                onAccepted: joinBtn.clicked()
              }

              TokenButton {
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
              visible: omaq.safetyCode !== ""
              foreground: root.foreground
            }

            TokenButton {
              text: root.moreOpen ? "Less" : "More"
              focusable: true
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: {
                root.moreOpen = !root.moreOpen
                if (root.moreOpen && root.moreSection === "")
                  root.moreSection = "chat"
                else if (!root.moreOpen)
                  root.moreSection = ""
              }
            }

            Column {
              visible: root.moreOpen
              width: parent.width
              spacing: Style.space(8)

              GridLayout {
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)

                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰍉"
                  text: "Search"
                  selected: root.moreSection === "chat"
                  onClicked: root.toggleMoreSection("chat")
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰘉"
                  text: "Groups"
                  selected: root.moreSection === "groups"
                  onClicked: root.toggleMoreSection("groups")
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰈙"
                  text: "Identity"
                  selected: root.moreSection === "identity"
                  onClicked: root.toggleMoreSection("identity")
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰀦"
                  text: "Danger"
                  selected: root.moreSection === "danger"
                  accent: root.urgent
                  onClicked: root.toggleMoreSection("danger")
                }
              }

              PanelSectionHeader {
                visible: root.moreSection === "chat"
                text: "CHAT"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Row {
                visible: root.moreSection === "chat"
                width: parent.width
                spacing: root.btnGap
                TokenTextField {
                  id: searchField
                  width: parent.width - searchBtn.implicitWidth - root.btnGap
                  foreground: root.controlForeground
                  placeholderText: "Search this chat"
                  onAccepted: omaq.searchChat(searchField.text)
                  onTextChanged: if (!text) omaq.searchItems = []
                }
                TokenButton {
                  id: searchBtn
                  iconText: "󰍉"
                  text: "Search"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.searchChat(searchField.text)
                }
              }

              Column {
                visible: root.moreSection === "chat" && omaq.searchItems && omaq.searchItems.length > 0
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: omaq.searchItems
                  delegate: Text {
                    required property var modelData
                    width: parent ? parent.width : 0
                    text: modelData && modelData.text ? String(modelData.text) : ""
                    color: root.dim
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                  }
                }
              }

              ActionButton {
                visible: root.moreSection === "chat" && omaq.safetyCode === "" && omaq.lastDirectId !== ""
                width: parent.width
                iconText: "󰌾"
                text: "Show safety code"
                onClicked: omaq.getSafety()
              }

              PanelSeparator {
                visible: root.moreSection === "chat" &&
                  (omaq.safetyCode !== "" || omaq.lastDirectId !== "")
                foreground: root.foreground
              }

              PanelSectionHeader {
                visible: root.moreSection === "groups"
                text: "GROUP"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              GridLayout {
                visible: root.moreSection === "groups"
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)
                TokenButton {
                  Layout.fillWidth: true
                  text: "Create group"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.createGroup()
                }
                TokenButton {
                  visible: omaq.lastGroup !== "" && omaq.lastDirectId !== ""
                  Layout.fillWidth: true
                  text: "Invite last contact"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: {
                    omaq.inviteUrl = ""
                    omaq.qrPath = ""
                    root.inviteOpen = true
                    omaq.inviteToGroup()
                  }
                }
                TokenButton {
                  visible: omaq.lastGroup !== "" && omaq.lastDirectId !== ""
                  Layout.fillWidth: true
                  text: "Make admin"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.setLastGroupMemberRole("admin")
                }
                TokenButton {
                  visible: omaq.lastGroup !== "" && omaq.lastDirectId !== ""
                  Layout.fillWidth: true
                  text: "Make member"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.setLastGroupMemberRole("member")
                }
                TokenButton {
                  visible: omaq.lastGroup !== "" && omaq.lastDirectId !== ""
                  Layout.fillWidth: true
                  text: "Remove last member"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.removeLastGroupMember()
                }
                TokenButton {
                  visible: omaq.lastGroup !== ""
                  Layout.fillWidth: true
                  text: "Leave group"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.leaveGroup()
                }
              }

              Text {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                width: parent.width
                text: omaq.lastGroup
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              TokenButton {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                text: "Dissolve group"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: omaq.dissolveGroup()
              }

              PanelSeparator {
                visible: root.moreSection === "groups"
                foreground: root.foreground
              }

              PanelSectionHeader {
                visible: root.moreSection === "identity"
                text: "IDENTITY"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Text {
                visible: root.moreSection === "identity" && !omaq.saveProtected
                width: parent.width
                text: "Passphrase not set"
                color: root.systemColors[1] || root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
              }

              TokenTextField {
                id: passField
                visible: root.moreSection === "identity"
                width: parent.width
                foreground: root.controlForeground
                password: true
                placeholderText: "Passphrase for identity file"
              }

              GridLayout {
                visible: root.moreSection === "identity"
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)
                TokenButton {
                  visible: !omaq.saveProtected
                  Layout.fillWidth: true
                  iconText: "󰌾"
                  text: "Protect"
                  bordered: true
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.protectIdentity(passField.text)
                }
                TokenButton {
                  visible: omaq.saveProtected
                  Layout.fillWidth: true
                  iconText: "󰌿"
                  text: "Remove lock"
                  focusable: true
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                  onClicked: omaq.unprotectIdentity(passField.text)
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰈉"
                  text: "Export"
                  onClicked: omaq.exportIdentity()
                }
              }

              TokenTextField {
                id: importPath
                visible: root.moreSection === "identity"
                width: parent.width
                foreground: root.controlForeground
                placeholderText: "Path to identity file"
              }

              Row {
                visible: root.moreSection === "identity"
                width: parent.width
                spacing: root.btnGap
                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  iconText: "󰏘"
                  text: "Import"
                  onClicked: {
                    if (importPath.text)
                      omaq.importIdentity(importPath.text, false)
                  }
                }
                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  iconText: "󰁨"
                  text: "Replace"
                  accent: root.urgent
                  onClicked: {
                    if (importPath.text)
                      omaq.importIdentity(importPath.text, true)
                  }
                }
              }

              PanelSeparator {
                visible: root.moreSection === "identity"
                foreground: root.foreground
              }

              PanelSectionHeader {
                visible: root.moreSection === "danger"
                text: "DANGER"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              GridLayout {
                visible: root.moreSection === "danger"
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)

                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰆴"
                  text: "Remove contact"
                  onClicked: omaq.removeContact()
                }

                ActionButton {
                  visible: !root.nospamConfirm
                  Layout.fillWidth: true
                  iconText: "󰒭"
                  text: "Rotate personal ID"
                  onClicked: root.nospamConfirm = true
                }
              }

              Text {
                visible: root.moreSection === "danger" && root.nospamConfirm
                width: parent.width
                text: "This voids every open invite."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              GridLayout {
                visible: root.moreSection === "danger" && root.nospamConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)
                ActionButton {
                  Layout.fillWidth: true
                  text: "Cancel"
                  onClicked: root.nospamConfirm = false
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰒭"
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
  }
}
}
