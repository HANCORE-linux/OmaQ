#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
python3 - "$root" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
panel = (root / "Panel.qml").read_text(encoding="utf-8")
agents = (root / "AGENTS.md").read_text(encoding="utf-8")
guide = (root / "docs/USER-GUIDE.md").read_text(encoding="utf-8")
diagram = (root / "docs/images/guide/37-invite-conflict.svg").read_text(
    encoding="utf-8"
)

required = [
    "id: selfHeaderAvatar\n              visible: !omaq.pending",
    "id: selfHeaderContent\n              visible: !omaq.pending",
    "id: heroHeaderRow",
    "id: pendingRequestContent\n              visible: omaq.pending",
    "id: pendingRequestTitle",
    "id: pendingRequestContext",
    'text: omaq.pendingGroup ? "Group invite" : "Friend request"',
    'text: omaq.pendingGroup ? "Private group" : "New contact"',
    '? "Accept group invitation" : "Accept friend request"',
    '"Decline and revoke link"',
    '"Decline friend request"',
    "function onPendingChanged()",
    "root.nicknameEditOpen = false",
]
for marker in required:
    if marker not in panel:
        raise SystemExit(f"panel-request-focus: missing {marker!r}")
layout_markers = [
    "readonly property int cardWidth: 400",
    "function orderedFriendCells(columnCount)",
    "readonly property int columnCount: Math.max(1, Math.min(2,",
    "model: root.orderedFriendCells(columnCount)",
    "id: inviteSteps",
    'text: "Send the link or QR through a trusted channel"',
    'text: "They redeem the invite once"',
    'text: "Verify and accept the request"',
]
for marker in layout_markers:
    if marker not in panel:
        raise SystemExit(f"panel-request-focus: missing panel layout marker {marker!r}")
if 'text: "You accept · the chat opens"' in panel:
    raise SystemExit("panel-request-focus: invite instructions promise automatic chat opening")

self_start = panel.index("id: selfHeaderContent")
pending_start = panel.index("id: pendingRequestContent")
support_start = panel.index("id: supportLinks", pending_start)
self_block = panel[self_start:pending_start]
pending_block = panel[pending_start:support_start]
if "id: panelCloseButton" in panel or 'tooltipText: "Close panel"' in panel:
    raise SystemExit("panel-request-focus: redundant panel Close action remains")
for forbidden in ('placeholderText: "Search this chat"', 'text: "Search"',
                  'label: "Search and safety"', "omaq.searchChat(",
                  "omaq.searchItems", "root.searchMetaText("):
    if forbidden in panel:
        raise SystemExit(f"panel-request-focus: panel message search remains: {forbidden}")
if 'label: "Safety code"' not in panel or 'text: "Show safety code"' not in panel:
    raise SystemExit("panel-request-focus: safety-code path was removed with message search")
if '"FILL": railIcon.selected && railIcon.fillSelected ? 1 : 0' not in panel:
    raise SystemExit("panel-request-focus: rail icon fill binding changed")
for icon, label in (("shield", "Safety code"), ("badge", "Identity")):
    start = panel.index(f'materialIcon: "{icon}"')
    end = panel.index("onClicked:", start)
    if "fillSelected: false" not in panel[start:end]:
        raise SystemExit(
            f"panel-request-focus: {label} rail icon changes glyph fill when selected"
        )
if 'text: "YOU · " + root.connectionLabel().toUpperCase()' not in self_block:
    raise SystemExit("panel-request-focus: normal self status is missing")
for forbidden in ("omaq.selfAvatar", "omaq.selfNickname", '"YOU · "'):
    if forbidden in pending_block:
        raise SystemExit(f"panel-request-focus: request content still exposes self presentation: {forbidden}")
if pending_block.count("onClicked: omaq.decide(true)") != 1 or \
   pending_block.count("onClicked: omaq.decide(false)") != 1:
    raise SystemExit("panel-request-focus: request decisions are missing or duplicated")
if "accessibleName: tooltipText" not in pending_block or \
   pending_block.count("focusable: true") != 2:
    raise SystemExit("panel-request-focus: request controls are not keyboard accessible")
if "pending contact or group request replaces that entire self presentation" not in agents:
    raise SystemExit("panel-request-focus: repository UI contract is stale")
if "temporarily replaces the complete self presentation" not in guide:
    raise SystemExit("panel-request-focus: user guide is stale")
for marker in (
    "Another device used this invite link",
    "Decline and revoke link",
):
    if marker not in guide or marker not in diagram:
        raise SystemExit(f"panel-request-focus: conflict guide lost {marker!r}")
