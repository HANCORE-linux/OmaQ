import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls as Controls
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
  property bool removeContactConfirm: false
  property bool replaceIdentityConfirm: false
  property string replaceIdentityPath: ""
  property bool showJoin: false
  property bool chatPickerOpen: false
  property bool inviteOpen: false
  property bool moreOpen: false
  property string moreSection: ""
  property bool settingsOpen: false
  property bool themeOpen: false
  property bool soundOpen: false
  property bool copied: false
  property bool safetyCodeVisible: false
  property bool safetyCopied: false
  property bool nicknameEditOpen: false
  property bool nicknameSubmitPending: false
  property bool avatarRestorePending: false
  property bool avatarRestoreMore: false
  property string groupInviteFriendId: ""
  property string groupInviteFeedback: ""
  property bool groupLeaveConfirm: false
  property string groupLeaveTarget: ""
  property bool groupDissolveConfirm: false
  property string groupDissolveTarget: ""
  property int friendPage: 0
  property int avatarPickExitCode: -1
  property bool avatarPickStreamDone: false
  property var systemColors: ["#101315", "#565d60", "#9fa5a9", "#d9dbdc", "#798186", "#aeaeae", "#707070", "#cbc2be"]
  property string systemThemeName: "System"
  readonly property var notificationSounds: [
    { id: "off", label: "Off" },
    { id: "icq-message", label: "ICQ" },
    { id: "qq", label: "QQ" },
    { id: "wechat", label: "WeChat" },
    { id: "skype", label: "Skype" },
    { id: "msn", label: "MSN" },
    { id: "aurora", label: "Aurora" },
    { id: "crystal", label: "Crystal" },
    { id: "ripple", label: "Ripple" },
    { id: "glow", label: "Glow" },
    { id: "halo", label: "Halo" },
    { id: "click", label: "Click" },
    { id: "pop", label: "Pop" },
    { id: "bell", label: "Bell" },
    { id: "soft", label: "Soft" },
    { id: "knock", label: "Knock" }
  ]
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
  readonly property real nicknameControlHeight: Style.space(28)
  readonly property int friendPageCount: Math.max(1,
    Math.ceil((omaq.friends ? omaq.friends.length : 0) / 30))
  readonly property var friendPageItems: (omaq.friends || []).slice(
    friendPage * 30, friendPage * 30 + 30)
  readonly property int friendColumnCount: Math.min(3, Math.max(1,
    Math.ceil(Math.max(1, friendPageItems.length) / 10)))
  readonly property int cardWidth: Style.space(340 + (friendColumnCount - 1) * 140)
  readonly property real railIconWidth: Style.space(34)
  readonly property real railWidth: railIconWidth * 2
  readonly property real actionButtonHeight: Style.space(36)
  readonly property bool primaryMenuOpen: root.inviteOpen || root.showJoin ||
    root.chatPickerOpen || root.settingsOpen || root.moreOpen || omaq.pending
  readonly property int visibleUnreadCount: Math.max(omaq.unreadCount, omaq.localUnreadTotal())
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

  onFriendPageCountChanged: friendPage = Math.min(friendPage, friendPageCount - 1)

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
    property int unreadCount: 0
    property bool badgeEnabled: true
    property int revision: 0
    signal clicked()

    implicitWidth: px
    implicitHeight: px
    onPathChanged: av.failed = false
    onRevisionChanged: av.failed = false

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
        source: av.path !== "" ? root.localFileUrl(av.path) + "?v=" + av.revision : ""
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

    Rectangle {
      visible: av.badgeEnabled && av.unreadCount > 0
      width: Math.max(Style.space(14), unreadText.implicitWidth + Style.space(6))
      height: Style.space(14)
      radius: height / 2
      anchors.right: parent.right
      anchors.top: parent.top
      color: root.urgent
      border.color: Color.popups.background
      border.width: 1

      Text {
        id: unreadText
        anchors.centerIn: parent
        text: av.unreadCount > 99 ? "99+" : String(av.unreadCount)
        color: Color.popups.background
        font.family: root.fontFamily
        font.pixelSize: Style.font.caption
        font.bold: true
      }
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
    implicitHeight: Math.max(root.actionButtonHeight,
                             row.implicitHeight + verticalPadding * 2)
    radius: root.controlRadius
    color: mouseArea.pressed ? root.controlActiveFill
      : selected || active ? root.controlActiveFill
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
        Qt.callLater(function() {
          if (root.opened)
            panelFocus.forceActiveFocus()
        })
      }
    }
  }

  component RailIcon: Item {
    id: railIcon
    property string materialIcon: ""
    property string label: ""
    property bool selected: false
    property color activeColor: root.systemColors[3] || root.controlAccent
    signal clicked()

    implicitWidth: root.railIconWidth
    implicitHeight: Style.space(34)
    opacity: enabled ? 1 : 0.35
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: if (enabled) railIcon.clicked()
    Keys.onReturnPressed: if (enabled) railIcon.clicked()
    Keys.onEnterPressed: if (enabled) railIcon.clicked()
    Keys.onSpacePressed: if (enabled) railIcon.clicked()

    Text {
      anchors.centerIn: parent
      text: railIcon.materialIcon
      color: railIcon.selected || railHover.hovered || railIcon.activeFocus
        ? railIcon.activeColor : root.dim
      font.family: "Material Symbols Rounded"
      font.pixelSize: Style.font.icon + Style.space(5)
      font.variableAxes: ({ "FILL": railIcon.selected ? 1 : 0, "wght": 500 })
      renderType: Text.QtRendering
      font.hintingPreference: Font.PreferNoHinting
    }

    HoverHandler { id: railHover; enabled: railIcon.enabled }
    Controls.ToolTip {
      id: railTooltip
      visible: (railHover.hovered || railIcon.activeFocus) && railIcon.label !== ""
      text: railIcon.label
      delay: 450
      timeout: 2600
      padding: Style.space(5)
      background: Rectangle {
        radius: Style.cornerRadius
        color: Qt.darker(root.panelBackground, 1.08)
        border.color: Qt.rgba(root.foreground.r, root.foreground.g,
                              root.foreground.b, 0.24)
        border.width: 1
      }
      contentItem: Text {
        text: railTooltip.text
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        renderType: Text.QtRendering
      }
    }
    MouseArea {
      anchors.fill: parent
      enabled: railIcon.enabled
      cursorShape: Qt.PointingHandCursor
      onClicked: {
        railIcon.clicked()
        Qt.callLater(function() {
          if (root.opened)
            panelFocus.forceActiveFocus()
        })
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
    implicitHeight: root.actionButtonHeight
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
    root.chatPickerOpen = false
    root.inviteOpen = false
    root.moreOpen = false
    root.moreSection = ""
    root.settingsOpen = false
    root.themeOpen = false
    root.soundOpen = false
    root.safetyCodeVisible = false
    root.safetyCopied = false
    root.copied = false
    root.nospamConfirm = false
    root.removeContactConfirm = false
    root.replaceIdentityConfirm = false
    root.replaceIdentityPath = ""
    root.nicknameEditOpen = false
    root.nicknameSubmitPending = false
    root.groupInviteFriendId = ""
    root.groupInviteFeedback = ""
    root.groupLeaveConfirm = false
    root.groupLeaveTarget = ""
    root.groupDissolveConfirm = false
    root.groupDissolveTarget = ""
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

  function dismissTransientSections() {
    root.inviteOpen = false
    root.showJoin = false
    root.chatPickerOpen = false
    root.settingsOpen = false
    root.themeOpen = false
    root.soundOpen = false
    root.moreOpen = false
    root.moreSection = ""
    root.safetyCodeVisible = false
    root.safetyCopied = false
    root.copied = false
    root.nicknameEditOpen = false
    root.nicknameSubmitPending = false
    root.nospamConfirm = false
    root.removeContactConfirm = false
    root.replaceIdentityConfirm = false
    root.replaceIdentityPath = ""
    root.groupInviteFriendId = ""
    root.groupInviteFeedback = ""
    root.groupLeaveConfirm = false
    root.groupLeaveTarget = ""
    root.groupDissolveConfirm = false
    root.groupDissolveTarget = ""
  }

  function toggleSettings() {
    var open = !root.settingsOpen
    root.dismissTransientSections()
    root.settingsOpen = open
  }

  function toggleAdvanced() {
    var open = !root.moreOpen
    root.dismissTransientSections()
    root.moreOpen = open
    root.moreSection = open ? "chat" : ""
  }

  function toggleMoreSection(section) {
    if (!root.moreOpen)
      root.moreOpen = true
    root.moreSection = root.moreSection === section ? "" : section
    if (root.moreSection !== "danger") {
      root.nospamConfirm = false
      root.removeContactConfirm = false
    }
    if (root.moreSection !== "identity") {
      root.replaceIdentityConfirm = false
      root.replaceIdentityPath = ""
    }
  }

  function errorText(code) {
    if (code === "locked")
      return "Unlock your identity to continue."
    if (code === "unsupported")
      return "That is not an OmaQ invite."
    if (code === "helper_down")
      return "OmaQ is restarting."
    if (code === "helper_incompatible")
      return "OmaQ helper needs to be restarted."
    if (code === "helper_handshake_pending")
      return "OmaQ is still restoring queued events."
    if (code === "helper_event_overflow")
      return "Some queued live events were compacted; history was resynchronized."
    if (code === "file_failed")
      return "File transfer failed."
    if (code === "avatar_failed")
      return "Avatar image is invalid or larger than 512 KiB."
    if (code === "nickname_invalid")
      return "Nickname must be 1–128 bytes without control characters."
    if (code === "identity_changed")
      return "Identity changed. Pending actions were discarded."
    if (code === "identity_backup_failed")
      return "Could not create a safe identity backup."
    if (code === "identity_backup_cleanup_failed")
      return "An old identity recovery backup could not be removed."
    if (code === "identity_passphrase_required")
      return "Enter the imported identity's passphrase first."
    if (code === "identity_import_failed")
      return "Identity file or passphrase is invalid."
    if (code === "identity_state_archive_failed")
      return "Could not archive the previous identity's local data."
    if (code === "identity_rollback_failed")
      return "Identity restore failed. The backup was kept in the OmaQ data folder."
    return code
  }

  function connectionLabel() {
    if (omaq.connectionState === "online")
      return "Online"
    if (omaq.connectionState === "locked")
      return "Locked"
    if (omaq.connectionState === "reconnecting")
      return "Reconnecting…"
    return "Connecting…"
  }

  function contactStatus(online) {
    if (omaq.connectionState !== "online")
      return omaq.connectionState === "reconnecting" ? "reconnecting…" : "connecting…"
    return online ? "online" : "offline"
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

  function copySafetyCode() {
    if (!omaq.safetyCode)
      return
    Quickshell.execDetached([
      "bash", "-c",
      "if command -v wl-copy >/dev/null 2>&1; then printf '%s' \"$1\" | wl-copy -n; elif command -v xclip >/dev/null 2>&1; then printf '%s' \"$1\" | xclip -selection clipboard; fi",
      "omaq-copy-safety", omaq.safetyCode
    ])
    root.safetyCopied = true
    safetyCopiedTimer.restart()
  }

  function showSafetyCode() {
    root.safetyCodeVisible = true
    omaq.getSafety()
  }

  function hideSafetyCode() {
    root.safetyCodeVisible = false
    root.safetyCopied = false
  }

  function toggleInvite() {
    var open = !root.inviteOpen
    root.dismissTransientSections()
    root.inviteOpen = open
    if (open && !omaq.inviteUrl)
      omaq.createInvite()
  }

  function toggleJoin() {
    var open = !root.showJoin
    root.dismissTransientSections()
    root.showJoin = open
  }

  function openChat() {
    var open = !root.chatPickerOpen
    root.dismissTransientSections()
    root.chatPickerOpen = open
    if (open)
      omaq.sendOp({ op: "status" })
  }

  function openRailAdvanced(section) {
    var key = String(section || "chat")
    var open = !(root.moreOpen && root.moreSection === key)
    root.dismissTransientSections()
    if (open) {
      root.moreOpen = true
      root.moreSection = key
    }
  }

  function openRailTheme() {
    var open = !(root.settingsOpen && root.themeOpen)
    root.dismissTransientSections()
    root.settingsOpen = open
    root.themeOpen = open
  }

  function openRailSounds() {
    var open = !(root.settingsOpen && root.soundOpen)
    root.dismissTransientSections()
    root.settingsOpen = open
    root.soundOpen = open
  }

  function friendStatus(friend) {
    if (omaq.connectionState !== "online")
      return omaq.connectionState === "reconnecting" ? "reconnecting…" : "connecting…"
    var status = String(friend && friend.status || "")
    if (status === "afk")
      return "afk"
    return friend && friend.online ? "online" : "offline"
  }

  function friendStatusColor(friend) {
    var status = root.friendStatus(friend)
    if (status === "online")
      return root.systemColors[4] || root.foreground
    if (status === "afk")
      return root.systemColors[3] || root.controlAccent
    return root.systemColors[1] || root.dim
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

  function friendName(id) {
    var key = String(id || "")
    var friends = omaq.friends || []
    for (var i = 0; i < friends.length; i++)
      if (String(friends[i].id || "") === key)
        return String(friends[i].name || ("Friend " + key))
    return key ? "Friend " + key : ""
  }

  function openFriend(id, name) {
    if (!id)
      return
    omaq.lastConversation = String(id)
    omaq.lastDirectId = String(id)
    omaq.clearUnread(String(id))
    if (chatSurface) {
      chatSurface.ensureCard(String(id), name || "")
      chatSurface.requestChatFocus(String(id))
    }
    root.close()
  }

  function openGroup(id) {
    var groupId = String(id || "")
    if (!omaq.selectGroup(groupId))
      return
    omaq.clearUnread(groupId)
    if (chatSurface) {
      chatSurface.ensureCard(groupId, omaq.groupName(groupId))
      chatSurface.requestChatFocus(groupId)
    }
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

  readonly property string notificationSound: {
    var value = root.settings && root.settings.sound
    return value ? String(value) : "icq-message"
  }

  function setNotificationSound(name) {
    var selectedSound = String(name || "off")
    root.persistSettings({ sound: selectedSound })
    if (chatSurface)
      chatSurface.previewSound(selectedSound)
  }

  function toggleThemeSettings() {
    root.themeOpen = !root.themeOpen
    if (root.themeOpen)
      root.soundOpen = false
  }

  function toggleSoundSettings() {
    root.soundOpen = !root.soundOpen
    if (root.soundOpen)
      root.themeOpen = false
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
    onFormatToolbarToggled: function(enabled) {
      root.persistSettings({ formatToolbar: enabled })
    }
  }

  Timer {
    id: copiedTimer
    interval: 1400
    onTriggered: root.copied = false
  }

  Timer {
    id: safetyCopiedTimer
    interval: 1400
    onTriggered: root.safetyCopied = false
  }

  Connections {
    target: omaq
    function onInviteUrlChanged() {
      if (omaq.inviteUrl !== "")
        omaq.saveQr()
    }
    function onGroupInviteSentTickChanged() {
      if (String(omaq.lastGroupInviteSentGroup || "") === String(omaq.lastGroup || "") &&
          String(omaq.lastGroupInviteSentFriend || "") === root.groupInviteFriendId)
        root.groupInviteFeedback = "Group invite sent to " +
          root.friendName(root.groupInviteFriendId)
    }
    function onGroupInviteFailedTickChanged() {
      if (String(omaq.lastGroupInviteFailedGroup || "") === String(omaq.lastGroup || "") &&
          String(omaq.lastGroupInviteFailedFriend || "") === root.groupInviteFriendId)
        root.groupInviteFeedback = omaq.lastGroupInviteFailedCode === "busy"
          ? "Recipient is handling another group invite"
          : "Group invite failed"
    }
    function onLastErrorTickChanged() {
      if (root.groupInviteFeedback === "Sending group invite…")
        root.groupInviteFeedback = "Group invite failed"
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
    function unread(): string { return String(root.visibleUnreadCount) }
  }

  BarIconButton {
    id: button
    property real callPulseOpacity: 1.0
    anchors.fill: parent
    bar: root.bar
    text: omaq.incomingCall ? "call" : (omaq.pending ? "" : "󰭹")
    fontFamily: omaq.incomingCall ? "Material Symbols Rounded" : "monospace"
    active: omaq.incomingCall || omaq.pending
    activeColor: omaq.pending && !omaq.incomingCall
      ? (root.systemColors[1] || root.urgent)
      : (root.systemColors[1] || root.urgent)
    opacity: omaq.incomingCall ? callPulseOpacity : 1.0
    tooltipText: omaq.incomingCall
      ? "Incoming call from " + root.friendName(omaq.lastCallConv)
      : (omaq.pending
        ? (omaq.pendingGroup ? "Group invite received" : "Friend request received")
        : "OmaQ")
    onPressed: function(b) {
      if (b === Qt.RightButton)
        return
      if (omaq.incomingCall && omaq.lastCallConv) {
        root.openFriend(omaq.lastCallConv, root.friendName(omaq.lastCallConv))
        return
      }
      root.toggle()
    }

    SequentialAnimation on callPulseOpacity {
      running: omaq.incomingCall
      loops: Animation.Infinite
      NumberAnimation { to: 0.38; duration: 520; easing.type: Easing.InOutSine }
      NumberAnimation { to: 1.0; duration: 520; easing.type: Easing.InOutSine }
      onRunningChanged: if (!running) button.callPulseOpacity = 1.0
    }
  }

  Rectangle {
    id: unreadBadge
    visible: root.visibleUnreadCount > 0 &&
      (!root.settings || root.settings.notifyBadge !== false)
    width: Math.max(Style.space(12), unreadBadgeText.implicitWidth + Style.space(6))
    height: Style.space(12)
    radius: height / 2
    color: root.urgent
    border.width: 0
    anchors.verticalCenter: button.verticalCenter
    anchors.verticalCenterOffset: -Style.space(6)
    anchors.horizontalCenter: button.horizontalCenter
    anchors.horizontalCenterOffset: Style.space(7)
    z: 100

    Text {
      id: unreadBadgeText
      anchors.centerIn: parent
      text: root.visibleUnreadCount > 99 ? "99" : String(root.visibleUnreadCount)
      color: root.bar && "background" in root.bar ? root.bar.background : Color.background
      font.family: root.fontFamily
      font.pixelSize: Style.space(7)
      font.bold: true
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
      height: Math.min(Math.max(column.implicitHeight,
                                actionRail.implicitHeight + Style.space(52)) + root.pad * 2,
                       popup.screen ? Math.max(Style.space(260), popup.screen.height - Style.space(24)) : Style.space(720))
      color: root.connectedSurfaceEnabled ? "transparent" : root.panelBackground
      borderSpec: root.connectedSurfaceEnabled
        ? Border.flat("transparent", 0) : Border.flat(root.panelBorder, root.panelBorderWidth)
      radius: root.panelRadius

      onXChanged: root.publishConnectedGeometry()
      onWidthChanged: {
        if (root.opened)
          root.placeCard()
        root.publishConnectedGeometry()
      }
      onHeightChanged: {
        if (root.opened)
          root.placeCard()
        root.publishConnectedGeometry()
      }

      MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: root.dismissTransientSections()
      }

      FocusScope {
        id: panelFocus
        anchors.fill: parent
        focus: root.opened
        Keys.onEscapePressed: root.close()

        Row {
          id: actionRail
          anchors.top: parent.top
          anchors.topMargin: root.pad + Style.space(48)
          anchors.right: parent.right
          spacing: 0
          z: 20

          Column {
            spacing: Style.space(2)

            RailIcon {
              visible: !omaq.locked
              materialIcon: "qr_code_2"
              label: "Invite"
              selected: root.inviteOpen
              onClicked: root.toggleInvite()
            }
            RailIcon {
              visible: !omaq.locked
              materialIcon: "person_add"
              label: "Add contact"
              selected: root.showJoin
              onClicked: root.toggleJoin()
            }
            RailIcon {
              visible: !omaq.locked
              materialIcon: "chat"
              label: "Open chat"
              selected: root.chatPickerOpen
              onClicked: root.openChat()
            }
            RailIcon {
              visible: !omaq.locked
              materialIcon: "groups"
              label: "Groups"
              selected: root.moreOpen && root.moreSection === "groups"
              onClicked: root.openRailAdvanced("groups")
            }
            RailIcon {
              materialIcon: "search"
              label: "Search and safety"
              selected: root.moreOpen && root.moreSection === "chat"
              onClicked: root.openRailAdvanced("chat")
            }
            RailIcon {
              materialIcon: "badge"
              label: "Identity"
              selected: root.moreOpen && root.moreSection === "identity"
              onClicked: root.openRailAdvanced("identity")
            }
          }

          Column {
            spacing: Style.space(2)

            RailIcon {
              materialIcon: "palette"
              label: "Theme"
              selected: root.settingsOpen && root.themeOpen
              onClicked: root.openRailTheme()
            }
            RailIcon {
              materialIcon: "music_note"
              label: "Sounds"
              selected: root.settingsOpen && root.soundOpen
              onClicked: root.openRailSounds()
            }
            RailIcon {
              materialIcon: "science"
              label: "Demo"
              selected: chatSurface && chatSurface.demoOpen
              onClicked: root.openDemo()
            }
            RailIcon {
              materialIcon: chatSurface && chatSurface.muted
                ? "notifications_off" : "notifications"
              label: chatSurface && chatSurface.muted ? "Unmute" : "Mute"
              selected: chatSurface && chatSurface.muted
              onClicked: if (chatSurface) chatSurface.toggleMute()
            }
            RailIcon {
              materialIcon: "warning"
              label: "Danger zone"
              selected: root.moreOpen && root.moreSection === "danger"
              onClicked: root.openRailAdvanced("danger")
            }
          }
        }

        Flickable {
          id: panelScroll
          anchors.fill: parent
          anchors.margins: root.pad
          anchors.rightMargin: root.pad
          contentWidth: width
          contentHeight: column.implicitHeight
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.VerticalFlick

          MouseArea {
            width: panelScroll.width
            height: Math.max(panelScroll.height, column.implicitHeight)
            acceptedButtons: Qt.LeftButton
            onClicked: {
              root.dismissTransientSections()
              panelFocus.forceActiveFocus()
            }
          }

          Column {
            id: column
            width: Math.max(0, panelScroll.width - root.railWidth)
            spacing: Style.space(12)

          Item {
            id: heroRow
            width: panelScroll.width
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
            visible: false
            width: parent.width
            columns: 3
            columnSpacing: root.btnGap
            rowSpacing: Style.space(6)
            readonly property real actionWidth: Math.max(0,
              (width - columnSpacing * (columns - 1)) / columns)

            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              Layout.preferredWidth: heroActions.actionWidth
              iconText: "qr_code_2"
              iconFontFamily: "Material Symbols Rounded"
              text: "Invite"
              selected: root.inviteOpen
              onClicked: root.toggleInvite()
            }
            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              Layout.preferredWidth: heroActions.actionWidth
              iconText: "person_add"
              iconFontFamily: "Material Symbols Rounded"
              text: "Add"
              selected: root.showJoin
              onClicked: root.toggleJoin()
            }
            ActionButton {
              visible: !omaq.locked
              Layout.fillWidth: true
              Layout.preferredWidth: heroActions.actionWidth
              iconText: "chat"
              iconFontFamily: "Material Symbols Rounded"
              text: "Chat"
              selected: root.chatPickerOpen
              onClicked: root.openChat()
            }
            ActionButton {
              Layout.columnSpan: 3
              Layout.fillWidth: true
              iconText: "settings"
              iconFontFamily: "Material Symbols Rounded"
              text: "Settings"
              selected: root.settingsOpen
              onClicked: root.toggleSettings()
            }
          }

          GridLayout {
            id: settingsActions
            visible: false
            width: parent.width
            columns: 2
            columnSpacing: root.btnGap
            rowSpacing: Style.space(6)
            readonly property real actionWidth: Math.max(0, (width - columnSpacing) / columns)

            ActionButton {
              Layout.fillWidth: true
              Layout.preferredWidth: settingsActions.actionWidth
              iconText: "science"
              iconFontFamily: "Material Symbols Rounded"
              text: "Demo"
              selected: chatSurface && chatSurface.demoOpen
              onClicked: root.openDemo()
            }
            ActionButton {
              Layout.fillWidth: true
              Layout.preferredWidth: settingsActions.actionWidth
              iconText: "palette"
              iconFontFamily: "Material Symbols Rounded"
              text: "Theme"
              selected: root.themeOpen
              accent: root.chatTheme === "system" ? Color.accent : Model.themeFor(root.chatTheme).accent
              onClicked: root.toggleThemeSettings()
            }
            ActionButton {
              Layout.fillWidth: true
              Layout.preferredWidth: settingsActions.actionWidth
              iconText: chatSurface && chatSurface.muted ? "notifications_off" : "notifications"
              iconFontFamily: "Material Symbols Rounded"
              text: chatSurface && chatSurface.muted ? "Unmute" : "Mute"
              tooltipText: "Mute notification sound"
              selected: chatSurface && chatSurface.muted
              onClicked: if (chatSurface) chatSurface.toggleMute()
            }
            ActionButton {
              Layout.fillWidth: true
              Layout.preferredWidth: settingsActions.actionWidth
              iconText: "music_note"
              iconFontFamily: "Material Symbols Rounded"
              text: "Sounds"
              selected: root.soundOpen
              onClicked: root.toggleSoundSettings()
            }
          }

          Column {
            visible: root.chatPickerOpen
            width: parent.width
            spacing: Style.space(6)

            PanelSectionHeader {
              text: "CHAT WITH"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            Text {
              visible: !omaq.friends || omaq.friends.length === 0
              width: parent.width
              text: "No contacts yet. Use Invite or Join first."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            Repeater {
              model: omaq.friends
              delegate: ActionButton {
                required property var modelData
                width: parent ? parent.width : 0
                text: {
                  var name = modelData && modelData.name
                    ? String(modelData.name)
                    : ("Friend " + (modelData ? modelData.id : ""))
                  return name + " · " + root.friendStatus(modelData)
                }
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: root.openFriend(modelData ? modelData.id : "",
                  modelData ? modelData.name : "")
              }
            }
          }

          Column {
            visible: !root.primaryMenuOpen
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
                revision: omaq.avatarTick
                onClicked: root.pickSelfAvatar()
              }
              Column {
                width: Style.space(160)
                y: (parent.height - height) / 2
                spacing: 0
                Text {
                  visible: omaq.selfNickname !== "" && !root.nicknameEditOpen
                  width: parent.width
                  height: root.nicknameControlHeight
                  text: omaq.selfNickname
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                  verticalAlignment: Text.AlignVCenter
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
                Row {
                  visible: omaq.selfNickname === "" || root.nicknameEditOpen
                  width: parent.width
                  height: root.nicknameControlHeight
                  spacing: root.btnGap
                  TokenTextField {
                    id: nicknameField
                    width: parent.width - nicknameButton.implicitWidth - root.btnGap
                    height: parent.height
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
                    text: ""
                    width: implicitWidth
                    height: parent.height
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    enabled: nicknameField.text.trim() !== ""
                    onClicked: {
                      root.nicknameSubmitPending = omaq.setNickname(nicknameField.text)
                    }
                  }
                }
              }
            }

            Text {
              text: root.connectionLabel()
              color: omaq.connectionState === "online" ? root.controlAccent : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: omaq.connectionState !== "online"
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

            Grid {
              id: friendsGrid
              visible: omaq.friends && omaq.friends.length > 0
              width: parent.width
              columns: root.friendColumnCount
              rows: 10
              flow: Grid.TopToBottom
              columnSpacing: Style.space(12)
              rowSpacing: Style.space(2)

              Repeater {
                model: root.friendPageItems
                delegate: Item {
                  id: friendDelegate
                  required property var modelData
                  width: Math.max(0, (friendsGrid.width -
                    friendsGrid.columnSpacing * (root.friendColumnCount - 1)) /
                    root.friendColumnCount)
                  height: Style.space(24)
                  activeFocusOnTab: true
                  Accessible.role: Accessible.Button
                  Accessible.name: friendName.text + " · " + root.friendStatus(friendDelegate.modelData)
                  Accessible.onPressAction: root.openFriend(friendDelegate.modelData ? friendDelegate.modelData.id : "",
                    friendDelegate.modelData ? friendDelegate.modelData.name : "")
                  Keys.onReturnPressed: root.openFriend(friendDelegate.modelData ? friendDelegate.modelData.id : "",
                    friendDelegate.modelData ? friendDelegate.modelData.name : "")
                  Keys.onEnterPressed: root.openFriend(friendDelegate.modelData ? friendDelegate.modelData.id : "",
                    friendDelegate.modelData ? friendDelegate.modelData.name : "")

                  Rectangle {
                    id: friendStatusDot
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Style.space(6)
                    height: width
                    radius: width / 2
                    color: root.friendStatusColor(friendDelegate.modelData)
                    border.width: 0
                  }

                  Text {
                    id: friendName
                    anchors.left: friendStatusDot.right
                    anchors.leftMargin: Style.space(6)
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    text: {
                      var friend = friendDelegate.modelData
                      var name = friend && friend.name
                        ? String(friend.name) : ("Friend " + (friend ? friend.id : ""))
                      var unread = omaq.unreadFor(friend ? friend.id : "")
                      return unread > 0 ? name + " · " + unread : name
                    }
                    color: root.friendStatusColor(friendDelegate.modelData)
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.body
                    font.bold: friendDelegate.modelData && friendDelegate.modelData.online
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                  }

                  MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openFriend(friendDelegate.modelData ? friendDelegate.modelData.id : "",
                      friendDelegate.modelData ? friendDelegate.modelData.name : "")
                  }
                }
              }
            }

            Row {
              visible: root.friendPageCount > 1
              x: Math.max(0, (parent.width - implicitWidth) / 2)
              spacing: Style.space(8)

              Text {
                id: previousFriendPage
                text: "chevron_left"
                color: root.friendPage > 0 ? root.systemColors[3] : root.dim
                font.family: "Material Symbols Rounded"
                font.pixelSize: Style.font.icon
                font.variableAxes: ({ "FILL": activeFocus ? 1 : 0, "wght": 500 })
                activeFocusOnTab: root.friendPage > 0
                Accessible.role: Accessible.Button
                Accessible.name: "Previous friend page"
                Accessible.onPressAction: if (root.friendPage > 0) root.friendPage--
                Keys.onReturnPressed: if (root.friendPage > 0) root.friendPage--
                Keys.onEnterPressed: if (root.friendPage > 0) root.friendPage--
                Keys.onSpacePressed: if (root.friendPage > 0) root.friendPage--
                MouseArea {
                  anchors.fill: parent
                  enabled: root.friendPage > 0
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.friendPage--
                }
              }
              Text {
                text: (root.friendPage + 1) + "/" + root.friendPageCount
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
              }
              Text {
                id: nextFriendPage
                text: "chevron_right"
                color: root.friendPage + 1 < root.friendPageCount
                  ? root.systemColors[3] : root.dim
                font.family: "Material Symbols Rounded"
                font.pixelSize: Style.font.icon
                font.variableAxes: ({ "FILL": activeFocus ? 1 : 0, "wght": 500 })
                activeFocusOnTab: root.friendPage + 1 < root.friendPageCount
                Accessible.role: Accessible.Button
                Accessible.name: "Next friend page"
                Accessible.onPressAction: if (root.friendPage + 1 < root.friendPageCount) root.friendPage++
                Keys.onReturnPressed: if (root.friendPage + 1 < root.friendPageCount) root.friendPage++
                Keys.onEnterPressed: if (root.friendPage + 1 < root.friendPageCount) root.friendPage++
                Keys.onSpacePressed: if (root.friendPage + 1 < root.friendPageCount) root.friendPage++
                MouseArea {
                  anchors.fill: parent
                  enabled: root.friendPage + 1 < root.friendPageCount
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.friendPage++
                }
              }
            }

            Text {
              visible: omaq.groups && omaq.groups.length > 0
              text: "GROUPS"
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
            }

            Column {
              visible: omaq.groups && omaq.groups.length > 0
              width: parent.width
              spacing: Style.space(2)

              Repeater {
                model: omaq.groups || []
                delegate: Item {
                  id: activeGroupDelegate
                  required property var modelData
                  width: parent ? parent.width : 0
                  height: Style.space(24)
                  activeFocusOnTab: true
                  readonly property int displayedMemberCount: Math.max(
                    Number(modelData && modelData.memberCount || 0),
                    modelData && modelData.members ? modelData.members.length : 0)
                  Accessible.role: Accessible.Button
                  Accessible.name: activeGroupName.text + ", " +
                    displayedMemberCount + (displayedMemberCount === 1 ? " member" : " members")
                  Accessible.onPressAction: root.openGroup(modelData ? modelData.id : "")
                  Keys.onReturnPressed: root.openGroup(modelData ? modelData.id : "")
                  Keys.onEnterPressed: root.openGroup(modelData ? modelData.id : "")

                  Text {
                    id: activeGroupIcon
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "groups"
                    color: root.systemColors[3] || root.controlAccent
                    font.family: "Material Symbols Rounded"
                    font.pixelSize: Style.font.icon
                    font.variableAxes: ({ "FILL": activeGroupDelegate.activeFocus ? 1 : 0,
                                          "wght": 500 })
                  }

                  Text {
                    id: activeGroupName
                    anchors.left: activeGroupIcon.right
                    anchors.leftMargin: Style.space(6)
                    anchors.right: activeGroupCount.left
                    anchors.rightMargin: Style.space(8)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    text: {
                      var group = activeGroupDelegate.modelData
                      var name = group && group.title ? String(group.title) : "Group"
                      var unread = omaq.unreadFor(group ? group.id : "")
                      return unread > 0 ? name + " · " + unread : name
                    }
                    color: root.foreground
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.body
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                  }

                  Text {
                    id: activeGroupCount
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(activeGroupDelegate.displayedMemberCount)
                    color: root.dim
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.caption
                    font.features: ({ "tnum": 1 })
                  }

                  MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openGroup(activeGroupDelegate.modelData
                      ? activeGroupDelegate.modelData.id : "")
                  }
                }
              }
            }
          }

          Column {
            visible: root.settingsOpen && root.themeOpen
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

          Column {
            visible: root.settingsOpen && root.soundOpen
            width: parent.width
            spacing: Style.space(6)

            PanelSectionHeader {
              text: "NOTIFICATION SOUND"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            GridLayout {
              id: soundGrid
              width: parent.width
              columns: 4
              columnSpacing: Style.space(4)
              rowSpacing: Style.space(4)
              readonly property real optionWidth: Math.max(0,
                (width - columnSpacing * (columns - 1)) / columns)

              Repeater {
                model: root.notificationSounds
                delegate: ActionButton {
                  required property var modelData
                  Layout.fillWidth: true
                  Layout.preferredWidth: soundGrid.optionWidth
                  iconText: ""
                  text: String(modelData.label)
                  fontSize: Style.font.bodySmall
                  horizontalPadding: Style.space(3)
                  selected: root.notificationSound === String(modelData.id)
                  onClicked: root.setNotificationSound(modelData.id)
                }
              }
            }
          }

          Text {
            visible: omaq.unreadWarning !== "" ||
              (chatSurface && chatSurface.autoOpenWarning !== "") ||
              (omaq.lastError !== "" && !(omaq.locked && omaq.lastError === "locked"))
            width: parent.width
            text: omaq.lastError !== "" && !(omaq.locked && omaq.lastError === "locked")
              ? root.errorText(omaq.lastError)
              : (omaq.unreadWarning !== "" ? omaq.unreadWarning
                : (chatSurface && chatSurface.autoOpenWarning !== ""
                  ? chatSurface.autoOpenWarning : ""))
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
                source: omaq.qrPath !== "" ? root.localFileUrl(omaq.qrPath) : ""
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

            TokenButton {
              visible: false
              text: root.moreOpen ? "Hide advanced" : "Advanced"
              iconText: "tune"
              iconFontFamily: "Material Symbols Rounded"
              focusable: true
              bordered: true
              selected: root.moreOpen
              foreground: root.foreground
              fontFamily: root.fontFamily
              onClicked: root.toggleAdvanced()
            }

            Column {
              visible: root.moreOpen
              width: parent.width
              spacing: Style.space(8)

              GridLayout {
                visible: false
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
                  iconText: "󰡉"
                  iconFontFamily: "monospace"
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
                visible: root.moreSection === "chat" && omaq.lastDirectId !== "" &&
                  (!root.safetyCodeVisible || omaq.safetyCode === "")
                width: parent.width
                iconText: "󰌾"
                text: "Show safety code"
                onClicked: root.showSafetyCode()
              }

              Column {
                visible: root.moreSection === "chat" && root.safetyCodeVisible &&
                  omaq.safetyCode !== ""
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

                Row {
                  width: parent.width
                  spacing: root.btnGap
                  ActionButton {
                    width: (parent.width - root.btnGap) / 2
                    iconText: root.safetyCopied ? "check" : "content_copy"
                    iconFontFamily: "Material Symbols Rounded"
                    text: root.safetyCopied ? "Copied" : "Copy"
                    onClicked: root.copySafetyCode()
                  }
                  ActionButton {
                    width: (parent.width - root.btnGap) / 2
                    iconText: "visibility_off"
                    iconFontFamily: "Material Symbols Rounded"
                    text: "Hide"
                    onClicked: root.hideSafetyCode()
                  }
                }
              }

              PanelSeparator {
                visible: root.moreSection === "chat" && root.safetyCodeVisible &&
                  omaq.safetyCode !== ""
                foreground: root.foreground
              }

              PanelSectionHeader {
                visible: root.moreSection === "groups"
                text: "GROUPS"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Row {
                visible: root.moreSection === "groups"
                width: parent.width
                spacing: root.btnGap

                TokenTextField {
                  id: groupNameField
                  width: parent.width - createGroupButton.width - parent.spacing
                  foreground: root.controlForeground
                  placeholderText: "Group name"
                  onAccepted: createGroupButton.clicked()
                }

                ActionButton {
                  id: createGroupButton
                  text: "Create"
                  enabled: omaq.groupTitleOk(groupNameField.text)
                  onClicked: {
                    if (omaq.createGroup(groupNameField.text.trim()))
                      groupNameField.text = ""
                  }
                }
              }

              Text {
                visible: root.moreSection === "groups" && (!omaq.groups || omaq.groups.length === 0)
                width: parent.width
                text: "No groups yet"
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
              }

              Column {
                visible: root.moreSection === "groups" && omaq.groups && omaq.groups.length > 0
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: omaq.groups || []
                  delegate: TokenButton {
                    required property var modelData
                    width: parent ? parent.width : 0
                    text: String(modelData.title || "Group") + " · " +
                      String(modelData.memberCount || 0) + "/" + String(modelData.limit || 10)
                    selected: String(modelData.id || "") === String(omaq.lastGroup || "")
                    bordered: true
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: {
                      omaq.selectGroup(modelData.id)
                      root.groupInviteFriendId = ""
                      root.groupInviteFeedback = ""
                      root.groupLeaveConfirm = false
                      root.groupLeaveTarget = ""
                      root.groupDissolveConfirm = false
                      root.groupDissolveTarget = ""
                    }
                  }
                }
              }

              ActionButton {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                width: parent.width
                iconText: "chat"
                iconFontFamily: "Material Symbols Rounded"
                text: "Open " + omaq.groupName(omaq.lastGroup)
                onClicked: root.openGroup(omaq.lastGroup)
              }

              PanelSectionHeader {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                text: "INVITE CONTACT TO " + omaq.groupName(omaq.lastGroup).toUpperCase()
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Flow {
                visible: root.moreSection === "groups" && omaq.lastGroup !== "" &&
                  omaq.friends && omaq.friends.length > 0
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: omaq.friends || []
                  delegate: TokenButton {
                    required property var modelData
                    text: String(modelData.name || ("Friend " + modelData.id))
                    selected: String(modelData.id || "") === root.groupInviteFriendId
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: root.groupInviteFriendId = String(modelData.id || "")
                  }
                }
              }

              ActionButton {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                width: parent.width
                text: root.groupInviteFriendId
                  ? "Invite contact · " + root.friendName(root.groupInviteFriendId)
                  : "Select a contact"
                enabled: root.groupInviteFriendId !== ""
                onClicked: {
                  root.groupInviteFeedback = "Sending group invite…"
                  if (!omaq.inviteToGroup(root.groupInviteFriendId, omaq.lastGroup))
                    root.groupInviteFeedback = "Group invite failed"
                }
              }

              Text {
                visible: root.moreSection === "groups" && root.groupInviteFeedback !== ""
                width: parent.width
                text: root.groupInviteFeedback
                color: root.groupInviteFeedback.indexOf("sent") >= 0 ||
                  root.groupInviteFeedback.indexOf("Sending") === 0
                  ? root.systemColors[3] : root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              Text {
                visible: root.moreSection === "groups" && root.groupLeaveConfirm
                width: parent.width
                text: "Leave " + omaq.groupName(root.groupLeaveTarget) + "?"
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              Row {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                width: parent.width
                spacing: root.btnGap

                ActionButton {
                  width: root.groupLeaveConfirm
                    ? (parent.width - parent.spacing) / 2 : parent.width
                  text: root.groupLeaveConfirm ? "Cancel" : "Leave " + omaq.groupName(omaq.lastGroup)
                  accent: root.groupLeaveConfirm ? root.controlAccent : root.urgent
                  onClicked: {
                    root.groupDissolveConfirm = false
                    root.groupDissolveTarget = ""
                    root.groupLeaveConfirm = !root.groupLeaveConfirm
                    root.groupLeaveTarget = root.groupLeaveConfirm
                      ? String(omaq.lastGroup || "") : ""
                  }
                }
                ActionButton {
                  visible: root.groupLeaveConfirm
                  width: visible ? (parent.width - parent.spacing) / 2 : 0
                  text: "Leave now"
                  accent: root.urgent
                  onClicked: {
                    var target = root.groupLeaveTarget
                    root.groupLeaveConfirm = false
                    root.groupLeaveTarget = ""
                    omaq.leaveGroup(target)
                  }
                }
              }

              Text {
                visible: root.moreSection === "groups" && root.groupDissolveConfirm
                width: parent.width
                text: "Dissolve " + omaq.groupName(root.groupDissolveTarget) + " for every member?"
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              Row {
                visible: root.moreSection === "groups" && omaq.lastGroup !== "" &&
                  omaq.groupSelfRole(root.groupDissolveConfirm
                    ? root.groupDissolveTarget : omaq.lastGroup) === "owner"
                width: parent.width
                spacing: root.btnGap

                ActionButton {
                  width: root.groupDissolveConfirm
                    ? (parent.width - parent.spacing) / 2 : parent.width
                  text: root.groupDissolveConfirm ? "Cancel" : "Dissolve group"
                  accent: root.groupDissolveConfirm ? root.controlAccent : root.urgent
                  onClicked: {
                    root.groupLeaveConfirm = false
                    root.groupLeaveTarget = ""
                    root.groupDissolveConfirm = !root.groupDissolveConfirm
                    root.groupDissolveTarget = root.groupDissolveConfirm
                      ? String(omaq.lastGroup || "") : ""
                  }
                }
                ActionButton {
                  visible: root.groupDissolveConfirm
                  width: visible ? (parent.width - parent.spacing) / 2 : 0
                  text: "Dissolve now"
                  accent: root.urgent
                  onClicked: {
                    var target = root.groupDissolveTarget
                    root.groupDissolveConfirm = false
                    root.groupDissolveTarget = ""
                    omaq.dissolveGroup(target)
                  }
                }
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
                  iconText: "󰈝"
                  iconFontFamily: "monospace"
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
                onTextChanged: {
                  if (root.replaceIdentityConfirm && text.trim() !== root.replaceIdentityPath) {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                  }
                }
              }

              Row {
                visible: root.moreSection === "identity"
                width: parent.width
                spacing: root.btnGap
                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  iconText: "file_open"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Import"
                  onClicked: {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                    if (importPath.text.trim())
                      omaq.importIdentity(importPath.text.trim(), false, passField.text)
                  }
                }
                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  iconText: "󰬲"
                  iconFontFamily: "monospace"
                  text: "Replace"
                  accent: root.urgent
                  onClicked: {
                    var value = importPath.text.trim()
                    if (value) {
                      root.replaceIdentityPath = value
                      root.replaceIdentityConfirm = true
                    }
                  }
                }
              }

              Text {
                visible: root.moreSection === "identity" && root.replaceIdentityConfirm
                width: parent.width
                text: "Replace the current identity with " + root.replaceIdentityPath + "?"
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              Row {
                visible: root.moreSection === "identity" && root.replaceIdentityConfirm
                width: parent.width
                spacing: root.btnGap

                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  text: "Cancel"
                  onClicked: {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                  }
                }
                ActionButton {
                  width: (parent.width - root.btnGap) / 2
                  iconText: "󰬲"
                  iconFontFamily: "monospace"
                  text: "Replace now"
                  accent: root.urgent
                  onClicked: {
                    omaq.importIdentity(root.replaceIdentityPath, true, passField.text)
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
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
                visible: root.moreSection === "danger" &&
                  !root.nospamConfirm && !root.removeContactConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)

                ActionButton {
                  visible: omaq.lastDirectId !== ""
                  Layout.fillWidth: true
                  iconText: "󰆴"
                  text: "Remove contact"
                  onClicked: root.removeContactConfirm = true
                }

                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰒭"
                  text: "Rotate personal ID"
                  onClicked: root.nospamConfirm = true
                }
              }

              Text {
                visible: root.moreSection === "danger" && root.removeContactConfirm
                width: parent.width
                text: "Remove this contact? Chat history stays on this machine."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              GridLayout {
                visible: root.moreSection === "danger" && root.removeContactConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)
                ActionButton {
                  Layout.fillWidth: true
                  text: "Cancel"
                  onClicked: root.removeContactConfirm = false
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰆴"
                  text: "Remove"
                  accent: root.urgent
                  onClicked: {
                    omaq.removeContact()
                    root.removeContactConfirm = false
                    root.safetyCodeVisible = false
                  }
                }
              }

              Text {
                visible: root.moreSection === "danger" && root.nospamConfirm
                width: parent.width
                text: "Rotate your personal ID? This voids every open invite."
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
                  accent: root.urgent
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
