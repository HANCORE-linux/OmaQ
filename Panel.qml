import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects
import QtQuick.Controls as Controls
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import qs.Ui
import qs.Commons
import "Model.js" as Model
import "." as OmaQ

BarWidget {
  id: root
  moduleName: "hancore.omaq"

  property bool opened: false
  property string redeemDraft: ""
  property string redeemRequest: ""
  property string redeemFeedback: ""
  property string redeemFeedbackRequest: ""
  property bool nospamConfirm: false
  property bool removeContactConfirm: false
  property bool removeContactPickerOpen: false
  property string removeContactId: ""
  property string removeContactKey: ""
  property bool identityPrimaryConfirm: false
  property int identityPrimaryRequestSequence: 0
  property string identityPrimaryRequest: ""
  property bool directReinviteClearConfirm: false
  property int directReinviteRequestSequence: 0
  property string directReinviteRequest: ""
  property bool replaceIdentityConfirm: false
  property string replaceIdentityPath: ""
  property bool showJoin: false
  property bool chatPickerOpen: false
  property bool inviteOpen: false
  property string inviteConfirmMode: ""
  property bool inviteActionPending: false
  property string inviteActionMode: ""
  property string inviteActionRequest: ""
  property int inviteActionSequence: 0
  property int inviteActionGeneration: -1
  property string inviteFeedback: ""
  property string inviteFeedbackRequest: ""
  property bool inviteFeedbackError: false
  property double inviteNow: Math.floor(Date.now() / 1000)
  property bool moreOpen: false
  property string moreSection: ""
  property bool settingsOpen: false
  property bool themeOpen: false
  property bool soundOpen: false
  property bool fontSizeOpen: false
  property int soundPickerExitCode: -1
  property bool soundPickerStreamDone: false
  property string soundActionRequest: ""
  property string soundAction: ""
  property string soundFeedback: ""
  property bool soundFeedbackError: false
  property string soundRemoveId: ""
  property string soundRemoveLabel: ""
  property string soundRemovePath: ""
  property bool soundRemoveConfirm: false
  property bool copied: false
  property bool safetyCodeVisible: false
  property bool safetyCopied: false
  property bool nicknameEditOpen: false
  property bool nicknameSubmitPending: false
  property string nicknameRequest: ""
  property int nicknameRequestSequence: 0
  property string nicknameFeedback: ""
  property string nicknameFeedbackRequest: ""
  property bool nicknameFeedbackError: false
  property bool avatarRestorePending: false
  property bool avatarRestoreMore: false
  property string groupInviteGroupId: ""
  property string groupInviteFriendId: ""
  property string groupInviteFriendKey: ""
  property string groupInviteRequest: ""
  property int groupInviteGeneration: -1
  property string groupInviteFeedback: ""
  property bool groupLeaveConfirm: false
  property string groupLeaveTarget: ""
  property bool groupDissolveConfirm: false
  property string groupDissolveTarget: ""
  property int avatarPickExitCode: -1
  property bool avatarPickStreamDone: false
  property string identityAction: ""
  property string identityRequest: ""
  property int identityRequestSequence: 0
  property string identityFeedback: ""
  property string identityFeedbackRequest: ""
  property bool identityFeedbackError: false
  property string settingsPersistenceWarning: ""
  property var settingsPersistenceExpected: ({})
  property bool settingsPersistencePending: false
  property int settingsPersistenceAttempts: 0
  property bool identityActionPending: false
  property string identityPickerMode: ""
  property int identityPickerExitCode: -1
  property bool identityPickerStreamDone: false
  property var systemColors: ["#101315", "#565d60", "#9fa5a9", "#d9dbdc", "#798186", "#aeaeae", "#707070", "#cbc2be"]
  property string systemThemeName: "System"
  readonly property var bundledNotificationSounds: [
    { id: "off", label: "Off", custom: false, path: "" },
    { id: "icq-message", label: "UHOH" },
    { id: "qq", label: "PING" },
    { id: "msn", label: "MAIL" },
    { id: "aurora", label: "Aurora" },
    { id: "glow", label: "Glow" },
    { id: "click", label: "Click" },
    { id: "knock", label: "Knock", custom: false, path: "" }
  ]
  readonly property var notificationSounds: {
    var revision = Number(omaq.soundTick || 0)
    var result = root.bundledNotificationSounds.slice()
    var custom = omaq.customSounds || []
    for (var i = 0; revision >= 0 && i < custom.length; i++)
      result.push({ id: "custom:" + String(custom[i].id || ""),
        soundId: String(custom[i].id || ""),
        label: String(custom[i].label || "Custom sound"),
        path: String(custom[i].path || ""), custom: true })
    return result
  }
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color barForeground: bar && "barForeground" in bar ? bar.barForeground : foreground
  readonly property var shibumiTokens: bar && "visualTokens" in bar ? bar.visualTokens : null
  readonly property string shellStyle: shibumiTokens && shibumiTokens.shellStyle !== undefined
    ? String(shibumiTokens.shellStyle) : "shibumi"
  readonly property bool connectedSurfaceEnabled: shellStyle !== "shibumi"
    && (barPos === "top" || barPos === "bottom")
  property real connectionReveal: 0
  property real cardHeightLimit: 0
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
  readonly property real controlRadius: {
    var themeRevision = root.systemThemeName
    return shibumiTokens && shibumiTokens.tileRadius !== undefined
      ? Number(shibumiTokens.tileRadius) : Style.cornerRadius
  }
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
  readonly property real btnGap: Style.space(6)
  readonly property real panelSectionGap: Style.space(8)
  readonly property real framePadding: Style.space(6)
  readonly property int pad: Style.spacing.popupPadding
  readonly property real nicknameControlHeight: Style.space(18)
  readonly property real nicknameEditorHeight: Style.space(30)
  readonly property int friendColumnCount: Math.min(3, Math.max(1,
    Math.ceil(Math.max(1, omaq.friends ? omaq.friends.length : 0) / 5)))
  readonly property int cardWidth: Style.space(320 + (friendColumnCount - 1) * 130)
  readonly property real railIconWidth: Style.space(30)
  readonly property real railWidth: railIconWidth * 2 + framePadding * 2
  readonly property real headerHeight: Style.space(48)
  readonly property real supportGlyphSize: Style.font.icon + Style.space(3)
  readonly property real basePrimaryAreaHeight: Math.max(
    identityContactsFrame.implicitHeight, actionRail.implicitHeight)
  readonly property real primaryAreaHeight: root.primaryMenuOpen
    ? Math.max(column.implicitHeight + root.framePadding * 2,
               root.basePrimaryAreaHeight)
    : root.basePrimaryAreaHeight
  readonly property real actionButtonHeight: Style.space(24)
  readonly property color onlineStatusColor: "#7dce6a"
  readonly property bool primaryMenuOpen: root.inviteOpen || root.showJoin ||
    root.chatPickerOpen || root.settingsOpen || root.moreOpen || omaq.locked ||
    omaq.incomingCall
  readonly property int visibleUnreadCount: Math.max(omaq.unreadCount, omaq.localUnreadTotal())
  readonly property string currentSafetyCode:
    String(omaq.safetyConv || "") === String(omaq.selectedDirectId || "")
      ? String(omaq.safetyCode || "") : ""
  readonly property bool contextualErrorHandled:
    (root.identityFeedbackRequest !== "" &&
     String(omaq.lastErrorRequest || "") === root.identityFeedbackRequest &&
     omaq.identityErrorCode(omaq.lastError)) ||
    (root.nicknameFeedbackRequest !== "" &&
     String(omaq.lastErrorRequest || "") === root.nicknameFeedbackRequest) ||
    (root.inviteFeedbackRequest !== "" &&
     String(omaq.lastErrorRequest || "") === root.inviteFeedbackRequest) ||
    (root.redeemFeedbackRequest !== "" &&
     String(omaq.lastErrorRequest || "") === root.redeemFeedbackRequest) ||
    (omaq.directReinviteRequired &&
     String(omaq.lastError || "") === "direct_state_reinvite_required") ||
    (omaq.identityPrimaryUncertain &&
     String(omaq.lastError || "") === "identity_primary_uncertain")
  readonly property string barPos: bar && bar.position ? String(bar.position) : "top"
  readonly property int inviteRemainingSeconds: Math.max(0,
    Math.floor(Number(omaq.inviteExpiresAt || 0) - root.inviteNow))
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
  readonly property string soundPickerScript:
    "if command -v zenity >/dev/null 2>&1; then\n" +
    "  exec zenity --file-selection --title='Import notification sound' --file-filter='PCM WAV audio | *.wav'\n" +
    "elif command -v kdialog >/dev/null 2>&1; then\n" +
    "  exec kdialog --getopenfilename \"$HOME\" '*.wav|PCM WAV audio'\n" +
    "elif command -v yad >/dev/null 2>&1; then\n" +
    "  exec yad --file --title='Import notification sound'\n" +
    "fi\n" +
    "exit 2\n"
  readonly property string identityPickerScript:
    "mode=$1\n" +
    "downloads=${XDG_DOWNLOAD_DIR:-$HOME/Downloads}\n" +
    "if [ \"$mode\" = export ]; then\n" +
    "  initial=$downloads/omaq-identity.save\n" +
    "  if command -v zenity >/dev/null 2>&1; then\n" +
    "    exec zenity --file-selection --save --confirm-overwrite --title='Export OmaQ identity' --filename=\"$initial\" --file-filter='OmaQ identity | *.save'\n" +
    "  elif command -v kdialog >/dev/null 2>&1; then\n" +
    "    exec kdialog --getsavefilename \"$initial\" '*.save|OmaQ identity'\n" +
    "  elif command -v yad >/dev/null 2>&1; then\n" +
    "    exec yad --file --save --confirm-overwrite --title='Export OmaQ identity' --filename=\"$initial\"\n" +
    "  fi\n" +
    "else\n" +
    "  if command -v zenity >/dev/null 2>&1; then\n" +
    "    exec zenity --file-selection --title='Select OmaQ identity' --file-filter='OmaQ identity | *.save'\n" +
    "  elif command -v kdialog >/dev/null 2>&1; then\n" +
    "    exec kdialog --getopenfilename \"$HOME\" '*.save|OmaQ identity'\n" +
    "  elif command -v yad >/dev/null 2>&1; then\n" +
    "    exec yad --file --title='Select OmaQ identity'\n" +
    "  fi\n" +
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

      SafeText {
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

      SafeText {
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
    property bool borderless: false
    property string accessibleName: tooltipText !== "" ? tooltipText : text
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
    readonly property real contentSpacing: iconText !== "" && text !== "" ? Style.space(4) : 0
    readonly property real naturalContentWidth:
      (iconText !== "" ? buttonIcon.implicitWidth : 0) +
      (text !== "" ? buttonLabel.implicitWidth : 0) + contentSpacing
    readonly property var normalBorder: bordered
      ? Border.flat(root.controlBorder, 1) : Border.none()
    readonly property var activeBorder: Border.flat(actionColor, 1)

    activeFocusOnTab: focusable
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: if (tokenButton.enabled) tokenButton.clicked()
    onActiveFocusChanged: if (activeFocus) root.ensurePanelItemVisible(tokenButton)
    Keys.onReturnPressed: if (focusable) tokenButton.clicked()
    Keys.onEnterPressed: if (focusable) tokenButton.clicked()
    Keys.onSpacePressed: if (focusable) tokenButton.clicked()

    implicitWidth: naturalContentWidth + horizontalPadding * 2
    implicitHeight: Math.max(root.actionButtonHeight,
                             Math.max(buttonIcon.implicitHeight, buttonLabel.implicitHeight) +
                             verticalPadding * 2)
    radius: root.themedRadius(height > 0 ? height : implicitHeight)
    color: borderless ? "transparent"
      : mouseArea.pressed ? root.controlActiveFill
      : selected || active ? root.controlActiveFill
      : hot ? root.controlHoverFill : root.controlFill
    borderSpec: borderless ? Border.none()
      : activeFocus || hot || selected || active ? activeBorder : normalBorder

    Behavior on color { ColorAnimation { duration: 100 } }

    Row {
      id: row
      anchors.centerIn: parent
      width: Math.min(tokenButton.naturalContentWidth,
                      Math.max(0, tokenButton.width - tokenButton.horizontalPadding * 2))
      spacing: tokenButton.contentSpacing

      SafeText {
        id: buttonIcon
        visible: tokenButton.iconText !== ""
        text: tokenButton.iconText
        color: tokenButton.selected || tokenButton.hot || tokenButton.activeFocus
          ? tokenButton.actionColor : tokenButton.foreground
        font.family: tokenButton.iconFontFamily
        font.pixelSize: tokenButton.iconSize
        anchors.verticalCenter: parent.verticalCenter
      }

      SafeText {
        id: buttonLabel
        visible: tokenButton.text !== ""
        width: Math.max(0, row.width -
          (buttonIcon.visible ? buttonIcon.implicitWidth + row.spacing : 0))
        text: tokenButton.text
        color: tokenButton.selected || tokenButton.hot || tokenButton.activeFocus
          ? tokenButton.actionColor : tokenButton.foreground
        font.family: tokenButton.fontFamily
        font.pixelSize: tokenButton.fontSize
        font.bold: false
        elide: Text.ElideRight
        anchors.verticalCenter: parent.verticalCenter
      }
    }

    Controls.ToolTip {
      id: tokenTooltip
      visible: (tokenButton.hot ||
        (tokenButton.activeFocus &&
         (tokenButton.activeFocusReason === Qt.TabFocusReason ||
          tokenButton.activeFocusReason === Qt.BacktabFocusReason))) &&
        (tokenButton.tooltipText !== "" || buttonLabel.truncated)
      text: tokenButton.tooltipText !== "" ? tokenButton.tooltipText : tokenButton.text
      delay: 450
      timeout: 2600
      padding: Style.space(5)
      background: Rectangle {
        radius: root.themedRadius(height)
        color: Qt.darker(root.panelBackground, 1.08)
        border.color: Qt.rgba(root.foreground.r, root.foreground.g,
                              root.foreground.b, 0.24)
        border.width: 1
      }
      contentItem: SafeText {
        text: tokenTooltip.text
        color: root.foreground
        font.family: root.fontFamily
        font.pixelSize: Style.font.bodySmall
        renderType: Text.QtRendering
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
    property bool fillSelected: true
    property color activeColor: root.systemColors[3] || root.controlAccent
    signal clicked()

    implicitWidth: root.railIconWidth
    implicitHeight: Style.space(30)
    opacity: enabled ? 1 : 0.35
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: if (enabled) railIcon.clicked()
    Keys.onReturnPressed: if (enabled) railIcon.clicked()
    Keys.onEnterPressed: if (enabled) railIcon.clicked()
    Keys.onSpacePressed: if (enabled) railIcon.clicked()

    SafeText {
      anchors.centerIn: parent
      text: railIcon.materialIcon
      color: railIcon.selected || railHover.hovered || railIcon.activeFocus
        ? railIcon.activeColor : root.dim
      font.family: "Material Symbols Rounded"
      font.pixelSize: Style.font.icon + Style.space(3)
      font.variableAxes: ({ "FILL": railIcon.selected && railIcon.fillSelected ? 1 : 0,
                            "wght": 500 })
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
      contentItem: SafeText {
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
    id: tokenField
    foreground: root.controlForeground
    accent: root.controlAccent
    font.family: root.fontFamily
    font.pixelSize: Style.font.bodySmall
    onActiveFocusChanged: if (activeFocus) root.ensurePanelItemVisible(tokenField)
    background: BorderSurface {
      anchors.fill: parent
      color: root.controlFill
      borderSpec: Border.flat(
        parent.activeFocus || parent.hovered ? tokenField.accent : root.controlBorder, 1)
      radius: root.themedRadius(parent.height)
    }
  }

  component ActionButton: TokenButton {
    implicitHeight: root.actionButtonHeight
    foreground: root.controlForeground
    accent: root.controlAccent
    fontFamily: root.fontFamily
    bordered: true
    focusable: true
    iconSize: Style.font.icon
    fontSize: Style.font.bodySmall
    horizontalPadding: Style.space(4)
    verticalPadding: Style.space(1)
  }

  function refreshPanelLayout() {
    Qt.callLater(function() {
      inviteContent.forceLayout()
      unlockedMenus.forceLayout()
      column.forceLayout()
    })
  }

  onPrimaryMenuOpenChanged: root.refreshPanelLayout()
  onInviteOpenChanged: root.refreshPanelLayout()
  onMoreSectionChanged: {
    root.refreshPanelLayout()
    if (root.moreSection === "groups" && omaq)
      omaq.refreshGroups(true)
  }
  onThemeOpenChanged: root.refreshPanelLayout()
  onSoundOpenChanged: root.refreshPanelLayout()
  onFontSizeOpenChanged: root.refreshPanelLayout()
  onShowJoinChanged: root.refreshPanelLayout()
  onChatPickerOpenChanged: root.refreshPanelLayout()

  function ensurePanelItemVisible(item) {
    if (!item || !root.opened || !panelScroll.contentItem)
      return
    var position = item.mapToItem(panelScroll.contentItem, 0, 0)
    var margin = Style.space(4)
    var top = position.y - margin
    var bottom = position.y + item.height + margin
    if (top < panelScroll.contentY)
      panelScroll.contentY = Math.max(0, top)
    else if (bottom > panelScroll.contentY + panelScroll.height)
      panelScroll.contentY = Math.min(
        Math.max(0, panelScroll.contentHeight - panelScroll.height),
        bottom - panelScroll.height)
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
    root.redeemRequest = ""
    root.redeemFeedback = ""
    root.redeemFeedbackRequest = ""
    root.chatPickerOpen = false
    root.inviteOpen = false
    root.inviteConfirmMode = ""
    if (!root.inviteActionPending) {
      omaq.clearRequestError(root.inviteFeedbackRequest)
      root.inviteFeedback = ""
      root.inviteFeedbackRequest = ""
      root.inviteFeedbackError = false
    }
    root.moreOpen = false
    root.moreSection = ""
    root.settingsOpen = false
    root.themeOpen = false
    root.soundOpen = false
    root.fontSizeOpen = false
    root.soundRemoveConfirm = false
    root.soundRemoveId = ""
    root.soundRemoveLabel = ""
    root.soundRemovePath = ""
    if (root.soundActionRequest === "" && !soundPick.running) {
      root.soundAction = ""
      root.soundFeedback = ""
      root.soundFeedbackError = false
    }
    root.safetyCodeVisible = false
    root.safetyCopied = false
    root.copied = false
    root.nospamConfirm = false
    root.removeContactPickerOpen = false
    root.removeContactConfirm = false
    root.removeContactId = ""
    root.removeContactKey = ""
    root.identityPrimaryConfirm = false
    root.identityPrimaryRequest = ""
    root.directReinviteClearConfirm = false
    root.directReinviteRequest = ""
    root.resetIdentityControls()
    root.nicknameEditOpen = false
    root.nicknameSubmitPending = false
    root.nicknameRequest = ""
    root.nicknameFeedback = ""
    root.nicknameFeedbackRequest = ""
    root.nicknameFeedbackError = false
    nicknameField.text = omaq.selfNickname
    nicknameSubmitTimer.stop()
    root.groupInviteGroupId = ""
    root.groupInviteFriendId = ""
    root.groupInviteFriendKey = ""
    root.groupInviteRequest = ""
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
    root.inviteConfirmMode = ""
    if (!root.inviteActionPending) {
      omaq.clearRequestError(root.inviteFeedbackRequest)
      root.inviteFeedback = ""
      root.inviteFeedbackRequest = ""
      root.inviteFeedbackError = false
    }
    root.showJoin = false
    root.redeemRequest = ""
    root.redeemFeedback = ""
    root.redeemFeedbackRequest = ""
    root.chatPickerOpen = false
    root.settingsOpen = false
    root.themeOpen = false
    root.soundOpen = false
    root.fontSizeOpen = false
    root.soundRemoveConfirm = false
    root.soundRemoveId = ""
    root.soundRemoveLabel = ""
    root.soundRemovePath = ""
    if (root.soundActionRequest === "" && !soundPick.running) {
      root.soundAction = ""
      root.soundFeedback = ""
      root.soundFeedbackError = false
    }
    root.moreOpen = false
    root.moreSection = ""
    root.safetyCodeVisible = false
    root.safetyCopied = false
    root.copied = false
    root.nicknameEditOpen = false
    root.nicknameSubmitPending = false
    root.nicknameRequest = ""
    root.nicknameFeedback = ""
    root.nicknameFeedbackRequest = ""
    root.nicknameFeedbackError = false
    nicknameField.text = omaq.selfNickname
    nicknameSubmitTimer.stop()
    root.nospamConfirm = false
    root.removeContactPickerOpen = false
    root.removeContactConfirm = false
    root.removeContactId = ""
    root.removeContactKey = ""
    root.identityPrimaryConfirm = false
    root.identityPrimaryRequest = ""
    root.directReinviteClearConfirm = false
    root.directReinviteRequest = ""
    root.resetIdentityControls()
    root.groupInviteGroupId = ""
    root.groupInviteFriendId = ""
    root.groupInviteFriendKey = ""
    root.groupInviteRequest = ""
    root.groupInviteFeedback = ""
    root.groupLeaveConfirm = false
    root.groupLeaveTarget = ""
    root.groupDissolveConfirm = false
    root.groupDissolveTarget = ""
    panelScroll.contentY = 0
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
      root.removeContactPickerOpen = false
      root.removeContactConfirm = false
      root.removeContactId = ""
      root.removeContactKey = ""
      root.identityPrimaryConfirm = false
      root.identityPrimaryRequest = ""
      root.directReinviteClearConfirm = false
      root.directReinviteRequest = ""
    }
    if (root.moreSection !== "identity")
      root.resetIdentityControls()
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
      return "Nickname must contain 1–18 valid characters."
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
    if (code === "identity_missing")
      return "Your existing OmaQ identity is unavailable. No replacement identity was created. Restore or import the original identity."
    if (code === "identity_mismatch")
      return "The active identity does not match OmaQ's protected identity record. No contact state was changed."
    if (code === "identity_guard_invalid")
      return "OmaQ could not verify the protected identity state and stopped before creating a replacement."
    if (code === "identity_recovery_degraded")
      return "Your active identity is saved, but its additional recovery copy could not be updated. Export an identity bundle before relying on automatic recovery."
    if (code === "identity_primary_uncertain")
      return "OmaQ could not confirm that the latest identity/contact change reached disk. The helper stopped using that identity; restart OmaQ and verify the result before retrying."
    if (code === "invite_self")
      return "This invite belongs to your own identity."
    if (code === "contact_exists")
      return "This person is already in your contacts. Remove the old contact on both devices before using a fresh invite."
    if (code === "contact_limit")
      return "The direct-contact limit has been reached."
    if (code === "invite_rejected")
      return "The invite could not be added. Create a fresh invite and try it once on the other device."
    if (code === "safety_key_changed")
      return "This contact's encryption identity changed. Remove the old contact state on both devices before exchanging a fresh invite."
    if (code === "group_registry_failed")
      return "Could not safely save private group state."
    if (code === "direct_state_migration_failed")
      return "Legacy direct-chat state could not be migrated safely."
    if (code === "direct_state_reinvite_required")
      return "Your OmaQ identity and contacts are intact. Some older direct-chat encryption state was safely archived and requires fresh invites."
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

  function inviteRemainingText() {
    var remaining = root.inviteRemainingSeconds
    var hours = Math.floor(remaining / 3600)
    var minutes = Math.floor((remaining % 3600) / 60)
    var seconds = remaining % 60
    function two(value) { return value < 10 ? "0" + value : String(value) }
    return two(hours) + ":" + two(minutes) + ":" + two(seconds)
  }

  function nextInviteActionRequest() {
    root.inviteActionSequence++
    return "invite-" + Date.now().toString(36) + "-" +
      root.inviteActionSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
  }

  function failInviteAction(message) {
    inviteActionTimer.stop()
    root.inviteActionPending = false
    root.inviteActionMode = ""
    root.inviteActionRequest = ""
    root.inviteActionGeneration = -1
    root.inviteFeedback = String(message || "Invite action failed. Try again.")
    root.inviteFeedbackError = true
  }

  function startInviteCreate(mode) {
    if (root.inviteActionPending)
      return false
    omaq.clearRequestError(root.inviteFeedbackRequest)
    root.inviteFeedbackRequest = ""
    root.inviteActionMode = String(mode || "create")
    root.inviteActionRequest = root.nextInviteActionRequest()
    root.inviteActionGeneration = Number(omaq.reconnectGeneration || 0)
    root.inviteActionPending = true
    root.inviteFeedback = "Creating invite…"
    root.inviteFeedbackError = false
    if (!omaq.createInvite(root.inviteActionRequest)) {
      root.failInviteAction("OmaQ is not ready to create an invite.")
      return false
    }
    inviteActionTimer.restart()
    return true
  }

  function startInviteRevoke(replaceAfter) {
    if (root.inviteActionPending)
      return false
    root.inviteConfirmMode = ""
    omaq.clearRequestError(root.inviteFeedbackRequest)
    root.inviteFeedbackRequest = ""
    root.inviteActionMode = replaceAfter ? "replace-revoke" : "revoke"
    root.inviteActionRequest = root.nextInviteActionRequest()
    root.inviteActionGeneration = Number(omaq.reconnectGeneration || 0)
    root.inviteActionPending = true
    root.inviteFeedback = replaceAfter ? "Revoking old invite…" : "Revoking invite…"
    root.inviteFeedbackError = false
    if (!omaq.revokeInvite(root.inviteActionRequest)) {
      root.failInviteAction("OmaQ is not ready to revoke this invite.")
      return false
    }
    inviteActionTimer.restart()
    return true
  }

  function copyInvite() {
    if (!omaq.inviteUrl || root.inviteRemainingSeconds <= 0)
      return
    Quickshell.execDetached(["wl-copy", "-n", omaq.inviteUrl])
    root.copied = true
    copiedTimer.restart()
  }

  function copySafetyCode() {
    if (root.currentSafetyCode === "")
      return
    Quickshell.execDetached([
      "bash", "-c",
      "if command -v wl-copy >/dev/null 2>&1; then printf '%s' \"$1\" | wl-copy -n; elif command -v xclip >/dev/null 2>&1; then printf '%s' \"$1\" | xclip -selection clipboard; fi",
      "omaq-copy-safety", root.currentSafetyCode
    ])
    root.safetyCopied = true
    safetyCopiedTimer.restart()
  }

  function selectSafetyContact(id) {
    if (!omaq.selectDirect(String(id || "")))
      return false
    root.safetyCopied = false
    root.safetyCodeVisible = false
    return true
  }

  function showSafetyCode() {
    root.safetyCopied = false
    root.safetyCodeVisible = true
    omaq.getSafety(omaq.selectedDirectId)
  }

  function hideSafetyCode() {
    root.safetyCodeVisible = false
    root.safetyCopied = false
  }

  function toggleInvite() {
    var open = !root.inviteOpen
    root.dismissTransientSections()
    root.inviteOpen = open
    if (open)
      root.inviteNow = Math.floor(Date.now() / 1000)
    if (open && !omaq.inviteUrl)
      root.startInviteCreate("create")
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

  function openRailFontSize() {
    var open = !(root.settingsOpen && root.fontSizeOpen)
    root.dismissTransientSections()
    root.settingsOpen = open
    root.fontSizeOpen = open
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

  function orderedFriendCells() {
    var friends = omaq.friends || []
    var columns = root.friendColumnCount
    var pageSize = columns * 5
    var cells = []
    for (var page = 0; page * pageSize < friends.length; page++) {
      var remaining = Math.min(pageSize, friends.length - page * pageSize)
      var rows = Math.min(5, remaining)
      for (var row = 0; row < rows; row++)
        for (var column = 0; column < columns; column++) {
          var source = page * pageSize + column * 5 + row
          cells.push(source < Math.min(friends.length, (page + 1) * pageSize)
            ? friends[source] : null)
        }
    }
    return cells
  }

  function onlineFriendCount() {
    var friends = omaq.friends || []
    var count = 0
    for (var i = 0; i < friends.length; i++)
      if (root.friendStatus(friends[i]) === "online")
        count++
    return count
  }

  function friendStatusDotColor(friend) {
    var status = root.friendStatus(friend)
    if (status === "online")
      return root.onlineStatusColor
    if (status === "afk")
      return Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.56)
    return Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.30)
  }

  function friendStatusColor(friend) {
    var status = root.friendStatus(friend)
    if (status === "online")
      return root.foreground
    if (status === "afk")
      return Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.72)
    return Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.48)
  }

  function friendKey(id) {
    var friendId = String(id || "")
    var friends = omaq.friends || []
    for (var i = 0; i < friends.length; i++)
      if (String(friends[i].id || "") === friendId)
        return String(friends[i].key || "")
    return ""
  }

  function friendMatches(id, expectedKey) {
    var key = String(expectedKey || "")
    return /^[0-9a-f]{64}$/.test(key) && root.friendKey(id) === key
  }

  function groupInviteCandidateMatches(groupId, id, expectedKey) {
    return omaq.groupInviteCandidateMatches(String(groupId || ""),
      String(id || ""), String(expectedKey || ""))
  }

  function clearStaleGroupInviteSelection() {
    if (root.groupInviteFriendId === "" ||
        root.groupInviteCandidateMatches(omaq.lastGroup,
          root.groupInviteFriendId, root.groupInviteFriendKey))
      return
    root.groupInviteGroupId = ""
    root.groupInviteFriendId = ""
    root.groupInviteFriendKey = ""
    root.groupInviteRequest = ""
    root.groupInviteGeneration = -1
    root.groupInviteFeedback = ""
  }

  function themedRadius(height) {
    var value = Number(root.controlRadius)
    var limit = Math.max(0, Number(height || root.actionButtonHeight) / 2)
    if (!isFinite(value) || value < 0)
      value = Number(root.panelRadius)
    if (!isFinite(value) || value < 0)
      value = 0
    return Math.min(value, limit)
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

  function identityPassphraseStats(value) {
    var text = String(value || "")
    var bytes = 0
    var characters = 0
    for (var i = 0; i < text.length; i++) {
      var code = text.charCodeAt(i)
      if ((code < 0x20 && code !== 9) || code === 0x7f)
        return { valid: false, bytes: bytes, characters: characters }
      if (code < 0x80) {
        bytes += 1
      } else if (code < 0x800) {
        bytes += 2
      } else if (code >= 0xd800 && code <= 0xdbff) {
        if (i + 1 >= text.length)
          return { valid: false, bytes: bytes, characters: characters }
        var low = text.charCodeAt(i + 1)
        if (low < 0xdc00 || low > 0xdfff)
          return { valid: false, bytes: bytes, characters: characters }
        bytes += 4
        i++
      } else if (code >= 0xdc00 && code <= 0xdfff) {
        return { valid: false, bytes: bytes, characters: characters }
      } else {
        bytes += 3
      }
      characters++
    }
    return { valid: bytes > 0 && bytes <= 128, bytes: bytes,
      characters: characters }
  }

  function identityPathValid(value) {
    var text = String(value || "")
    if (text.length === 0 || text.charAt(0) !== "/" || text.indexOf("..") >= 0)
      return false
    var bytes = 0
    for (var i = 0; i < text.length; i++) {
      var code = text.charCodeAt(i)
      if (code < 0x20 || code === 0x7f)
        return false
      if (code < 0x80) {
        bytes += 1
      } else if (code < 0x800) {
        bytes += 2
      } else if (code >= 0xd800 && code <= 0xdbff) {
        if (i + 1 >= text.length)
          return false
        var low = text.charCodeAt(i + 1)
        if (low < 0xdc00 || low > 0xdfff)
          return false
        bytes += 4
        i++
      } else if (code >= 0xdc00 && code <= 0xdfff) {
        return false
      } else {
        bytes += 3
      }
    }
    return bytes < 512
  }

  function identityExistingPassphraseValid(value) {
    return root.identityPassphraseStats(value).valid
  }

  function identityNewPassphraseValid(value) {
    var stats = root.identityPassphraseStats(value)
    return stats.valid && stats.characters >= 8
  }

  function identityFailureText(code) {
    if (code === "locked")
      return "The identity passphrase is incorrect."
    if (code === "identity_exists")
      return "An identity already exists. Use Import identity to continue."
    if (code === "identity_passphrase_required")
      return "Enter the imported identity's passphrase first."
    if (code === "identity_import_failed")
      return "Identity file or passphrase is invalid."
    if (code === "busy")
      return "Finish active calls, transfers, invites, or read receipts first."
    if (code === "unsupported")
      return "This identity action is not supported by the running helper."
    if (code === "forbidden") {
      if (root.identityAction === "protect")
        return "Use at least 8 characters and at most 128 bytes."
      if (root.identityAction === "unprotect")
        return "The current identity passphrase is incorrect."
      if (root.identityAction === "export")
        return "The identity could not be exported to that path."
    }
    return root.errorText(code)
  }

  function clearIdentityFeedback() {
    var request = root.identityFeedbackRequest
    root.identityFeedback = ""
    root.identityFeedbackRequest = ""
    root.identityFeedbackError = false
    omaq.clearIdentityError(request)
  }

  function finishIdentityAction(success, message) {
    identityActionTimer.stop()
    root.identityActionPending = false
    root.identityFeedbackError = !success
    root.identityFeedback = String(message || "")
    root.identityAction = ""
    root.identityRequest = ""
    passField.text = ""
    unlockField.text = ""
  }

  function runIdentityAction(action, path, secret) {
    if (root.identityActionPending)
      return false
    var key = String(action || "")
    if (key !== "unlock" && !omaq.supportsIdentityActions) {
      root.identityFeedbackError = true
      root.identityFeedback = "Update the local OmaQ helper before changing identity settings."
      return false
    }
    var selectedPath = String(path || "").trim()
    var passphrase = secret === undefined ? passField.text : String(secret || "")
    var sent = false
    if ((key === "export" || key === "import" || key === "replace") &&
        !root.identityPathValid(selectedPath)) {
      root.identityFeedbackError = true
      root.identityFeedback = "Choose a valid absolute identity-bundle path."
      return false
    }
    if (key === "protect" && !root.identityNewPassphraseValid(passphrase)) {
      root.identityFeedbackError = true
      root.identityFeedback = "Use at least 8 characters and at most 128 bytes."
      return false
    }
    if ((key === "unlock" || key === "unprotect") &&
        !root.identityExistingPassphraseValid(passphrase)) {
      root.identityFeedbackError = true
      root.identityFeedback = "Enter the current identity passphrase."
      return false
    }
    if ((key === "import" || key === "replace") && passphrase !== "" &&
        !root.identityExistingPassphraseValid(passphrase)) {
      root.identityFeedbackError = true
      root.identityFeedback = "The imported passphrase is longer than 128 bytes or invalid."
      return false
    }
    root.identityRequestSequence = root.identityRequestSequence + 1
    root.identityRequest = Date.now().toString(36) + "-identity-" +
      root.identityRequestSequence.toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36)
    root.identityAction = key
    root.identityFeedbackRequest = root.identityRequest
    root.identityActionPending = true
    root.identityFeedbackError = false
    root.identityFeedback = key === "export" ? "Exporting identity…"
      : key === "import" ? "Checking identity bundle…"
      : key === "replace" ? "Importing identity…"
      : key === "unprotect" ? "Removing identity protection…"
      : key === "unlock" ? "Unlocking identity…"
      : "Protecting identity…"
    if (key === "unlock")
      sent = omaq.unlockIdentity(passphrase, root.identityRequest)
    else if (key === "protect")
      sent = omaq.protectIdentity(passphrase, root.identityRequest)
    else if (key === "unprotect")
      sent = omaq.unprotectIdentity(passphrase, root.identityRequest)
    else if (key === "export")
      sent = omaq.exportIdentity(selectedPath, root.identityRequest)
    else if (key === "import")
      sent = omaq.inspectIdentity(selectedPath, passphrase, root.identityRequest)
    else if (key === "replace")
      sent = omaq.importIdentity(selectedPath, true, passphrase, root.identityRequest)
    passField.text = ""
    unlockField.text = ""
    if (!sent) {
      root.finishIdentityAction(false, "OmaQ is not ready for that identity action.")
      return false
    }
    identityActionTimer.restart()
    return true
  }

  function startIdentityPicker(mode) {
    if (!omaq.supportsIdentityActions || root.identityActionPending || identityPick.running)
      return
    root.identityPickerMode = String(mode || "import")
    root.identityPickerExitCode = -1
    root.identityPickerStreamDone = false
    root.identityActionPending = true
    omaq.clearIdentityError(root.identityFeedbackRequest)
    root.identityFeedbackRequest = ""
    root.identityFeedbackError = false
    root.identityFeedback = root.identityPickerMode === "export"
      ? "Choose where to export the private identity bundle."
      : "Choose an OmaQ identity bundle."
    identityPick.running = false
    identityPick.running = true
  }

  function finishIdentityPicker() {
    if (root.identityPickerExitCode < 0 || !root.identityPickerStreamDone)
      return
    var mode = root.identityPickerMode
    var path = String(identityPickOutput.text || "").trim()
    var code = root.identityPickerExitCode
    root.identityPickerExitCode = -1
    root.identityPickerStreamDone = false
    root.identityPickerMode = ""
    root.identityActionPending = false
    if (code === 0 && path !== "" && !root.identityPathValid(path)) {
      root.identityFeedbackError = true
      root.identityFeedback = "The selected identity-bundle path is invalid."
      return
    }
    if (code !== 0 || path === "") {
      if (code !== 0 && code !== 1) {
        root.identityFeedbackError = true
        root.identityFeedback = "No supported file picker is available."
      } else {
        root.identityFeedbackError = false
        root.identityFeedback = ""
      }
      return
    }
    if (mode === "export") {
      root.runIdentityAction("export", path)
      return
    }
    importPath.text = path
    if (mode === "replace") {
      root.replaceIdentityPath = path
      root.replaceIdentityConfirm = true
      root.identityFeedbackError = false
      root.identityFeedback = "Review the selected bundle before importing this identity."
      return
    }
    root.runIdentityAction("import", path)
  }

  function resetIdentityControls() {
    if (!root.identityActionPending && !identityPick.running) {
      var feedbackRequest = root.identityFeedbackRequest
      root.identityAction = ""
      root.identityRequest = ""
      root.identityFeedback = ""
      root.identityFeedbackRequest = ""
      root.identityFeedbackError = false
      passField.text = ""
      unlockField.text = ""
      importPath.text = ""
      omaq.clearIdentityError(feedbackRequest)
    }
    root.replaceIdentityConfirm = false
    root.replaceIdentityPath = ""
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
    if (!id || !omaq.selectDirect(String(id)))
      return
    if (chatSurface) {
      chatSurface.openConversation(String(id), name || "")
    }
    omaq.markConversationRead(String(id))
    root.close()
  }

  function openGroup(id) {
    var groupId = String(id || "")
    if (!omaq.selectGroup(groupId, true))
      return
    if (chatSurface) {
      chatSurface.openConversation(groupId, omaq.groupName(groupId))
    }
    omaq.markConversationRead(groupId)
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

  function verifyPersistedSettings(raw) {
    if (!root.settingsPersistencePending)
      return
    var config
    try { config = JSON.parse(String(raw || "")) } catch (error) {
      root.settingsPersistenceWarning = "This setting is active for this session but could not be saved."
      return
    }
    var candidates = []
    var layout = config && config.bar && config.bar.layout
    var sections = ["left", "center", "right"]
    var section
    if (layout) {
      for (section = 0; section < sections.length; section++) {
        var entries = layout[sections[section]] || []
        for (var entryIndex = 0; entryIndex < entries.length; entryIndex++)
          candidates.push(entries[entryIndex])
      }
    }
    var plugins = config && Array.isArray(config.plugins) ? config.plugins : []
    for (var pluginIndex = 0; pluginIndex < plugins.length; pluginIndex++)
      candidates.push(plugins[pluginIndex])
    var expected = root.settingsPersistenceExpected || {}
    var matched = false
    for (var candidateIndex = 0; candidateIndex < candidates.length; candidateIndex++) {
      var candidate = candidates[candidateIndex]
      if (!candidate || String(candidate.id || "") !== root.moduleName)
        continue
      matched = true
      for (var key in expected)
        if (key !== "id" && candidate[key] !== expected[key]) {
          matched = false
          break
        }
      if (matched)
        break
    }
    if (!matched && root.settingsPersistenceAttempts < 4) {
      root.settingsPersistenceAttempts = root.settingsPersistenceAttempts + 1
      settingsVerifyTimer.restart()
      return
    }
    root.settingsPersistencePending = false
    root.settingsPersistenceWarning = matched ? "" :
      "This setting is active for this session but could not be saved."
  }

  function persistSettings(values) {
    var entry = { id: root.moduleName }
    var changed = false
    var existing
    for (existing in root.settings)
      if (existing !== "id")
        entry[existing] = root.settings[existing]
    var key
    for (key in values) {
      if (entry[key] !== values[key])
        changed = true
      entry[key] = values[key]
    }
    root.settings = entry
    if (!root.bar || !root.bar.shell ||
        typeof root.bar.shell.updateEntryInline !== "function") {
      root.settingsPersistencePending = false
      root.settingsPersistenceWarning = "This setting is active for this session but could not be saved."
      return false
    }
    try {
      var result = root.bar.shell.updateEntryInline(root.moduleName, entry)
      if (changed && result === false) {
        // False also means the host already has this exact entry; verify the
        // durable file before deciding whether persistence failed.
        root.settingsPersistenceExpected = entry
        root.settingsPersistenceAttempts = 0
        root.settingsPersistencePending = true
        settingsVerifyTimer.restart()
        return true
      }
      if (changed) {
        root.settingsPersistenceExpected = entry
        root.settingsPersistenceAttempts = 0
        root.settingsPersistencePending = true
        settingsVerifyTimer.restart()
      }
      return true
    } catch (error) {
      root.settingsPersistencePending = false
      root.settingsPersistenceWarning = "This setting is active for this session but could not be saved."
      return false
    }
  }

  function setTheme(name) {
    root.persistSettings({ chatTheme: name })
  }

  readonly property real messageScale: {
    var value = Number(root.settings && root.settings.messageScale)
    return [0.9, 1.0, 1.1, 1.2, 1.4].indexOf(value) >= 0 ? value : 1.0
  }

  function setMessageScale(value) {
    var scale = Number(value)
    if ([0.9, 1.0, 1.1, 1.2, 1.4].indexOf(scale) < 0)
      return
    root.persistSettings({ messageScale: scale })
  }

  readonly property string notificationSound: {
    var value = String(root.settings && root.settings.sound || "icq-message")
    if (value === "custom")
      return value
    for (var i = 0; i < root.bundledNotificationSounds.length; i++)
      if (String(root.bundledNotificationSounds[i].id || "") === value)
        return value
    return "icq-message"
  }
  readonly property string notificationSoundPath:
    String(root.settings && root.settings.soundCustomPath || "")
  readonly property string notificationSoundId:
    String(root.settings && root.settings.soundCustomId || "")

  function notificationSoundSelected(option) {
    var sound = option || ({})
    if (sound.custom)
      return root.notificationSound === "custom" &&
        ((root.notificationSoundId !== "" &&
          root.notificationSoundId === String(sound.soundId || "")) ||
         (root.notificationSoundId === "" &&
          root.notificationSoundPath === String(sound.path || "")))
    return root.notificationSound === String(sound.id || "")
  }

  function selectedCustomSoundOption() {
    for (var i = 0; i < root.notificationSounds.length; i++)
      if (root.notificationSounds[i].custom &&
          root.notificationSoundSelected(root.notificationSounds[i]))
        return root.notificationSounds[i]
    return null
  }

  function setNotificationSound(option) {
    var sound = option || ({})
    var selectedSound = sound.custom ? "custom" : String(sound.id || "off")
    var selectedPath = sound.custom ? String(sound.path || "") : ""
    if (sound.custom && (selectedPath.charAt(0) !== "/" ||
        !/^[0-9a-f]{32}$/.test(String(sound.soundId || ""))))
      return
    root.persistSettings({ sound: selectedSound, soundCustomPath: selectedPath,
      soundCustomId: sound.custom ? String(sound.soundId || "") : "" })
    root.soundRemoveConfirm = false
    if (chatSurface)
      chatSurface.previewSound(selectedSound)
  }

  function startSoundPicker() {
    if (!omaq.supportsCustomSounds || soundPick.running ||
        root.soundActionRequest !== "")
      return
    root.soundPickerExitCode = -1
    root.soundPickerStreamDone = false
    root.soundFeedback = "Choose a PCM WAV notification sound to import."
    root.soundFeedbackError = false
    soundPick.running = false
    soundPick.running = true
  }

  function finishSoundPicker() {
    if (root.soundPickerExitCode < 0 || !root.soundPickerStreamDone)
      return
    var code = root.soundPickerExitCode
    var path = String(soundPickOutput.text || "").trim()
    root.soundPickerExitCode = -1
    root.soundPickerStreamDone = false
    if (code !== 0 || path === "") {
      root.soundFeedbackError = code !== 0 && code !== 1
      root.soundFeedback = root.soundFeedbackError
        ? "No supported file picker is available." : ""
      return
    }
    if (path.charAt(0) !== "/" || path.length > 511) {
      root.soundFeedbackError = true
      root.soundFeedback = "Choose a valid absolute sound-file path."
      return
    }
    var request = omaq.nextSoundRequest("import")
    if (!omaq.importCustomSound(path, request)) {
      root.soundFeedbackError = true
      root.soundFeedback = "OmaQ is not ready to import that sound."
      return
    }
    root.soundAction = "import"
    root.soundActionRequest = request
    root.soundFeedback = "Importing notification sound…"
    root.soundFeedbackError = false
    soundActionTimer.restart()
  }

  function requestSoundRemoval(option) {
    var sound = option || ({})
    if (!sound.custom || !/^[0-9a-f]{32}$/.test(String(sound.soundId || "")))
      return
    root.soundRemoveId = String(sound.soundId)
    root.soundRemoveLabel = String(sound.label || "Custom sound")
    root.soundRemovePath = String(sound.path || "")
    root.soundRemoveConfirm = true
  }

  function confirmSoundRemoval() {
    if (!root.soundRemoveConfirm || root.soundActionRequest !== "" ||
        !/^[0-9a-f]{32}$/.test(root.soundRemoveId))
      return
    var request = omaq.nextSoundRequest("remove")
    if (!omaq.removeCustomSound(root.soundRemoveId, request)) {
      root.soundFeedbackError = true
      root.soundFeedback = "OmaQ is not ready to remove that sound."
      return
    }
    root.soundAction = "remove"
    root.soundActionRequest = request
    root.soundRemoveConfirm = false
    root.soundFeedback = "Removing managed notification sound…"
    root.soundFeedbackError = false
    soundActionTimer.restart()
  }

  function toggleThemeSettings() {
    root.themeOpen = !root.themeOpen
    if (root.themeOpen) {
      root.soundOpen = false
      root.fontSizeOpen = false
    }
  }

  function toggleSoundSettings() {
    root.soundOpen = !root.soundOpen
    if (root.soundOpen) {
      root.themeOpen = false
      root.fontSizeOpen = false
    }
  }

  function openRepo() {
    Quickshell.execDetached(["xdg-open", "https://github.com/HANCORE-linux/OmaQ"])
  }

  function openKoFi() {
    Quickshell.execDetached(["xdg-open", "https://ko-fi.com/hancore"])
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
    var margin = gap
    var sw = popup.screen.width
    var sh = popup.screen.height
    var horizontalBar = root.barPos === "top" || root.barPos === "bottom"
    root.cardHeightLimit = Math.max(Style.space(260),
      sh - (horizontalBar ? root.barThickness + gap + margin : margin * 2))

    var x = p.x + button.width / 2 - card.width / 2
    var y = root.barThickness + gap
    if (root.barPos === "bottom") {
      y = sh - root.barThickness - gap - card.height
    } else if (root.barPos === "left") {
      x = root.barThickness + gap
      y = p.y + button.height / 2 - card.height / 2
    } else if (root.barPos === "right") {
      x = sw - root.barThickness - gap - card.width
      y = p.y + button.height / 2 - card.height / 2
    }
    x = Math.max(margin, Math.min(x, sw - card.width - margin))
    y = Math.max(margin, Math.min(y, sh - card.height - margin))
    card.x = Math.round(x)
    card.y = Math.round(y)
  }

  FileView {
    id: settingsVerifyFile
    path: root.bar && root.bar.shell && root.bar.shell.userConfigPath
      ? String(root.bar.shell.userConfigPath) : ""
    watchChanges: true
    printErrors: false
    onLoaded: root.verifyPersistedSettings(text())
    onLoadFailed: {
      if (root.settingsPersistencePending && root.settingsPersistenceAttempts < 4) {
        root.settingsPersistenceAttempts = root.settingsPersistenceAttempts + 1
        settingsVerifyTimer.restart()
      } else if (root.settingsPersistencePending) {
        root.settingsPersistencePending = false
        root.settingsPersistenceWarning =
          "This setting is active for this session but could not be saved."
      }
    }
    onFileChanged: reload()
  }

  Timer {
    id: settingsVerifyTimer
    interval: 500
    repeat: false
    onTriggered: {
      if (root.settingsPersistencePending && settingsVerifyFile.path !== "")
        settingsVerifyFile.reload()
    }
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
    onRunningChanged: if (!running && root.opened)
      Qt.callLater(function() { if (root.opened) panelFocus.forceActiveFocus() })
  }

  Process {
    id: soundPick
    running: false
    command: ["bash", "-c", root.soundPickerScript, "omaq-sound-picker"]
    stdout: StdioCollector {
      id: soundPickOutput
      waitForEnd: true
      onStreamFinished: {
        root.soundPickerStreamDone = true
        root.finishSoundPicker()
      }
    }
    onExited: function(code) {
      root.soundPickerExitCode = code
      root.finishSoundPicker()
    }
    onRunningChanged: if (!running && root.opened)
      Qt.callLater(function() { if (root.opened) panelFocus.forceActiveFocus() })
  }

  Process {
    id: identityPick
    running: false
    command: ["bash", "-c", root.identityPickerScript,
              "omaq-identity-picker", root.identityPickerMode]
    stdout: StdioCollector {
      id: identityPickOutput
      waitForEnd: true
      onStreamFinished: {
        root.identityPickerStreamDone = true
        root.finishIdentityPicker()
      }
    }
    onExited: function(code) {
      root.identityPickerExitCode = code
      root.finishIdentityPicker()
    }
    onRunningChanged: if (!running && root.opened)
      Qt.callLater(function() { if (root.opened) panelFocus.forceActiveFocus() })
  }

  Timer {
    id: inviteClock
    interval: 1000
    repeat: true
    running: root.opened && root.inviteOpen && omaq.inviteUrl !== ""
    triggeredOnStart: true
    onTriggered: {
      root.inviteNow = Math.floor(Date.now() / 1000)
      if (omaq.inviteUrl !== "" && Number(omaq.inviteExpiresAt || 0) > 0 &&
          root.inviteRemainingSeconds <= 0) {
        omaq.inviteUrl = ""
        omaq.inviteExpiresAt = 0
        omaq.qrPath = ""
        root.copied = false
        root.inviteFeedback = "Invite expired. Create a new link."
        root.inviteFeedbackError = false
      }
    }
  }

  Timer {
    id: inviteActionTimer
    interval: 10000
    repeat: false
    onTriggered: {
      root.inviteActionPending = false
      root.inviteActionMode = ""
      root.inviteActionRequest = ""
      root.inviteFeedback = "Invite action timed out. Check OmaQ status and try again."
      root.inviteFeedbackError = true
    }
  }

  Timer {
    id: nicknameSubmitTimer
    interval: 10000
    repeat: false
    onTriggered: {
      root.nicknameSubmitPending = false
      root.nicknameFeedbackRequest = root.nicknameRequest
      root.nicknameFeedback = "Nickname update timed out. Try again."
      root.nicknameFeedbackError = true
      root.nicknameRequest = ""
    }
  }

  Timer {
    id: soundActionTimer
    interval: 12000
    repeat: false
    onTriggered: {
      root.soundFeedback = "The sound action is delayed. OmaQ will apply its correlated result after reconnecting."
      root.soundFeedbackError = true
    }
  }

  Timer {
    id: identityActionTimer
    interval: 20000
    repeat: false
    onTriggered: root.finishIdentityAction(false,
      "The identity action timed out. Check OmaQ status and try again.")
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
    instanceName: button.QsWindow && button.QsWindow.window &&
      button.QsWindow.window.screen ? String(button.QsWindow.window.screen.name || "default") : "default"
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
    function onFriendsChanged() {
      if (root.removeContactPickerOpen || root.removeContactConfirm) {
        root.removeContactPickerOpen = false
        root.removeContactConfirm = false
        root.removeContactId = ""
        root.removeContactKey = ""
      }
      root.clearStaleGroupInviteSelection()
    }
    function onGroupsChanged() {
      root.clearStaleGroupInviteSelection()
    }
    function onSoundTickChanged() {
      if (root.soundActionRequest === "" ||
          String(omaq.lastSoundRequest || "") !== root.soundActionRequest ||
          String(omaq.lastSoundOperation || "") !== root.soundAction)
        return
      soundActionTimer.stop()
      var action = root.soundAction
      root.soundActionRequest = ""
      root.soundAction = ""
      if (!omaq.lastSoundSucceeded) {
        root.soundFeedbackError = true
        root.soundFeedback = omaq.lastSoundCode === "invalid_sound"
          ? "Choose a valid PCM WAV file up to 8 MiB and 30 seconds."
          : (omaq.lastSoundCode === "sound_remove_failed"
            ? "The managed sound copy could not be removed." :
              "Custom sound storage is unavailable.")
        if (action === "remove" && root.soundRemoveId !== "")
          root.soundRemoveConfirm = true
        return
      }
      if (action === "import") {
        var selectedId = String(omaq.lastSoundSelected || "")
        var imported = null
        var sounds = omaq.customSounds || []
        for (var i = 0; i < sounds.length; i++)
          if (String(sounds[i].id || "") === selectedId) {
            imported = sounds[i]
            break
          }
        if (!imported) {
          root.soundFeedbackError = true
          root.soundFeedback = "The imported sound could not be verified."
          return
        }
        root.persistSettings({ sound: "custom",
          soundCustomPath: String(imported.path || ""),
          soundCustomId: String(imported.id || "") })
        root.soundFeedback = "Imported " + String(imported.label || "custom sound") + "."
        root.soundFeedbackError = false
        if (chatSurface)
          chatSurface.previewSound("custom")
      } else {
        if (root.notificationSound === "custom" &&
            root.notificationSoundPath === root.soundRemovePath)
          root.persistSettings({ sound: "off", soundCustomPath: "",
            soundCustomId: "" })
        root.soundFeedback = "Removed the OmaQ-managed sound copy."
        root.soundFeedbackError = false
        root.soundRemoveId = ""
        root.soundRemoveLabel = ""
        root.soundRemovePath = ""
        root.soundRemoveConfirm = false
      }
    }
    function onInviteUrlChanged() {
      root.inviteNow = Math.floor(Date.now() / 1000)
      root.refreshPanelLayout()
      if (omaq.inviteUrl !== "")
        omaq.saveQr()
    }
    function onQrPathChanged() { root.refreshPanelLayout() }
    function onInviteActionTickChanged() {
      if (!root.inviteActionPending || root.inviteActionRequest === "" ||
          String(omaq.lastInviteRequest || "") !== root.inviteActionRequest)
        return
      var op = String(omaq.lastInviteAction || "")
      inviteActionTimer.stop()
      if (op === "revoke" && root.inviteActionMode === "replace-revoke") {
        root.inviteActionPending = false
        root.inviteActionRequest = ""
        root.startInviteCreate("replace-create")
      } else if (op === "revoke") {
        root.inviteActionPending = false
        root.inviteActionMode = ""
        root.inviteActionRequest = ""
        root.inviteActionGeneration = -1
        root.inviteFeedbackRequest = ""
        root.inviteFeedback = "Invite revoked."
        root.inviteFeedbackError = false
        root.copied = false
      } else if (op === "create") {
        var replaced = root.inviteActionMode === "replace-create"
        root.inviteActionPending = false
        root.inviteActionMode = ""
        root.inviteActionRequest = ""
        root.inviteActionGeneration = -1
        root.inviteFeedbackRequest = ""
        root.inviteFeedback = replaced ? "New invite created." : "Invite ready."
        root.inviteFeedbackError = false
      }
    }
    function onIdentityPrimaryTickChanged() {
      if (root.identityPrimaryRequest !== "" &&
          String(omaq.lastIdentityPrimaryRequest || "") === root.identityPrimaryRequest) {
        root.identityPrimaryRequest = ""
        root.identityPrimaryConfirm = false
      }
    }
    function onRedeemTickChanged() {
      if (root.redeemRequest !== "" &&
          String(omaq.lastRedeemRequest || "") === root.redeemRequest) {
        root.redeemRequest = ""
        root.redeemFeedbackRequest = ""
        root.redeemFeedback = omaq.lastRedeemKind === "group"
          ? "Group invite checked. Waiting for the incoming group request."
          : "Invite checked. Waiting for the other person to accept."
        root.redeemDraft = ""
      }
    }
    function onDirectReinviteTickChanged() {
      if (root.directReinviteRequest !== "" &&
          String(omaq.lastDirectReinviteRequest || "") === root.directReinviteRequest) {
        root.directReinviteRequest = ""
        root.directReinviteClearConfirm = false
      }
    }
    function onGroupInviteSentTickChanged() {
      if (root.groupInviteFeedback === "Sending group invite…" &&
          String(omaq.lastGroupInviteSentGroup || "") === root.groupInviteGroupId &&
          String(omaq.lastGroupInviteSentFriend || "") === root.groupInviteFriendId &&
          String(omaq.lastGroupInviteSentRequest || "") === root.groupInviteRequest)
        root.groupInviteFeedback = "Invitation sent · waiting for acceptance"
    }
    function onGroupInviteFailedTickChanged() {
      if (root.groupInviteFeedback === "Sending group invite…" &&
          String(omaq.lastGroupInviteFailedGroup || "") === root.groupInviteGroupId &&
          String(omaq.lastGroupInviteFailedFriend || "") === root.groupInviteFriendId &&
          String(omaq.lastGroupInviteFailedRequest || "") === root.groupInviteRequest)
        root.groupInviteFeedback = omaq.lastGroupInviteFailedCode === "busy"
          ? "Recipient still has a group invitation waiting for a decision"
          : "Group invite failed"
    }
    function onReconnectGenerationChanged() {
      if (root.inviteActionPending && root.inviteActionGeneration >= 0 &&
          root.inviteActionGeneration !== Number(omaq.reconnectGeneration || 0))
        root.failInviteAction("OmaQ restarted before the invite action completed.")
      if (root.identityPrimaryRequest !== "") {
        root.identityPrimaryRequest = ""
        root.identityPrimaryConfirm = false
      }
      if (root.directReinviteRequest !== "") {
        root.directReinviteRequest = ""
        root.directReinviteClearConfirm = false
      }
      if (root.redeemRequest !== "") {
        root.redeemFeedback = "Connection changed before the invite result was confirmed. Check your contacts before trying again."
        root.redeemFeedbackRequest = root.redeemRequest
        root.redeemRequest = ""
      }
    }
    function onHelperInstanceGenerationChanged() {
      if (root.identityPrimaryRequest !== "") {
        root.identityPrimaryRequest = ""
        root.identityPrimaryConfirm = false
      }
      if (root.directReinviteRequest !== "") {
        root.directReinviteRequest = ""
        root.directReinviteClearConfirm = false
      }
      if (root.redeemRequest !== "") {
        root.redeemFeedback = "OmaQ restarted before the invite result was confirmed. Check your contacts before trying again."
        root.redeemFeedbackRequest = root.redeemRequest
        root.redeemRequest = ""
      }
      if (root.groupInviteFeedback === "Sending group invite…" &&
          root.groupInviteGeneration >= 0 &&
          root.groupInviteGeneration !== Number(omaq.helperInstanceGeneration || 0)) {
          root.groupInviteFeedback = "Group invite failed"
          root.groupInviteRequest = ""
          root.groupInviteGeneration = -1
        }
    }
    function onHelperCompatibilityChanged() {
      if (root.inviteActionPending && omaq.helperCompatibility === "incompatible")
        root.failInviteAction("The running helper does not support this invite action.")
      if (root.groupInviteFeedback === "Sending group invite…" &&
          omaq.helperCompatibility === "incompatible") {
        root.groupInviteFeedback = "Group invite failed"
        root.groupInviteRequest = ""
        root.groupInviteGeneration = -1
      }
    }
    function onIdentityActionTickChanged() {
      if (!root.identityActionPending || root.identityRequest === "")
        return
      var op = String(omaq.lastIdentityOp || "")
      var legacyUnlock = root.identityAction === "unlock" &&
        !omaq.supportsIdentityActions && op === "unlock"
      if (!legacyUnlock &&
          String(omaq.lastIdentityRequest || "") !== root.identityRequest)
        return
      if (root.identityAction === "unlock" && op === "unlock")
        root.finishIdentityAction(true, "Identity unlocked.")
      else if (root.identityAction === "protect" && op === "protect")
        root.finishIdentityAction(true, "Identity protection enabled.")
      else if (root.identityAction === "unprotect" &&
               (op === "unprotect" || (op === "protect" && !omaq.lastIdentityProtected)))
        root.finishIdentityAction(true, "Identity protection removed.")
      else if (root.identityAction === "export" && op === "export")
        root.finishIdentityAction(true, "Identity exported to " +
          String(omaq.lastIdentityPath || "the selected file") + ".")
      else if (root.identityAction === "import" && op === "inspect")
        root.finishIdentityAction(true,
          "Identity bundle validated. Use Import identity to activate it.")
      else if (root.identityAction === "replace" && op === "import")
        root.finishIdentityAction(true,
          "Identity imported. Existing direct contacts must be removed on both sides and re-invited before messaging.")
    }
    function onLastErrorTickChanged() {
      if (root.inviteActionPending && root.inviteActionRequest !== "" &&
          String(omaq.lastErrorRequest || "") === root.inviteActionRequest) {
        var inviteError = String(omaq.lastError || "")
        root.inviteFeedbackRequest = root.inviteActionRequest
        root.failInviteAction(inviteError === "locked"
          ? "Unlock your identity before changing the invite."
          : inviteError === "no_ratchet"
            ? "Secure invitation setup is unavailable."
            : inviteError === "identity_changed"
              ? "Identity changed before the invite action completed."
              : inviteError === "unsupported"
                ? "This invite action is not supported by the running helper."
                : "Invite action failed. Try again.")
      }
      if (root.redeemRequest !== "" &&
          String(omaq.lastErrorRequest || "") === root.redeemRequest) {
        root.redeemFeedbackRequest = root.redeemRequest
        root.redeemFeedback = root.errorText(omaq.lastError)
        root.redeemRequest = ""
      }
      if (root.identityPrimaryRequest !== "" &&
          String(omaq.lastErrorRequest || "") === root.identityPrimaryRequest) {
        root.identityPrimaryRequest = ""
        root.identityPrimaryConfirm = false
      }
      if (root.directReinviteRequest !== "" &&
          String(omaq.lastErrorRequest || "") === root.directReinviteRequest) {
        root.directReinviteRequest = ""
        root.directReinviteClearConfirm = false
      }
      if (root.identityActionPending && root.identityAction !== "" &&
          root.identityRequest !== "" &&
          String(omaq.lastErrorRequest || "") === root.identityRequest)
        root.finishIdentityAction(false, root.identityFailureText(omaq.lastError))
      else if (root.identityActionPending && root.identityAction === "unlock" &&
               !omaq.supportsIdentityActions &&
               String(omaq.lastErrorRequest || "") === "" &&
               ["locked", "forbidden"].indexOf(String(omaq.lastError || "")) >= 0)
        root.finishIdentityAction(false, root.identityFailureText(omaq.lastError))
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
    slotSize: Math.max(Style.bar.iconCanvas + Style.space(6),
                       Style.bar.iconSlot - Style.space(2))
    opticalSize: Style.bar.iconCanvas + Style.space(2)
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
      ? "Incoming call"
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
    color: root.systemColors[1] || root.urgent
    border.width: 0
    anchors.verticalCenter: button.verticalCenter
    anchors.verticalCenterOffset: -Style.space(6)
    anchors.horizontalCenter: button.horizontalCenter
    anchors.horizontalCenterOffset: Style.space(7)
    z: 100

    SafeText {
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

    property bool observedActiveFocus: false
    readonly property bool backingWindowActive: {
      var window = panelFocus.QsWindow.window
      return !!(window && window.active)
    }

    WlrLayershell.namespace: "omaq-panel"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: root.opened ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None

    anchors { top: true; bottom: true; left: true; right: true }

    // A stuck panel must never retain a desktop-sized Wayland input region.
    // Only the visible card is clickable; clicks elsewhere pass through.
    mask: Region { item: card }

    HyprlandFocusGrab {
      id: clickAwayGrab
      active: root.opened && popup.visible && !root.avatarRestorePending &&
        root.identityPickerMode === "" && !avatarPick.running && !identityPick.running &&
        !soundPick.running
      windows: [popup]
      onCleared: if (root.opened && !root.avatarRestorePending &&
                     root.identityPickerMode === "" &&
                     !avatarPick.running && !identityPick.running && !soundPick.running)
        root.close()
    }

    onVisibleChanged: {
      if (!visible) {
        observedActiveFocus = false
        return
      }
      root.cardHeightLimit = 0
      Qt.callLater(function() {
        if (!root.opened)
          return
        root.placeCard()
        root.publishConnectedGeometry()
        panelFocus.forceActiveFocus()
      })
    }
    onBackingWindowActiveChanged: {
      if (backingWindowActive) {
        observedActiveFocus = true
        return
      }
      if (!observedActiveFocus || !root.opened || avatarPick.running ||
          identityPick.running || soundPick.running)
        return
      Qt.callLater(function() {
        if (root.opened && !popup.backingWindowActive &&
            !avatarPick.running && !identityPick.running && !soundPick.running)
          root.close()
      })
    }

    Connections {
      target: popup.screen
      function onGeometryChanged() { if (root.opened) root.placeCard() }
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
      height: Math.min(heroVisual.height + root.panelSectionGap +
                       root.primaryAreaHeight + root.pad * 2,
                       root.cardHeightLimit > 0 ? root.cardHeightLimit :
                         (popup.screen ? Math.max(Style.space(260),
                           popup.screen.height - Style.space(24)) : Style.space(720)))
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

        Rectangle {
          id: heroVisual
          anchors.top: parent.top
          anchors.topMargin: root.pad
          anchors.left: parent.left
          anchors.leftMargin: root.pad
          width: Math.max(0, parent.width - root.pad * 2 - root.railWidth -
                          root.panelSectionGap)
          height: root.headerHeight
          radius: root.themedRadius(height)
          color: omaq.pending
            ? Qt.rgba((root.systemColors[3] || root.controlAccent).r,
                (root.systemColors[3] || root.controlAccent).g,
                (root.systemColors[3] || root.controlAccent).b,
                pendingHeaderHover.hovered ? 0.16 : 0.09)
            : "transparent"
          border.color: omaq.pending
            ? (root.systemColors[3] || root.controlAccent) : root.controlBorder
          border.width: 1
          clip: true
          z: 20

          HoverHandler {
            id: pendingHeaderHover
            enabled: omaq.pending
          }

          Row {
            id: heroHeaderRow
            anchors.fill: parent
            anchors.margins: root.framePadding
            spacing: Style.space(8)

            AvatarPic {
              id: selfHeaderAvatar
              visible: !omaq.pending
              anchors.verticalCenter: parent.verticalCenter
              px: Style.space(34)
              path: omaq.selfAvatar
              online: omaq.selfOnline
              revision: omaq.avatarTick
              onClicked: root.pickSelfAvatar()
            }

            Column {
              id: selfHeaderContent
              visible: !omaq.pending
              width: Math.max(0, parent.width - selfHeaderAvatar.width -
                              parent.spacing)
              anchors.verticalCenter: parent.verticalCenter
              spacing: 0

              SafeText {
                visible: omaq.selfNickname !== "" && !root.nicknameEditOpen
                width: parent.width
                height: root.nicknameControlHeight
                text: omaq.selfNickname
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
                font.bold: true
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
                height: root.nicknameEditorHeight
                spacing: root.btnGap
                TokenTextField {
                  id: nicknameField
                  width: parent.width - nicknameButton.implicitWidth - root.btnGap
                  height: parent.height
                  foreground: root.controlForeground
                  accent: root.nicknameFeedbackError ? root.urgent : root.controlAccent
                  font.pixelSize: Style.font.body
                  placeholderText: "Nickname · max 18"
                  maximumLength: 36
                  text: omaq.selfNickname
                  onTextEdited: {
                    omaq.clearRequestError(root.nicknameFeedbackRequest)
                    root.nicknameFeedback = ""
                    root.nicknameFeedbackRequest = ""
                    root.nicknameFeedbackError = false
                    var limited = omaq.limitNickname(text, 18)
                    if (limited !== text)
                      text = limited
                  }
                  onAccepted: if (nicknameButton.enabled) nicknameButton.clicked()

                  Controls.ToolTip {
                    id: nicknameFeedbackTooltip
                    visible: (omaq.selfNickname === "" || root.nicknameEditOpen) &&
                      root.nicknameFeedback !== ""
                    text: root.nicknameFeedback
                    delay: 0
                    timeout: -1
                    contentItem: SafeText {
                      text: nicknameFeedbackTooltip.text
                      color: root.nicknameFeedbackError ? root.urgent : root.foreground
                      font.family: root.fontFamily
                      font.pixelSize: Style.font.bodySmall
                    }
                  }
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
                  tooltipText: "Save nickname"
                  accessibleName: "Save nickname"
                  enabled: omaq.supportsIdentityActions && !root.nicknameSubmitPending &&
                    omaq.nicknameValid(nicknameField.text)
                  onClicked: {
                    if (root.nicknameSubmitPending || !nicknameButton.enabled)
                      return
                    omaq.clearRequestError(root.nicknameFeedbackRequest)
                    root.nicknameFeedback = ""
                    root.nicknameFeedbackRequest = ""
                    root.nicknameFeedbackError = false
                    root.nicknameRequestSequence++
                    root.nicknameRequest = Date.now().toString(36) + "-nickname-" +
                      root.nicknameRequestSequence.toString(36) + "-" +
                      Math.floor(Math.random() * 0x100000000).toString(36)
                    root.nicknameSubmitPending = omaq.setNickname(
                      nicknameField.text, root.nicknameRequest)
                    if (root.nicknameSubmitPending) {
                      nicknameSubmitTimer.restart()
                    } else {
                      root.nicknameFeedback = "OmaQ is not ready to update the nickname."
                      root.nicknameFeedbackError = true
                      root.nicknameRequest = ""
                    }
                  }
                }
              }

              RowLayout {
                id: selfStatusRow
                visible: root.nicknameFeedback === "" &&
                  omaq.selfNickname !== "" && !root.nicknameEditOpen
                width: parent.width
                height: Style.space(16)
                spacing: Style.space(3)

                SafeText {
                  Layout.fillWidth: true
                  Layout.alignment: Qt.AlignVCenter
                  text: "YOU · " + root.connectionLabel().toUpperCase()
                  color: omaq.connectionState === "online"
                    ? root.onlineStatusColor : root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                  font.bold: omaq.connectionState !== "online"
                  font.letterSpacing: 0.8
                  elide: Text.ElideRight
                }
              }

              SafeText {
                id: nicknameFeedbackText
                visible: root.nicknameFeedback !== "" &&
                  omaq.selfNickname !== "" && !root.nicknameEditOpen
                width: parent.width
                height: Style.space(16)
                verticalAlignment: Text.AlignVCenter
                text: root.nicknameFeedback
                color: root.nicknameFeedbackError ? root.urgent
                  : (root.systemColors[3] || root.onlineStatusColor)
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                elide: Text.ElideRight
              }
            }

            RowLayout {
              id: pendingRequestContent
              visible: omaq.pending
              width: parent.width
              height: parent.height
              spacing: Style.space(5)
              Accessible.name: omaq.pendingGroup
                ? "Group invitation. Join a private group."
                : "Friend request. Connect as a friend."

              ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0

                SafeText {
                  id: pendingRequestTitle
                  Layout.fillWidth: true
                  text: omaq.pendingGroup ? "Group invite" : "Friend request"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.body
                  font.bold: true
                  elide: Text.ElideRight
                }

                SafeText {
                  id: pendingRequestContext
                  Layout.fillWidth: true
                  text: omaq.pendingGroup ? "Private group" : "New contact"
                  color: root.dim
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.caption
                  elide: Text.ElideRight
                }
              }

              TokenButton {
                id: pendingAcceptButton
                Layout.minimumWidth: Style.space(28)
                Layout.preferredWidth: Style.space(28)
                Layout.maximumWidth: Style.space(28)
                Layout.minimumHeight: Style.space(28)
                Layout.preferredHeight: Style.space(28)
                Layout.maximumHeight: Style.space(28)
                iconText: "check"
                iconFontFamily: "Material Symbols Rounded"
                tooltipText: omaq.pendingGroup
                  ? "Accept group invitation" : "Accept friend request"
                accessibleName: tooltipText
                focusable: true
                foreground: root.foreground
                accent: root.systemColors[3] || root.controlAccent
                selected: true
                horizontalPadding: Style.space(2)
                verticalPadding: Style.space(2)
                onClicked: omaq.decide(true)
              }

              TokenButton {
                id: pendingDeclineButton
                Layout.minimumWidth: Style.space(28)
                Layout.preferredWidth: Style.space(28)
                Layout.maximumWidth: Style.space(28)
                Layout.minimumHeight: Style.space(28)
                Layout.preferredHeight: Style.space(28)
                Layout.maximumHeight: Style.space(28)
                iconText: "close"
                iconFontFamily: "Material Symbols Rounded"
                tooltipText: omaq.pendingGroup
                  ? "Decline group invitation" : "Decline friend request"
                accessibleName: tooltipText
                focusable: true
                foreground: root.foreground
                accent: root.systemColors[3] || root.controlAccent
                horizontalPadding: Style.space(2)
                verticalPadding: Style.space(2)
                onClicked: omaq.decide(false)
              }
            }
          }
        }

        Rectangle {
          id: supportLinks
          anchors.top: parent.top
          anchors.topMargin: root.pad
          anchors.right: parent.right
          anchors.rightMargin: root.pad
          width: root.railWidth
          height: root.headerHeight
          radius: root.themedRadius(height)
          color: "transparent"
          border.color: root.controlBorder
          border.width: 1
          z: 20

          Row {
            id: supportGlyphRow
            anchors.centerIn: parent
            spacing: 0

            TokenButton {
              id: githubSupportButton
              width: root.railIconWidth
              height: Style.space(30)
              borderless: true
              accent: root.systemColors[3] || root.controlAccent
              tooltipText: "Open OmaQ on GitHub"
              accessibleName: tooltipText
              focusable: true
              horizontalPadding: Style.space(2)
              verticalPadding: Style.space(2)
              onClicked: root.openRepo()

              Image {
                id: hancoreSupportIcon
                anchors.centerIn: parent
                width: root.supportGlyphSize
                height: root.supportGlyphSize
                source: Qt.resolvedUrl("assets/hancore-link.png")
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                layer.enabled: githubSupportButton.hot || githubSupportButton.activeFocus
                layer.effect: MultiEffect {
                  id: hancoreSupportHover
                  colorization: 1
                  colorizationColor: root.systemColors[3] || root.controlAccent
                }
              }
            }

            TokenButton {
              id: kofiSupportButton
              width: root.railIconWidth
              height: Style.space(30)
              borderless: true
              accent: root.systemColors[3] || root.controlAccent
              tooltipText: "Support HANCORE on Ko-fi"
              accessibleName: tooltipText
              focusable: true
              horizontalPadding: Style.space(2)
              verticalPadding: Style.space(2)
              onClicked: root.openKoFi()

              Image {
                id: kofiSupportIcon
                anchors.centerIn: parent
                width: root.supportGlyphSize
                height: root.supportGlyphSize
                source: Qt.resolvedUrl("assets/kofi-mono.svg")
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                layer.enabled: kofiSupportButton.hot || kofiSupportButton.activeFocus
                layer.effect: MultiEffect {
                  id: kofiSupportHover
                  colorization: 1
                  colorizationColor: root.systemColors[3] || root.controlAccent
                }
              }
            }
          }
        }

        Connections {
          target: omaq
          function onPendingChanged() {
            if (!omaq.pending)
              return
            root.nicknameEditOpen = false
            if (!root.nicknameSubmitPending)
              nicknameField.text = omaq.selfNickname
          }
          function onSelfNicknameChanged() {
            if (!root.nicknameEditOpen && !root.nicknameSubmitPending)
              nicknameField.text = omaq.selfNickname
          }
          function onNicknameTickChanged() {
            if (root.nicknameSubmitPending && root.nicknameRequest !== "" &&
                String(omaq.lastNicknameRequest || "") === root.nicknameRequest) {
              root.nicknameSubmitPending = false
              omaq.clearRequestError(root.nicknameFeedbackRequest)
              root.nicknameFeedback = ""
              root.nicknameFeedbackRequest = ""
              root.nicknameFeedbackError = false
              root.nicknameRequest = ""
              nicknameField.text = omaq.selfNickname
              nicknameSubmitTimer.stop()
              root.nicknameEditOpen = false
            }
          }
          function onLastErrorTickChanged() {
            if (!root.nicknameSubmitPending)
              return
            var correlated = root.nicknameRequest !== "" &&
              String(omaq.lastErrorRequest || "") === root.nicknameRequest
            var helperFailure = ["helper_down", "helper_incompatible",
              "identity_changed"].indexOf(omaq.lastError) !== -1
            if (correlated || helperFailure) {
              root.nicknameSubmitPending = false
              root.nicknameFeedbackRequest = correlated ? root.nicknameRequest : ""
              root.nicknameFeedback = omaq.lastError === "unsupported"
                ? "Nickname update is unavailable."
                : omaq.lastError === "nickname_invalid"
                  ? "Nickname must contain 1–18 valid characters."
                  : "Nickname update failed. Try again."
              root.nicknameFeedbackError = true
              root.nicknameRequest = ""
              nicknameSubmitTimer.stop()
            }
          }
        }

        Rectangle {
          id: actionRail
          anchors.top: parent.top
          anchors.topMargin: root.pad + heroVisual.height + root.panelSectionGap
          anchors.right: parent.right
          anchors.rightMargin: root.pad
          implicitWidth: railColumns.implicitWidth + root.framePadding * 2
          implicitHeight: railColumns.implicitHeight + root.framePadding * 2
          width: implicitWidth
          height: root.basePrimaryAreaHeight
          radius: root.themedRadius(height)
          color: "transparent"
          border.color: root.controlBorder
          border.width: 1
          z: 20

          Row {
            id: railColumns
            anchors.centerIn: parent
            spacing: 0

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
              materialIcon: "shield"
              label: "Safety code"
              selected: root.moreOpen && root.moreSection === "chat"
              onClicked: root.openRailAdvanced("chat")
            }
            RailIcon {
              materialIcon: "format_size"
              label: "Chat message size"
              selected: root.settingsOpen && root.fontSizeOpen
              onClicked: root.openRailFontSize()
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
                materialIcon: "warning_amber"
                label: "Danger zone"
                selected: root.moreOpen && root.moreSection === "danger"
                fillSelected: false
                onClicked: root.openRailAdvanced("danger")
              }
              RailIcon {
                materialIcon: "badge"
                label: "Identity"
                selected: root.moreOpen && root.moreSection === "identity"
                onClicked: root.openRailAdvanced("identity")
              }
            }
          }
        }

        Rectangle {
          id: activeMenuFrame
          visible: root.primaryMenuOpen
          anchors.top: parent.top
          anchors.topMargin: root.pad + heroVisual.height + root.panelSectionGap
          anchors.left: parent.left
          anchors.leftMargin: root.pad
          width: heroVisual.width
          height: panelScroll.height
          radius: root.themedRadius(height)
          color: "transparent"
          border.color: root.controlBorder
          border.width: 1
          z: 10
        }

        Flickable {
          id: panelScroll
          anchors.top: parent.top
          anchors.topMargin: root.pad + heroVisual.height + root.panelSectionGap
          anchors.left: parent.left
          anchors.leftMargin: root.pad
          anchors.right: parent.right
          anchors.rightMargin: root.pad
          height: Math.max(0, card.height - root.pad * 2 - heroVisual.height -
                           root.panelSectionGap)
          contentWidth: width
          contentHeight: column.y + column.implicitHeight +
            (root.primaryMenuOpen ? root.framePadding : 0)
          clip: true
          boundsBehavior: Flickable.StopAtBounds
          flickableDirection: Flickable.VerticalFlick

          Controls.ScrollBar.vertical: Controls.ScrollBar {
            id: panelScrollbar
            anchors.right: parent.right
            anchors.rightMargin: root.railWidth + root.panelSectionGap
            visible: panelScroll.contentHeight > panelScroll.height + 1
            policy: visible ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
            width: Style.space(3)
            opacity: panelScroll.moving || hovered ? 0.5 : 0.14
            Behavior on opacity { NumberAnimation { duration: 180 } }
            contentItem: Rectangle {
              implicitWidth: Style.space(2)
              radius: width / 2
              color: root.systemColors[3]
            }
          }

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
            x: root.primaryMenuOpen ? root.framePadding : 0
            y: root.primaryMenuOpen ? root.framePadding : 0
            width: Math.max(0, panelScroll.width - root.railWidth -
                            root.panelSectionGap -
                            (root.primaryMenuOpen ? root.framePadding * 2 : 0))
            spacing: root.panelSectionGap

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

            SafeText {
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

          Rectangle {
            id: identityContactsFrame
            visible: !root.primaryMenuOpen
            width: parent.width
            implicitHeight: identityContactsColumn.implicitHeight + root.framePadding * 2
            height: root.basePrimaryAreaHeight
            radius: root.themedRadius(height)
            color: "transparent"
            border.color: root.controlBorder
            border.width: 1

            Column {
              id: identityContactsColumn
              anchors.fill: parent
              anchors.margins: root.framePadding
              spacing: Style.space(6)

            Item {
              id: contactsFrame
              visible: (omaq.friends && omaq.friends.length > 0) ||
                (omaq.groups && omaq.groups.length > 0)
              width: parent.width
              implicitHeight: contactsColumn.implicitHeight
              height: implicitHeight

              Column {
                id: contactsColumn
                anchors.fill: parent
                spacing: Style.space(4)

            SafeText {
              visible: omaq.friends && omaq.friends.length > 0
              text: "FRIENDS · " + root.onlineFriendCount() + "/" +
                (omaq.friends ? omaq.friends.length : 0)
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
            }

            GridView {
              id: friendsGrid
              visible: omaq.friends && omaq.friends.length > 0
              width: parent.width
              height: visible ? Math.min(contentHeight, Style.space(24) * 5) : 0
              clip: true
              boundsBehavior: Flickable.StopAtBounds
              flow: GridView.FlowLeftToRight
              cellWidth: width / root.friendColumnCount
              cellHeight: Style.space(24)
              model: root.orderedFriendCells()

              delegate: Item {
                  id: friendDelegate
                  required property var modelData
                  required property int index
                  width: friendsGrid.cellWidth
                  height: friendsGrid.cellHeight
                  visible: !!modelData
                  readonly property int unreadCount: omaq.unreadFor(
                    modelData ? modelData.id : "")
                  activeFocusOnTab: true
                  HoverHandler { id: friendHover }
                  onActiveFocusChanged: if (activeFocus)
                    friendsGrid.positionViewAtIndex(index, GridView.Contain)
                  Accessible.role: Accessible.Button
                  Accessible.name: friendName.text + " · " + root.friendStatus(friendDelegate.modelData) +
                    (friendDelegate.unreadCount > 0
                      ? " · " + friendDelegate.unreadCount + " unread messages" : "")
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
                    color: root.friendStatusDotColor(friendDelegate.modelData)
                    border.width: 0
                  }

                  SafeText {
                    id: friendName
                    anchors.left: friendStatusDot.right
                    anchors.leftMargin: Style.space(6)
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    text: {
                      var friend = friendDelegate.modelData
                      return friend && friend.name
                        ? String(friend.name) : ("Friend " + (friend ? friend.id : ""))
                    }
                    color: friendHover.hovered || friendDelegate.activeFocus
                      ? (root.systemColors[3] || root.controlAccent)
                      : root.friendStatusColor(friendDelegate.modelData)
                    font.family: root.fontFamily
                    font.pixelSize: Style.font.body
                    font.bold: friendDelegate.modelData && friendDelegate.modelData.online
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                  }

                  Rectangle {
                    visible: friendDelegate.unreadCount > 0
                    x: friendName.x
                    y: friendName.y + (friendName.height + friendName.paintedHeight) / 2 +
                      Style.space(1)
                    width: Math.min(friendName.width, friendName.paintedWidth)
                    height: Math.max(1, Style.space(1))
                    color: root.systemColors[3]
                    radius: height / 2
                  }

                  MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openFriend(friendDelegate.modelData ? friendDelegate.modelData.id : "",
                      friendDelegate.modelData ? friendDelegate.modelData.name : "")
                  }
                }

              Controls.ScrollBar.vertical: Controls.ScrollBar {
                id: friendsScrollbar
                visible: friendsGrid.contentHeight > friendsGrid.height + 1
                policy: visible ? Controls.ScrollBar.AlwaysOn : Controls.ScrollBar.AlwaysOff
                width: Style.space(3)
                opacity: friendsGrid.moving || hovered ? 0.5 : 0.14
                Behavior on opacity { NumberAnimation { duration: 180 } }
                contentItem: Rectangle {
                  implicitWidth: Style.space(2)
                  radius: width / 2
                  color: root.dim
                }
                background: Item {}
              }
            }

            SafeText {
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

                  SafeText {
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

                  SafeText {
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

                  SafeText {
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
            }
          }
          }

          Column {
            visible: root.settingsOpen && root.fontSizeOpen
            width: parent.width
            spacing: Style.space(6)

            PanelSectionHeader {
              text: "CHAT MESSAGE SIZE"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            Row {
              width: parent.width
              spacing: Style.space(4)

              Repeater {
                model: [
                  { label: "90%", value: 0.9 },
                  { label: "100%", value: 1.0 },
                  { label: "110%", value: 1.1 },
                  { label: "120%", value: 1.2 },
                  { label: "140%", value: 1.4 }
                ]
                delegate: ActionButton {
                  required property var modelData
                  width: Math.max(0, (parent.width - parent.spacing * 4) / 5)
                  text: String(modelData.label)
                  selected: root.messageScale === Number(modelData.value)
                  tooltipText: "Chat messages: " + text
                  onClicked: root.setMessageScale(modelData.value)
                }
              }
            }

            Rectangle {
              width: parent.width
              implicitHeight: messageScalePreview.implicitHeight + Style.space(14)
              height: implicitHeight
              radius: root.themedRadius(height)
              color: Qt.rgba(root.controlAccent.r, root.controlAccent.g,
                root.controlAccent.b, 0.16)
              border.color: root.controlBorder
              border.width: 1

              SafeText {
                id: messageScalePreview
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Style.space(7)
                text: "This is how a chat message will look."
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Math.max(Style.font.caption,
                  Math.round(Style.font.body * root.messageScale))
                wrapMode: Text.WordWrap
              }
            }

            SafeText {
              width: parent.width
              text: "Changes message text and text typed in the composer."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }
          }

          Column {
            visible: root.settingsOpen && root.themeOpen
            width: parent.width
            spacing: Style.space(8)

            SafeText {
              width: parent.width
              text: "Applies to chat and Demo windows; the panel follows the Omarchy palette."
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }

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

                  SafeText {
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
              columns: 3
              columnSpacing: Style.space(4)
              rowSpacing: Style.space(4)
              readonly property real optionWidth: Math.max(0,
                (width - columnSpacing * (columns - 1)) / columns)

              Repeater {
                model: root.notificationSounds
                delegate: ActionButton {
                  required property var modelData
                  Layout.minimumWidth: soundGrid.optionWidth
                  Layout.preferredWidth: soundGrid.optionWidth
                  Layout.maximumWidth: soundGrid.optionWidth
                  Layout.minimumHeight: root.actionButtonHeight
                  Layout.preferredHeight: root.actionButtonHeight
                  Layout.maximumHeight: root.actionButtonHeight
                  iconText: ""
                  text: String(modelData.label)
                  fontSize: Style.font.bodySmall
                  horizontalPadding: Style.space(3)
                  selected: root.notificationSoundSelected(modelData)
                  tooltipText: modelData.custom
                    ? "Use imported sound " + String(modelData.label) : ""
                  onClicked: root.setNotificationSound(modelData)
                }
              }
            }

            SafeText {
              visible: !omaq.supportsCustomSounds
              width: parent.width
              text: "Update the local OmaQ helper to import or remove custom sounds."
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }

            ActionButton {
              width: parent.width
              text: soundPick.running ? "Choosing sound…" : "Import sound"
              iconText: "audio_file"
              iconFontFamily: "Material Symbols Rounded"
              enabled: omaq.supportsCustomSounds && !soundPick.running &&
                root.soundActionRequest === ""
              onClicked: root.startSoundPicker()
            }

            ActionButton {
              visible: !!root.selectedCustomSoundOption() && !root.soundRemoveConfirm
              width: parent.width
              text: "Remove selected sound"
              iconText: "delete"
              iconFontFamily: "Material Symbols Rounded"
              enabled: root.soundActionRequest === ""
              onClicked: root.requestSoundRemoval(root.selectedCustomSoundOption())
            }

            SafeText {
              visible: root.soundRemoveConfirm
              width: parent.width
              text: "Remove " + root.soundRemoveLabel +
                " from OmaQ? The original source file stays unchanged."
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            GridLayout {
              visible: root.soundRemoveConfirm
              width: parent.width
              columns: 2
              columnSpacing: root.btnGap

              ActionButton {
                Layout.fillWidth: true
                text: "Cancel"
                enabled: root.soundActionRequest === ""
                onClicked: {
                  root.soundRemoveConfirm = false
                  root.soundRemoveId = ""
                  root.soundRemoveLabel = ""
                  root.soundRemovePath = ""
                }
              }

              ActionButton {
                Layout.fillWidth: true
                text: "Remove"
                iconText: "delete"
                iconFontFamily: "Material Symbols Rounded"
                accent: root.urgent
                enabled: root.soundActionRequest === ""
                onClicked: root.confirmSoundRemoval()
              }
            }

            SafeText {
              visible: root.soundFeedback !== ""
              width: parent.width
              text: root.soundFeedback
              color: root.soundFeedbackError ? root.urgent : root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.caption
              wrapMode: Text.WordWrap
            }
          }

          Column {
            visible: omaq.identityPrimaryUncertain
            width: parent.width
            spacing: Style.space(6)

            PanelSectionHeader {
              text: "VERIFY IDENTITY STATE"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            SafeText {
              width: parent.width
              text: "OmaQ could not confirm whether the latest identity, contact, or group change reached disk before restarting. Messaging and mutations are paused. Review your nickname and contacts first; group recovery stays paused and is reconciled only after confirmation."
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            GridLayout {
              visible: !root.identityPrimaryConfirm
              width: parent.width
              columns: 2
              columnSpacing: root.btnGap

              ActionButton {
                Layout.fillWidth: true
                text: "Show contacts"
                iconText: "manage_accounts"
                iconFontFamily: "Material Symbols Rounded"
                onClicked: root.dismissTransientSections()
              }

              ActionButton {
                Layout.fillWidth: true
                text: "Mark verified"
                iconText: "verified"
                iconFontFamily: "Material Symbols Rounded"
                enabled: root.identityPrimaryRequest === ""
                onClicked: root.identityPrimaryConfirm = true
              }
            }

            SafeText {
              visible: root.identityPrimaryConfirm
              width: parent.width
              text: "Clear this warning? Confirm only after checking whether the intended identity and contact change is present."
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            GridLayout {
              visible: root.identityPrimaryConfirm
              width: parent.width
              columns: 2
              columnSpacing: root.btnGap

              ActionButton {
                Layout.fillWidth: true
                text: "Cancel"
                enabled: root.identityPrimaryRequest === ""
                onClicked: root.identityPrimaryConfirm = false
              }

              ActionButton {
                Layout.fillWidth: true
                text: "Clear warning"
                iconText: "verified"
                iconFontFamily: "Material Symbols Rounded"
                accent: root.urgent
                enabled: root.identityPrimaryRequest === ""
                onClicked: {
                  root.identityPrimaryRequestSequence++
                  var request = Date.now().toString(36) + "-primary-" +
                    root.identityPrimaryRequestSequence.toString(36) + "-" +
                    Math.floor(Math.random() * 0x100000000).toString(36)
                  if (omaq.acknowledgeIdentityPrimary(request))
                    root.identityPrimaryRequest = request
                  else
                    omaq.lastError = "helper_incompatible"
                }
              }
            }
          }

          Column {
            visible: omaq.directReinviteRequired
            width: parent.width
            spacing: Style.space(6)

            PanelSectionHeader {
              text: "DIRECT CHAT RECOVERY"
              foreground: root.foreground
              fontFamily: root.fontFamily
            }

            SafeText {
              width: parent.width
              text: "Your OmaQ identity and contact list are intact. Older direct-chat encryption state could not be safely assigned to current contacts and was archived. Remove affected contacts on both devices, exchange one fresh invite, and verify that the new chat works."
              color: root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            SafeText {
              visible: !omaq.supportsDirectRecovery
              width: parent.width
              text: "Update the local OmaQ helper before marking recovery complete. Existing contacts remain available."
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            GridLayout {
              visible: !root.directReinviteClearConfirm
              width: parent.width
              columns: 2
              columnSpacing: root.btnGap

              ActionButton {
                Layout.fillWidth: true
                text: "Review contacts"
                iconText: "manage_accounts"
                iconFontFamily: "Material Symbols Rounded"
                onClicked: {
                  root.dismissTransientSections()
                  root.moreOpen = true
                  root.moreSection = "danger"
                }
              }

              ActionButton {
                Layout.fillWidth: true
                text: "Mark complete"
                iconText: "task_alt"
                iconFontFamily: "Material Symbols Rounded"
                enabled: omaq.supportsDirectRecovery && root.directReinviteRequest === ""
                onClicked: root.directReinviteClearConfirm = true
              }
            }

            SafeText {
              visible: root.directReinviteClearConfirm
              width: parent.width
              text: "Clear this recovery warning? Continue only after affected contacts were removed on both devices, re-invited, and the new direct chat was verified."
              color: root.urgent
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }

            GridLayout {
              visible: root.directReinviteClearConfirm
              width: parent.width
              columns: 2
              columnSpacing: root.btnGap

              ActionButton {
                Layout.fillWidth: true
                text: "Cancel"
                enabled: root.directReinviteRequest === ""
                onClicked: root.directReinviteClearConfirm = false
              }

              ActionButton {
                Layout.fillWidth: true
                text: "Clear warning"
                iconText: "task_alt"
                iconFontFamily: "Material Symbols Rounded"
                accent: root.urgent
                enabled: root.directReinviteRequest === ""
                onClicked: {
                  root.directReinviteRequestSequence++
                  var request = Date.now().toString(36) + "-reinvite-" +
                    root.directReinviteRequestSequence.toString(36) + "-" +
                    Math.floor(Math.random() * 0x100000000).toString(36)
                  if (omaq.clearDirectReinvite(request))
                    root.directReinviteRequest = request
                  else
                    omaq.lastError = "helper_incompatible"
                }
              }
            }
          }

          SafeText {
            visible: root.settingsPersistenceWarning !== "" ||
              omaq.unreadWarning !== "" ||
              (chatSurface && chatSurface.autoOpenWarning !== "") ||
              (omaq.lastError !== "" && !(omaq.locked && omaq.lastError === "locked") &&
               !root.contextualErrorHandled)
            width: parent.width
            text: root.settingsPersistenceWarning !== ""
              ? root.settingsPersistenceWarning
              : (omaq.lastError !== "" && !(omaq.locked && omaq.lastError === "locked") &&
                 !root.contextualErrorHandled
                ? root.errorText(omaq.lastError)
                : (omaq.unreadWarning !== "" ? omaq.unreadWarning
                  : (chatSurface && chatSurface.autoOpenWarning !== ""
                    ? chatSurface.autoOpenWarning : "")))
            color: root.urgent
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          Column {
            visible: omaq.locked
            width: parent.width
            spacing: Style.space(8)

            SafeText {
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
              maximumLength: 128
              placeholderText: "Passphrase"
              onTextEdited: if (!root.identityActionPending) root.clearIdentityFeedback()
            }

            ActionButton {
              width: parent.width
              iconText: "lock_open"
              iconFontFamily: "Material Symbols Rounded"
              text: "Unlock"
              focusable: true
              enabled: !root.identityActionPending &&
                root.identityExistingPassphraseValid(unlockField.text)
              onClicked: root.runIdentityAction("unlock", "", unlockField.text)
            }

            SafeText {
              visible: root.identityFeedback !== ""
              width: parent.width
              text: root.identityFeedback
              color: root.identityFeedbackError ? root.urgent
                : (root.systemColors[3] || root.onlineStatusColor)
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }
          }

          Column {
            id: unlockedMenus
            visible: !omaq.locked && root.primaryMenuOpen
            width: parent.width
            height: {
              var total = 0
              var visibleCount = 0
              for (var childIndex = 0; childIndex < children.length; childIndex++) {
                var child = children[childIndex]
                if (!child.visible)
                  continue
                if (visibleCount > 0)
                  total += spacing
                total += child.height
                visibleCount++
              }
              return total
            }
            spacing: Style.space(12)

            Row {
              visible: omaq.incomingCall
              spacing: root.btnGap
              TokenButton {
                text: "Answer"
                bordered: true
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: {
                  if (omaq.answerCall(omaq.lastCallConv, omaq.lastCallKey))
                    OmaQ.CallTone.stopAll()
                }
              }
              TokenButton {
                text: "Decline call"
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                onClicked: {
                  if (omaq.stopCall(omaq.lastCallConv, omaq.lastCallKey))
                    OmaQ.CallTone.stopAll()
                }
              }
            }

            Column {
              id: inviteContent
              visible: root.inviteOpen && omaq.inviteUrl !== ""
              width: parent.width
              spacing: Style.space(8)

              PanelSectionHeader {
                text: "YOUR INVITE"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Item {
                id: inviteQr
                visible: true
                width: parent.width
                height: omaq.qrPath !== "" ? 148 : 0

                Image {
                  visible: omaq.qrPath !== ""
                  anchors.left: parent.left
                  width: 148
                  height: 148
                  fillMode: Image.PreserveAspectFit
                  source: omaq.qrPath !== "" ? root.localFileUrl(omaq.qrPath) : ""
                  asynchronous: true
                  smooth: false
                }
              }

              SafeText {
                width: parent.width
                text: root.shortInvite(omaq.inviteUrl)
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              SafeText {
                width: parent.width
                text: "Valid for " + root.inviteRemainingText()
                color: root.inviteRemainingSeconds <= 300 ? root.urgent : root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
              }

              GridLayout {
                visible: root.inviteConfirmMode === ""
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)

                ActionButton {
                  Layout.fillWidth: true
                  text: root.copied ? "Copied" : "Copy link"
                  enabled: !root.inviteActionPending && root.inviteRemainingSeconds > 0
                  onClicked: root.copyInvite()
                }
                ActionButton {
                  Layout.fillWidth: true
                  text: "New link"
                  enabled: !root.inviteActionPending
                  onClicked: root.inviteConfirmMode = "replace"
                }
                ActionButton {
                  Layout.columnSpan: 2
                  Layout.fillWidth: true
                  text: "Revoke"
                  accent: root.urgent
                  enabled: !root.inviteActionPending
                  onClicked: root.inviteConfirmMode = "revoke"
                }
              }

              SafeText {
                visible: root.inviteConfirmMode !== ""
                width: parent.width
                text: root.inviteConfirmMode === "replace"
                  ? "Revoke this invite and create a new link?"
                  : "Revoke this invite? It cannot be used again."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              GridLayout {
                visible: root.inviteConfirmMode !== ""
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                ActionButton {
                  Layout.fillWidth: true
                  text: "Cancel"
                  onClicked: root.inviteConfirmMode = ""
                }
                ActionButton {
                  Layout.fillWidth: true
                  text: root.inviteConfirmMode === "replace" ? "New link" : "Revoke"
                  accent: root.urgent
                  onClicked: root.startInviteRevoke(root.inviteConfirmMode === "replace")
                }
              }

              SafeText {
                visible: root.inviteFeedback !== ""
                width: parent.width
                text: root.inviteFeedback
                color: root.inviteFeedbackError ? root.urgent : root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }
            }

            Column {
              visible: root.inviteOpen && omaq.inviteUrl === ""
              width: parent.width
              spacing: Style.space(8)

              PanelSectionHeader {
                text: "INVITATION"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }
              SafeText {
                visible: root.inviteFeedback !== ""
                width: parent.width
                text: root.inviteFeedback
                color: root.inviteFeedbackError ? root.urgent : root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }
              ActionButton {
                width: parent.width
                text: "Create new invite"
                enabled: !root.inviteActionPending
                onClicked: root.startInviteCreate("create")
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
                enabled: root.redeemRequest === ""
                onTextChanged: {
                  root.redeemDraft = text
                  if (root.redeemRequest === "") {
                    root.redeemFeedback = ""
                    root.redeemFeedbackRequest = ""
                  }
                }
                onAccepted: if (joinBtn.enabled) joinBtn.clicked()
              }

              TokenButton {
                id: joinBtn
                width: parent.width
                text: "Join chat"
                bordered: true
                focusable: true
                foreground: root.foreground
                fontFamily: root.fontFamily
                enabled: root.redeemRequest === ""
                onClicked: {
                  if (Model.parseInvite(root.redeemDraft)) {
                    var request = omaq.redeem(root.redeemDraft)
                    if (request === "legacy") {
                      root.redeemFeedbackRequest = ""
                      root.redeemFeedback = "Invite submitted to the older helper. Check your contacts before trying it again."
                    } else if (request !== "") {
                      root.redeemRequest = request
                      root.redeemFeedbackRequest = request
                      root.redeemFeedback = "Checking invite…"
                    } else {
                      root.redeemFeedback = "OmaQ is not ready to check this invite."
                    }
                  } else {
                    root.redeemFeedback = root.errorText("unsupported")
                  }
                }
              }

              SafeText {
                visible: root.redeemFeedback !== ""
                width: parent.width
                text: root.redeemFeedback
                color: root.redeemFeedback === "Checking invite…"
                  ? root.dim : (root.redeemFeedback.indexOf("Waiting") >= 0
                    ? (root.systemColors[3] || root.onlineStatusColor) : root.urgent)
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
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
                  iconText: "shield"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Safety code"
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
                text: "SAFETY CODE"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              SafeText {
                id: safetyContactEmpty
                visible: root.moreSection === "chat" && (!omaq.friends || omaq.friends.length === 0)
                width: parent.width
                text: "Add a direct contact before comparing safety codes."
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }

              Flow {
                id: safetyContactChoices
                visible: root.moreSection === "chat" && omaq.friends && omaq.friends.length > 0
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: omaq.friends || []
                  delegate: TokenButton {
                    required property var modelData
                    width: Math.min(implicitWidth, safetyContactChoices.width)
                    text: String(modelData.name || ("Friend " + modelData.id))
                    selected: String(modelData.id || "") === String(omaq.selectedDirectId || "")
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: root.selectSafetyContact(modelData.id)
                  }
                }
              }

              ActionButton {
                id: safetyShowButton
                visible: root.moreSection === "chat" && omaq.selectedDirectId !== "" &&
                  (!root.safetyCodeVisible || root.currentSafetyCode === "")
                width: parent.width
                iconText: "󰌾"
                text: "Show safety code"
                onClicked: root.showSafetyCode()
              }

              SafeText {
                visible: root.moreSection === "chat" && omaq.selectedDirectId !== "" &&
                  (!root.safetyCodeVisible || root.currentSafetyCode === "")
                width: parent.width
                text: "Matching codes verify the same two transport identities."
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }

              Column {
                visible: root.moreSection === "chat" && root.safetyCodeVisible &&
                  root.currentSafetyCode !== ""
                width: parent.width
                spacing: Style.space(6)

                PanelSectionHeader {
                  text: "SAFETY CODE"
                  foreground: root.foreground
                  fontFamily: root.fontFamily
                }

                SafeText {
                  width: parent.width
                  text: root.currentSafetyCode
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
                  root.currentSafetyCode !== ""
                foreground: root.foreground
              }

              PanelSectionHeader {
                visible: root.moreSection === "groups"
                text: "GROUPS"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Column {
                visible: root.moreSection === "groups"
                width: parent.width
                spacing: root.btnGap

                TokenTextField {
                  id: groupNameField
                  width: parent.width
                  foreground: root.controlForeground
                  placeholderText: "Group name"
                  onAccepted: createGroupButton.clicked()
                }

                ActionButton {
                  id: createGroupButton
                  width: parent.width
                  height: root.actionButtonHeight
                  text: "Create"
                  enabled: omaq.groupTitleOk(groupNameField.text)
                  onClicked: {
                    if (omaq.createGroup(groupNameField.text.trim()))
                      groupNameField.text = ""
                  }
                }
              }

              SafeText {
                visible: root.moreSection === "groups" &&
                  (!omaq.groups || omaq.groups.length === 0)
                width: parent.width
                text: !omaq.groupsReady
                  ? (omaq.groupProjectionFailed
                    ? "Groups could not be loaded — close and reopen Groups to retry"
                    : "Loading groups…")
                  : "No groups yet"
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
                    enabled: root.groupInviteFeedback !== "Sending group invite…"
                    bordered: true
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: {
                      omaq.selectGroup(modelData.id)
                      root.groupInviteGroupId = ""
                      root.groupInviteFriendId = ""
                      root.groupInviteFriendKey = ""
                      root.groupInviteRequest = ""
                      root.groupInviteGeneration = -1
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
                text: "INVITE CONTACT TO GROUP"
                foreground: root.foreground
                fontFamily: root.fontFamily
              }

              Flow {
                visible: root.moreSection === "groups" && omaq.lastGroup !== "" &&
                  omaq.groupInviteCandidates(omaq.lastGroup).length > 0
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: omaq.groupInviteCandidates(omaq.lastGroup)
                  delegate: TokenButton {
                    required property var modelData
                    text: String(modelData.name || ("Friend " + modelData.id))
                    selected: String(modelData.id || "") === root.groupInviteFriendId
                    enabled: root.groupInviteFeedback !== "Sending group invite…"
                    focusable: true
                    foreground: root.foreground
                    fontFamily: root.fontFamily
                    onClicked: {
                      root.groupInviteFriendId = String(modelData.id || "")
                      root.groupInviteFriendKey = String(modelData.key || "")
                      root.groupInviteRequest = ""
                      root.groupInviteGeneration = -1
                      root.groupInviteFeedback = ""
                    }
                  }
                }
              }

              ActionButton {
                visible: root.moreSection === "groups" && omaq.lastGroup !== ""
                width: parent.width
                text: root.groupInviteFriendId
                  ? "Invite contact · " + root.friendName(root.groupInviteFriendId)
                  : "Select a contact"
                enabled: root.friendMatches(root.groupInviteFriendId,
                  root.groupInviteFriendKey) &&
                  root.groupInviteCandidateMatches(omaq.lastGroup,
                    root.groupInviteFriendId, root.groupInviteFriendKey) &&
                  root.groupInviteFeedback !== "Sending group invite…"
                onClicked: {
                  root.groupInviteGroupId = String(omaq.lastGroup || "")
                  root.groupInviteRequest = omaq.nextGroupInviteRequest()
                  root.groupInviteGeneration = Number(omaq.helperInstanceGeneration || 0)
                  root.groupInviteFeedback = "Sending group invite…"
                  if (!omaq.inviteToGroup(root.groupInviteFriendId,
                        root.groupInviteFriendKey, root.groupInviteGroupId,
                        root.groupInviteRequest))
                    root.groupInviteFeedback = "Group invite failed"
                }
              }

              SafeText {
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

              SafeText {
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

              SafeText {
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

              SafeText {
                visible: root.moreSection === "identity"
                width: parent.width
                text: omaq.saveProtected ? "Identity file protected" : "Passphrase not set"
                color: omaq.saveProtected ? root.onlineStatusColor
                  : (root.systemColors[1] || root.dim)
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                font.bold: true
              }

              SafeText {
                visible: root.moreSection === "identity" && !omaq.supportsIdentityActions
                width: parent.width
                text: "Update the local OmaQ helper before changing identity settings. Existing identity and contacts remain available."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WordWrap
              }

              TokenTextField {
                id: passField
                visible: root.moreSection === "identity"
                width: parent.width
                foreground: root.controlForeground
                password: true
                maximumLength: 128
                placeholderText: omaq.saveProtected
                  ? "Current or imported identity passphrase"
                  : "New or imported identity passphrase"
                enabled: omaq.supportsIdentityActions && !root.identityActionPending
                onTextEdited: if (!root.identityActionPending)
                  root.clearIdentityFeedback()
              }

              SafeText {
                visible: root.moreSection === "identity" && !omaq.saveProtected
                width: parent.width
                text: "Use at least 8 characters and at most 128 bytes."
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }

              SafeText {
                visible: root.moreSection === "identity"
                width: parent.width
                text: "Validate bundle checks a bundle without changing this identity. Import identity activates it after confirmation."
                color: root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.caption
                wrapMode: Text.WordWrap
              }

              TokenTextField {
                id: importPath
                visible: root.moreSection === "identity"
                width: parent.width
                foreground: root.controlForeground
                maximumLength: 511
                placeholderText: "Selected identity bundle"
                enabled: omaq.supportsIdentityActions && !root.identityActionPending
                onTextEdited: if (!root.identityActionPending)
                  root.clearIdentityFeedback()
                onTextChanged: {
                  if (root.replaceIdentityConfirm && text.trim() !== root.replaceIdentityPath) {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                  }
                }
              }

              GridLayout {
                id: identityActions
                visible: root.moreSection === "identity" && !root.replaceIdentityConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: root.btnGap
                readonly property real actionWidth: Math.max(0,
                  (width - columnSpacing) / columns)

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: omaq.saveProtected ? "lock_open" : "lock"
                  iconFontFamily: "Material Symbols Rounded"
                  text: omaq.saveProtected ? "Remove lock" : "Protect"
                  focusable: true
                  enabled: omaq.supportsIdentityActions && !root.identityActionPending &&
                    (omaq.saveProtected
                      ? root.identityExistingPassphraseValid(passField.text)
                      : root.identityNewPassphraseValid(passField.text))
                  onClicked: root.runIdentityAction(
                    omaq.saveProtected ? "unprotect" : "protect", "")
                }

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: "file_upload"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Export"
                  focusable: true
                  enabled: omaq.supportsIdentityActions && !root.identityActionPending
                  onClicked: root.startIdentityPicker("export")
                }

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: "file_open"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Validate bundle"
                  focusable: true
                  enabled: omaq.supportsIdentityActions && !root.identityActionPending
                  onClicked: {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                    var path = importPath.text.trim()
                    if (path !== "")
                      root.runIdentityAction("import", path)
                    else
                      root.startIdentityPicker("import")
                  }
                }

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: "published_with_changes"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Import identity"
                  focusable: true
                  accent: root.urgent
                  enabled: omaq.supportsIdentityActions && !root.identityActionPending
                  onClicked: {
                    var path = importPath.text.trim()
                    if (path === "") {
                      root.startIdentityPicker("replace")
                    } else {
                      root.replaceIdentityPath = path
                      root.replaceIdentityConfirm = true
                      root.identityFeedback = "Review the selected bundle before importing this identity."
                      root.identityFeedbackError = false
                    }
                  }
                }
              }

              SafeText {
                visible: root.moreSection === "identity" && root.identityFeedback !== ""
                width: parent.width
                text: root.identityFeedback
                color: root.identityFeedbackError ? root.urgent
                  : (root.systemColors[3] || root.onlineStatusColor)
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              SafeText {
                visible: root.moreSection === "identity" && root.replaceIdentityConfirm
                width: parent.width
                text: "Import " + root.replaceIdentityPath + " as the active identity? Existing direct contacts will require a fresh invite."
                color: root.urgent
                font.family: root.fontFamily
                font.pixelSize: Style.font.bodySmall
                wrapMode: Text.WrapAnywhere
              }

              GridLayout {
                id: identityReplaceActions
                visible: root.moreSection === "identity" && root.replaceIdentityConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: root.btnGap
                readonly property real actionWidth: Math.max(0,
                  (width - columnSpacing) / columns)

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityReplaceActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: "close"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Cancel"
                  focusable: true
                  enabled: !root.identityActionPending
                  onClicked: {
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                    root.identityFeedback = ""
                    passField.text = ""
                  }
                }

                ActionButton {
                  Layout.fillWidth: true
                  Layout.preferredWidth: identityReplaceActions.actionWidth
                  Layout.preferredHeight: root.actionButtonHeight
                  iconText: "published_with_changes"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Import identity now"
                  focusable: true
                  accent: root.urgent
                  enabled: omaq.supportsIdentityActions && !root.identityActionPending &&
                    root.replaceIdentityPath !== ""
                  onClicked: {
                    var path = root.replaceIdentityPath
                    root.replaceIdentityConfirm = false
                    root.replaceIdentityPath = ""
                    root.runIdentityAction("replace", path)
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
                  !root.nospamConfirm && !root.removeContactPickerOpen &&
                  !root.removeContactConfirm
                width: parent.width
                columns: 2
                columnSpacing: root.btnGap
                rowSpacing: Style.space(4)

                ActionButton {
                  visible: omaq.friends && omaq.friends.length > 0
                  Layout.fillWidth: true
                  iconText: "person_remove"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Remove"
                  tooltipText: "Remove contact"
                  onClicked: {
                    root.removeContactId = ""
                    root.removeContactKey = ""
                    root.removeContactPickerOpen = true
                  }
                }

                ActionButton {
                  Layout.fillWidth: true
                  iconText: "󰒭"
                  text: "Rotate ID"
                  tooltipText: "Rotate personal ID"
                  onClicked: root.nospamConfirm = true
                }
              }

              Column {
                visible: root.moreSection === "danger" && root.removeContactPickerOpen
                width: parent.width
                spacing: Style.space(4)

                SafeText {
                  width: parent.width
                  text: "Select the contact to remove"
                  color: root.foreground
                  font.family: root.fontFamily
                  font.pixelSize: Style.font.bodySmall
                  font.bold: true
                }

                Repeater {
                  model: omaq.friends || []
                  delegate: ActionButton {
                    required property var modelData
                    width: parent ? parent.width : 0
                    iconText: "person"
                    iconFontFamily: "Material Symbols Rounded"
                    text: String(modelData && modelData.name ||
                      ("Friend " + String(modelData && modelData.id || "")))
                    tooltipText: text
                    enabled: /^[0-9a-f]{64}$/.test(String(modelData && modelData.key || ""))
                    onClicked: {
                      root.removeContactId = String(modelData && modelData.id || "")
                      root.removeContactKey = String(modelData && modelData.key || "")
                      root.removeContactPickerOpen = false
                      root.removeContactConfirm = root.friendMatches(
                        root.removeContactId, root.removeContactKey)
                    }
                  }
                }

                ActionButton {
                  width: parent.width
                  text: "Cancel"
                  onClicked: {
                    root.removeContactPickerOpen = false
                    root.removeContactId = ""
                    root.removeContactKey = ""
                  }
                }
              }

              SafeText {
                visible: root.moreSection === "danger" && root.removeContactConfirm
                width: parent.width
                text: "Remove " + root.friendName(root.removeContactId) +
                  "? Chat history stays in local storage."
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
                  onClicked: {
                    root.removeContactConfirm = false
                    root.removeContactId = ""
                    root.removeContactKey = ""
                  }
                }
                ActionButton {
                  Layout.fillWidth: true
                  iconText: "person_remove"
                  iconFontFamily: "Material Symbols Rounded"
                  text: "Remove"
                  accent: root.urgent
                  enabled: root.friendMatches(root.removeContactId, root.removeContactKey)
                  onClicked: {
                    var selectedContact = root.removeContactId
                    var selectedKey = root.removeContactKey
                    if (root.friendMatches(selectedContact, selectedKey))
                      omaq.removeContact(selectedContact, selectedKey)
                    root.removeContactConfirm = false
                    root.removeContactId = ""
                    root.removeContactKey = ""
                    root.safetyCodeVisible = false
                  }
                }
              }

              SafeText {
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