if "37-invite-conflict.svg" not in guide:
    raise SystemExit("panel-request-focus: conflict diagram link is missing")
print("panel-request-focus: source ok")
PY

wayland_socket=""
case "${WAYLAND_DISPLAY:-}" in
  /*) wayland_socket=$WAYLAND_DISPLAY ;;
  ?*) wayland_socket=${XDG_RUNTIME_DIR:-}/$WAYLAND_DISPLAY ;;
esac
if ! command -v quickshell >/dev/null 2>&1 ||
   ! command -v timeout >/dev/null 2>&1 ||
   [ ! -S "$wayland_socket" ] ||
   [ ! -d /usr/share/omarchy/shell/Ui ] ||
   [ ! -d /usr/share/omarchy/shell/Commons ]; then
  echo "panel-request-focus: runtime skipped (Quickshell/Wayland fixture unavailable)"
  exit 0
fi

tmp=$(mktemp -d /tmp/omaq-panel-request-XXXXXX)
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT HUP INT TERM
mkdir -p "$tmp/omaq"
for path in Panel.qml Service.qml SafeText.qml Model.js Emoji.js MessageLayout.js CallTone.qml \
  ChatSurface.qml PlacementController.qml SurfaceCoordinator.qml qmldir assets pages \
  sounds themes scripts helper manifest.json; do
  cp -a "$root/$path" "$tmp/omaq/"
done
ln -s /usr/share/omarchy/shell/Ui "$tmp/Ui"
ln -s /usr/share/omarchy/shell/Commons "$tmp/Commons"
cat >"$tmp/omaq/ChatSurface.qml" <<'QML'
import QtQuick

Item {
  property var service: null
  property var bar: null
  property var settings: null
  property string instanceName: "test"
  property bool autoOpenWarning: false
  property bool demoOpen: false
  property bool muted: false
  signal formatToolbarToggled(bool enabled)
  function openConversation() {}
  function openDemo() { demoOpen = true }
  function previewSound() {}
  function toggleMute() { muted = !muted }
}
QML
cat >"$tmp/import-probe.qml" <<'QML'
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import qs.Ui
import qs.Commons

ShellRoot {
  Timer {
    interval: 0
    running: true
    onTriggered: {
      console.log("PANEL_REQUEST_IMPORTS ok")
      Qt.quit()
    }
  }
}
QML
probe_out="$tmp/import-probe.out"
if ! timeout 8s env \
  QML_IMPORT_PATH="/usr/share/omarchy/shell${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
  quickshell -n -p "$tmp/import-probe.qml" >"$probe_out" 2>&1 ||
   ! grep -q 'PANEL_REQUEST_IMPORTS ok' "$probe_out"; then
  echo "panel-request-focus: runtime skipped (QML imports unavailable)"
  exit 0
fi
python3 - "$tmp/omaq" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
panel_path = root / "Panel.qml"
panel = panel_path.read_text(encoding="utf-8")
needle = '  moduleName: "hancore.omaq"\n'
aliases = '''  property alias testService: omaq
  property alias testSelfAvatar: selfHeaderAvatar
  property alias testSelfContent: selfHeaderContent
  property alias testRequestContent: pendingRequestContent
  property alias testRequestTitle: pendingRequestTitle
  property alias testRequestContext: pendingRequestContext
  property alias testRequestDetails: pendingRequestDetails
  property alias testRequestSafety: pendingRequestSafety
  property alias testRequestGuidance: pendingRequestGuidance
  property alias testRequestConflict: pendingRequestConflict
  property alias testHero: heroVisual
  property alias testAcceptButton: pendingAcceptButton
  property alias testDeclineButton: pendingDeclineButton
  property alias testHeaderRow: heroHeaderRow
  property alias testSafetyEmpty: safetyContactEmpty
  property alias testSafetyChoices: safetyContactChoices
  property alias testSafetyShowButton: safetyShowButton
  property alias testCard: card
  property alias testFriendsGrid: friendsGrid
  property alias testInviteContent: inviteContent
  property alias testInviteSteps: inviteSteps
  property alias testInviteStepsRepeater: inviteStepsRepeater
  property alias testNicknameField: nicknameField
'''
if panel.count(needle) != 1:
    raise SystemExit("panel-request-focus: test alias insertion point changed")
panel_path.write_text(panel.replace(needle, needle + aliases), encoding="utf-8")

service_path = root / "Service.qml"
service = service_path.read_text(encoding="utf-8")
launch = "  Component.onCompleted: root.launchHelperDetached()\n"
if service.count(launch) != 1:
    raise SystemExit("panel-request-focus: helper launch seam changed")
service = service.replace(launch, "  Component.onCompleted: {}\n")
immediate = '''    if (root.procReady) {
      proc.write(line)
      return true
    }
    return false
  }

  function sendConversationOp'''
test_immediate = '''    if (root.procReady) {
      var captured = root.pendingOps.slice()
      captured.push(line)
      root.pendingOps = captured
      return true
    }
    return false
  }

  function sendConversationOp'''
if service.count(immediate) != 1:
    raise SystemExit("panel-request-focus: immediate-send test seam changed")
service_path.write_text(service.replace(immediate, test_immediate), encoding="utf-8")
PY
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import qs.Ui
import qs.Commons
import "omaq" as OmaQ

ShellRoot {
  id: testRoot
  property bool failed: false
  property int step: 0
  property real compactHeight: 0

  function check(value, message) {
    if (value)
      return
    failed = true
    console.error("PANEL_REQUEST_FAIL " + message)
  }

  function inviteStepsFit() {
    if (panel.testInviteStepsRepeater.count !== 3)
      return false
    for (var stepIndex = 0; stepIndex < 3; stepIndex++) {
      var stepRow = panel.testInviteStepsRepeater.itemAt(stepIndex)
      if (!stepRow || stepRow.x < -0.5 ||
          stepRow.x + stepRow.width > panel.testInviteSteps.width + 0.5) {
        console.error("PANEL_REQUEST_STEP_ROW", stepIndex, stepRow,
          stepRow ? stepRow.x : -1, stepRow ? stepRow.width : -1,
          panel.testInviteSteps.width)
        return false
      }
      for (var childIndex = 0; childIndex < stepRow.children.length; childIndex++) {
        var child = stepRow.children[childIndex]
        if (child.visible && (child.x < -0.5 || child.y < -0.5 ||
            child.x + child.width > stepRow.width + 0.5 ||
            child.y + child.height > stepRow.height + 0.5)) {
          console.error("PANEL_REQUEST_STEP_CHILD", stepIndex, childIndex,
            child.x, child.y, child.width, child.height, stepRow.width,
            stepRow.height)
          return false
        }
      }
    }
    return true
  }

  OmaQ.Panel {
    id: panel
    opened: false
  }

  Timer {
    interval: 120
    repeat: true
    running: true
    onTriggered: {
      if (testRoot.step === 0) {
        panel.testService.selfNickname = "HANCORE"
        panel.testService.connectionState = "online"
        panel.testService.pending = false
        var semanticPalette = ["#010203", "#111213", "#212223", "#313233",
                               "#414243", "#515253", "#616263", "#717273"]
        panel.parseSystemColors([
          'background = "#010203"',
          'red = "#111213"',
          'green = "#212223"',
          'yellow = "#313233"',
          'blue = "#414243"',
          'magenta = "#515253"',
          'cyan = "#616263"',
          'foreground = "#717273"'
        ].join("\n"))
        for (var colorIndex = 0; colorIndex < semanticPalette.length; colorIndex++)
          testRoot.check(panel.systemColors[colorIndex] === semanticPalette[colorIndex],
            "semantic system color " + colorIndex + " was not parsed")
        panel.parseSystemColors('yellow = "#313233"\ncolor3 = "#a1a2a3"')
        testRoot.check(panel.systemColors[3] === "#a1a2a3",
          "legacy ANSI system color did not override its semantic equivalent")
      } else if (testRoot.step === 1) {
        testRoot.check(panel.testSelfAvatar.visible, "self avatar hidden without request")
        testRoot.check(panel.testSelfContent.visible, "self content hidden without request")
        testRoot.check(!panel.testRequestContent.visible, "request visible without request")
        testRoot.check(Math.abs(panel.testSelfAvatar.width + panel.testSelfContent.width +
                                panel.testHeaderRow.spacing - panel.testHeaderRow.width) < 1,
                       "self header geometry overflow")
        panel.testService.activeHelperProtocol = 15
        panel.testService.pendingGroup = false
        panel.testService.pending = true
        testRoot.check(panel.testRequestContent.Accessible.name ===
          "Friend request. Connect as a friend.",
          "Protocol-15 accessibility text requires an unavailable safety code")
        var safetyPartsA = []
        var safetyPartsB = []
        for (var safetyIndex = 0; safetyIndex < 16; safetyIndex++) {
          safetyPartsA.push("aaaa")
          safetyPartsB.push("bbbb")
        }
        panel.testService.activeHelperProtocol = 16
        panel.testService.pendingRequestKey =
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        panel.testService.pendingRequestSafety =
          safetyPartsA.join(" ") + " / " + safetyPartsB.join(" ")
        panel.testService.pendingRequestConflictKey = ""
        panel.testService.pendingGroup = false
        panel.testService.pending = true
      } else if (testRoot.step === 2) {
        testRoot.check(!panel.testSelfAvatar.visible, "self avatar visible for friend request")
        testRoot.check(!panel.testSelfContent.visible, "self content visible for friend request")
        testRoot.check(panel.testRequestContent.visible, "friend request content hidden")
        testRoot.check(panel.testRequestTitle.text === "Friend request", "friend title incorrect")
        testRoot.check(panel.testRequestContent.Accessible.name ===
          "Friend request. Compare the safety code with your friend before accepting.",
          "Protocol-16 accessibility text omits safety comparison")
        testRoot.check(!panel.testRequestContext.visible,
          "legacy friend context visible with Protocol-16 safety")
        testRoot.check(panel.testRequestSafety.visible &&
          panel.testRequestSafety.text.indexOf("\n") > 0,
          "two-group request safety code is not visible")
        testRoot.check(panel.testRequestGuidance.visible &&
          panel.testRequestGuidance.text ===
            "Compare this code with your friend before accepting",
          "request safety guidance is missing")
        testRoot.check(panel.testHero.height > Style.space(48),
          "Protocol-16 request card did not expand for the safety code")
        testRoot.check(!panel.testRequestTitle.truncated,
                       "friend title truncated " + panel.testRequestTitle.width + "/" +
                       panel.testRequestTitle.implicitWidth)
        testRoot.check(panel.testAcceptButton.visible && panel.testAcceptButton.focusable,
                       "accept action unavailable")
        testRoot.check(panel.testDeclineButton.visible && panel.testDeclineButton.focusable,
                       "decline action unavailable")
        testRoot.check(panel.testAcceptButton.width === Style.space(28) &&
                       panel.testAcceptButton.height === Style.space(28) &&
                       panel.testDeclineButton.width === Style.space(28) &&
                       panel.testDeclineButton.height === Style.space(28),
                       "request action sizing changed")
        testRoot.check(Math.abs(panel.testRequestContent.width -
                                panel.testHeaderRow.width) < 1,
                       "request header geometry overflow")
        panel.testService.pendingRequestConflictKey =
          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        testRoot.check(panel.testRequestConflict.visible &&
          panel.testRequestConflict.text === "Another device used this invite link",
          "request conflict banner is missing")
        testRoot.check(panel.testRequestDetails.implicitHeight <=
          panel.testHero.height - Style.space(12),
          "request fingerprint content overflows the pending card")
        panel.testService.pendingOps = []
        panel.testService.awaitingHelperInstance = false
        panel.testService.procReady = false
        panel.testDeclineButton.clicked()
        testRoot.check(panel.testService.pending &&
          panel.testService.pendingOps.length === 0,
          "offline Protocol-16 decline was queued or cleared locally")
        panel.testService.procReady = true
        panel.testDeclineButton.clicked()
        var decline = panel.testService.pendingOps.length === 1
          ? JSON.parse(panel.testService.pendingOps[0]) : ({})
        testRoot.check(decline.op === "contact.decide" && decline.accept === false &&
          decline.key === "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "decline did not send the key-bound revoking contact decision")
        testRoot.check(panel.testService.pending &&
          panel.testService.pendingRequestSafety !== "" &&
          panel.testService.pendingRequestConflictKey !== "",
          "decline cleared before helper confirmation")
        panel.testService.handleLine(JSON.stringify({ event: "error",
          code: "identity_changed", request: "x" }))
        testRoot.check(panel.testService.pending,
          "rejected stale decline hid the helper-owned request")
        panel.testService.pendingOps = []
        panel.testDeclineButton.clicked()
        testRoot.check(panel.testService.pendingOps.length === 1 &&
          panel.testService.pending,
          "retried decline did not wait for helper confirmation")
        panel.testService.handleLine(JSON.stringify({ event: "invite", url: "",
          expires: 0, op: "clear" }))
        testRoot.check(!panel.testService.pending &&
          panel.testService.pendingRequestSafety === "" &&
          panel.testService.pendingRequestConflictKey === "",
          "authoritative invite clear did not clear request presentation state")
        panel.testService.pendingOps = []
        panel.testService.procReady = false
        panel.testService.awaitingHelperInstance = false
        panel.testService.pendingGroup = true
        panel.testService.pending = true
      } else if (testRoot.step === 3) {
        testRoot.check(panel.testRequestTitle.text === "Group invite", "group title incorrect")
        testRoot.check(panel.testRequestContext.text === "Private group", "group context incorrect")
        testRoot.check(!panel.testRequestTitle.truncated,
                       "group title truncated " + panel.testRequestTitle.width + "/" +
                       panel.testRequestTitle.implicitWidth)
        testRoot.check(!panel.testRequestContext.truncated, "group context truncated")
        panel.testService.pending = false
      } else if (testRoot.step === 4) {
        testRoot.check(panel.testSelfAvatar.visible, "self avatar did not return")
        testRoot.check(panel.testSelfContent.visible, "self content did not return")
        testRoot.check(!panel.testRequestContent.visible, "request content did not clear")
        panel.testService.friends = []
        panel.openRailAdvanced("chat")
      } else if (testRoot.step === 5) {
        testRoot.check(panel.testSafetyEmpty.visible,
          "safety menu has no empty-contact guidance")
        panel.testService.friends = [{ id: "7",
          name: "Alice with a deliberately overlong remote legacy contact name that must stay inside the panel frame",
          key: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          online: false }]
      } else if (testRoot.step === 6) {
        var choicesFit = panel.testSafetyChoices.visible &&
          panel.testSafetyChoices.width > 0 && panel.testSafetyChoices.children.length > 0
        for (var childIndex = 0;
             choicesFit && childIndex < panel.testSafetyChoices.children.length;
             childIndex++)
          choicesFit = panel.testSafetyChoices.children[childIndex].width <=
            panel.testSafetyChoices.width + 0.5
        testRoot.check(choicesFit, "long safety contact overflowed the panel frame")
        testRoot.check(panel.selectSafetyContact("7"),
          "safety contact could not be selected")
        testRoot.check(panel.testService.selectedDirectId === "7" &&
          panel.testService.selectedDirectKey ===
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "safety contact binding is incomplete")
        testRoot.check(!panel.testSafetyEmpty.visible &&
          panel.testSafetyShowButton.visible,
          "bound safety action did not replace the empty state")
        panel.showSafetyCode()
        testRoot.check(panel.testService.safetyConv === "7" &&
          panel.testService.safetyRequest !== "",
          "safety request is not bound to the selected contact")
        panel.moreOpen = false
        var friends = []
        for (var friendIndex = 0; friendIndex < 14; friendIndex++)
          friends.push({ id: String(friendIndex + 1),
            name: "Wilhelmine-Konstanze " + String(friendIndex + 1),
            key: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            online: false })
        panel.testService.friends = friends
      } else if (testRoot.step === 7) {
        testRoot.compactHeight = panel.testCard.height
        testRoot.check(panel.testCard.width === 400,
          "panel card width is not the literal 400 pixels")
        testRoot.check(panel.testFriendsGrid.visible &&
          panel.testFriendsGrid.columnCount === 1 &&
          panel.testFriendsGrid.count === 14 &&
          Math.abs(panel.testFriendsGrid.cellWidth -
            panel.testFriendsGrid.width) < 0.5,
          "contact grid is not width-driven or one-column at font base 12")
        var firstFriend = panel.testFriendsGrid.itemAtIndex(0)
        testRoot.check(firstFriend && firstFriend.modelData.id === "1",
          "one-column friend order changed")
        testRoot.check(panel.testFriendsGrid.contentHeight >
          panel.testFriendsGrid.height + 1,
          "one-column friend list does not expose scrolling after five rows")
        panel.testFriendsGrid.positionViewAtIndex(13, GridView.End)
      } else if (testRoot.step === 8) {
        var lastFriend = panel.testFriendsGrid.itemAtIndex(13)
        testRoot.check(lastFriend && lastFriend.modelData.id === "14" &&
          panel.testFriendsGrid.contentY > 0,
          "last friend cannot be reached through the scrollable grid")
        Style.spacingScale = 0.75
      } else if (testRoot.step === 9) {
        testRoot.check(panel.testFriendsGrid.columnCount === 2 &&
          panel.testFriendsGrid.count === 18 &&
          Math.abs(panel.testFriendsGrid.cellWidth * 2 -
            panel.testFriendsGrid.width) < 0.5,
          "width-driven two-column contact path did not activate")
        var ordered = panel.orderedFriendCells(2)
        testRoot.check(ordered.length === 18 && ordered[0].id === "1" &&
          ordered[1].id === "6" && ordered[2].id === "2" &&
          ordered[9].id === "10" && ordered[10].id === "11" &&
          ordered[11] === null && ordered[16].id === "14" &&
          ordered[17] === null,
          "two-column friend order or page padding changed")
        Style.spacingScale = 1
        panel.testService.inviteUrl = "omaq://invite/mock-token"
        panel.testService.qrPath = "/tmp/omaq-invite-layout-missing.png"
        panel.testService.inviteExpiresAt = Math.floor(Date.now() / 1000) + 86400
        panel.inviteOpen = true
      } else if (testRoot.step === 10) {
        testRoot.check(panel.testInviteContent.visible &&
          panel.testInviteSteps.visible && testRoot.inviteStepsFit(),
          "invite instructions are hidden, incomplete, or overflowing")
        testRoot.check(panel.testCard.width === 400 &&
          panel.testCard.height > testRoot.compactHeight,
          "invite content did not extend the fixed-width panel")
        Style.fontBaseSize = 16
      } else if (testRoot.step === 11) {
        testRoot.check(panel.testCard.width === 400 &&
          panel.testInviteContent.width > 0 &&
          panel.testInviteContent.width < panel.testCard.width &&
          testRoot.inviteStepsFit(),
          "font base 16 escaped the fixed panel or invite rows")
        panel.inviteOpen = false
        panel.nicknameEditOpen = true
        panel.nicknameSubmitPending = true
        panel.nicknameRequest = "nickname-partial"
        panel.testNicknameField.text = "Partial name"
        panel.testService.handleLine(JSON.stringify({ event: "nickname",
          value: "Partial name" }))
      } else if (testRoot.step === 12) {
        testRoot.check(panel.testService.selfNickname === "Partial name" &&
          panel.nicknameSubmitPending && panel.nicknameEditOpen,
          "uncorrelated saved profile falsely completed a partial nickname request")
        panel.testService.handleLine(JSON.stringify({ event: "error",
          code: "nickname_group_sync_failed", request: "unrelated-name" }))
        testRoot.check(panel.nicknameSubmitPending,
          "unrelated nickname error completed the pending request")
        panel.testService.handleLine(JSON.stringify({ event: "error",
          code: "nickname_group_sync_failed", request: "nickname-partial" }))
      } else if (testRoot.step === 13) {
        testRoot.check(!panel.nicknameSubmitPending && panel.nicknameEditOpen &&
          panel.nicknameFeedbackError &&
          panel.nicknameFeedback === "Nickname saved; group updates are unconfirmed." &&
          panel.nicknameFeedbackRequest === "nickname-partial" &&
          panel.testNicknameField.text === "Partial name",
          "partial nickname result lost its warning, draft, or correlation")
        panel.nicknameSubmitPending = true
        panel.nicknameRequest = "nickname-retry"
        panel.testService.handleLine(JSON.stringify({ event: "nickname",
          value: "Partial name", request: "nickname-retry" }))
      } else if (testRoot.step === 14) {
        testRoot.check(!panel.nicknameSubmitPending && !panel.nicknameEditOpen &&
          panel.nicknameFeedback === "" && !panel.nicknameFeedbackError &&
          panel.nicknameRequest === "" && panel.testNicknameField.text === "Partial name",
          "confirmed same-name retry failed to clear the partial result")
        console.log(testRoot.failed ? "PANEL_REQUEST_RESULT fail" : "PANEL_REQUEST_RESULT ok")
        Qt.quit()
      }
      testRoot.step++
    }
  }
}
QML
out="$tmp/runtime.out"
if ! timeout 12s env \
  OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" \
  QML_IMPORT_PATH="/usr/share/omarchy/shell${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}" \
  quickshell -n -p "$tmp/shell.qml" >"$out" 2>&1; then
  cat "$out" >&2
  echo "panel-request-focus: hidden QML runtime failed" >&2
  exit 1
fi
if grep -Eq 'PANEL_REQUEST_FAIL|PANEL_REQUEST_RESULT fail|ReferenceError|TypeError|Binding loop|Cannot anchor|Unable to assign' "$out" ||
   ! grep -q 'PANEL_REQUEST_RESULT ok' "$out"; then
  cat "$out" >&2
  echo "panel-request-focus: hidden QML assertions failed" >&2
  exit 1
fi
echo "panel-request-focus: runtime ok"
