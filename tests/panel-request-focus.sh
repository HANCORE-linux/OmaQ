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
    '? "Decline group invitation" : "Decline friend request"',
    "function onPendingChanged()",
    "root.nicknameEditOpen = false",
]
for marker in required:
    if marker not in panel:
        raise SystemExit(f"panel-request-focus: missing {marker!r}")

self_start = panel.index("id: selfHeaderContent")
pending_start = panel.index("id: pendingRequestContent")
support_start = panel.index("id: supportLinks", pending_start)
self_block = panel[self_start:pending_start]
pending_block = panel[pending_start:support_start]
if "id: panelCloseButton" in panel or 'tooltipText: "Close panel"' in panel:
    raise SystemExit("panel-request-focus: redundant panel Close action remains")
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
for path in Panel.qml Service.qml Model.js Emoji.js MessageLayout.js CallTone.qml \
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
  property alias testAcceptButton: pendingAcceptButton
  property alias testDeclineButton: pendingDeclineButton
  property alias testHeaderRow: heroHeaderRow
'''
if panel.count(needle) != 1:
    raise SystemExit("panel-request-focus: test alias insertion point changed")
panel_path.write_text(panel.replace(needle, needle + aliases), encoding="utf-8")

service_path = root / "Service.qml"
service = service_path.read_text(encoding="utf-8")
launch = "  Component.onCompleted: root.launchHelperDetached()\n"
if service.count(launch) != 1:
    raise SystemExit("panel-request-focus: helper launch seam changed")
service_path.write_text(service.replace(launch, "  Component.onCompleted: {}\n"),
                        encoding="utf-8")
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

  function check(value, message) {
    if (value)
      return
    failed = true
    console.error("PANEL_REQUEST_FAIL " + message)
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
      } else if (testRoot.step === 1) {
        testRoot.check(panel.testSelfAvatar.visible, "self avatar hidden without request")
        testRoot.check(panel.testSelfContent.visible, "self content hidden without request")
        testRoot.check(!panel.testRequestContent.visible, "request visible without request")
        testRoot.check(Math.abs(panel.testSelfAvatar.width + panel.testSelfContent.width +
                                panel.testHeaderRow.spacing - panel.testHeaderRow.width) < 1,
                       "self header geometry overflow")
        panel.testService.pendingGroup = false
        panel.testService.pending = true
      } else if (testRoot.step === 2) {
        testRoot.check(!panel.testSelfAvatar.visible, "self avatar visible for friend request")
        testRoot.check(!panel.testSelfContent.visible, "self content visible for friend request")
        testRoot.check(panel.testRequestContent.visible, "friend request content hidden")
        testRoot.check(panel.testRequestTitle.text === "Friend request", "friend title incorrect")
        testRoot.check(panel.testRequestContext.text === "New contact", "friend context incorrect")
        testRoot.check(!panel.testRequestTitle.truncated,
                       "friend title truncated " + panel.testRequestTitle.width + "/" +
                       panel.testRequestTitle.implicitWidth)
        testRoot.check(!panel.testRequestContext.truncated, "friend context truncated")
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
        panel.testService.pendingGroup = true
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
if grep -q 'PANEL_REQUEST_FAIL\|PANEL_REQUEST_RESULT fail' "$out" ||
   ! grep -q 'PANEL_REQUEST_RESULT ok' "$out"; then
  cat "$out" >&2
  echo "panel-request-focus: hidden QML assertions failed" >&2
  exit 1
fi
echo "panel-request-focus: runtime ok"
