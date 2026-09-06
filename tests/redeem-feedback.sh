#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root/Panel.qml" <<'PY'
from pathlib import Path
import sys

panel = Path(sys.argv[1]).read_text(encoding="utf-8")
field_start = panel.index("                id: redeemField\n")
field_end = panel.index("\n              TokenButton {", field_start)
field = panel[field_start:field_end]
if field.count("onTextEdited: {") != 1 or "onTextChanged:" in field:
    raise SystemExit("redeem-feedback: invite input does not distinguish user edits")
for marker in (
    "root.redeemDraft = text",
    'if (root.redeemRequest === "") {',
    'root.redeemFeedback = ""',
    'root.redeemFeedbackRequest = ""',
    'root.redeemSafety = ""',
):
    if marker not in field:
        raise SystemExit(f"redeem-feedback: input handler lost {marker!r}")
result_start = panel.index("    function onRedeemTickChanged() {\n")
result_end = panel.index("    function onDirectReinviteTickChanged() {\n", result_start)
result = panel[result_start:result_end]
for marker in (
    'String(omaq.lastRedeemRequest || "") === root.redeemRequest',
    '"Invite checked. Waiting for the other person to accept."',
    'String(omaq.lastRedeemSafety || "")',
    'root.redeemDraft = ""',
):
    if marker not in result:
        raise SystemExit(f"redeem-feedback: correlated result lost {marker!r}")
safety_start = panel.index("                id: redeemedInviteSafety\n")
safety_end = panel.index("\n            TokenButton {", safety_start)
safety = panel[safety_start:safety_end]
for marker in (
    'visible: root.redeemSafety !== ""',
    'root.redeemSafety.replace(" / ", "\\n")',
    'text: "Compare this code with your friend before they accept"',
):
    if marker not in safety:
        raise SystemExit(f"redeem-feedback: safety result lost {marker!r}")
PY

qml=/usr/lib/qt6/bin/qml
[ -x "$qml" ] || {
  echo "redeem-feedback: Qt 6 qml runtime is required" >&2
  exit 1
}
tmp=$(mktemp -d /tmp/omaq-redeem-feedback-XXXXXX)
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT HUP INT TERM
cat >"$tmp/fixture.qml" <<'QML'
import QtQuick
import QtQuick.Controls

ApplicationWindow {
  id: root
  width: 1
  height: 1
  visible: true
  property string redeemDraft: "omaq://invite/test"
  property string redeemRequest: "request-1"
  property string redeemFeedback: "Checking invite…"
  property string redeemFeedbackRequest: "request-1"
  property string redeemSafety: ""
  property bool failed: false

  function check(value, message) {
    if (value)
      return
    failed = true
    console.error("REDEEM_FEEDBACK_FAIL " + message)
  }

  TextField {
    id: redeemField
    text: root.redeemDraft
    enabled: root.redeemRequest === ""
    onTextEdited: {
      root.redeemDraft = text
      if (root.redeemRequest === "") {
        root.redeemFeedback = ""
        root.redeemFeedbackRequest = ""
        root.redeemSafety = ""
      }
    }
  }

  Component.onCompleted: {
    root.redeemRequest = ""
    root.redeemFeedbackRequest = ""
    root.redeemFeedback = "Invite checked. Waiting for the other person to accept."
    root.redeemSafety = "aaaa / bbbb"
    root.redeemDraft = ""
    Qt.callLater(function() {
      root.check(root.redeemDraft === "", "programmatic draft clear failed")
      root.check(redeemField.text === "", "field did not follow the cleared draft")
      root.check(root.redeemFeedback.indexOf("Waiting") >= 0,
        "programmatic draft clear removed success feedback")
      root.check(root.redeemSafety === "aaaa / bbbb",
        "programmatic draft clear removed the safety code")
      redeemField.text = "omaq://invite/next"
      redeemField.textEdited()
      Qt.callLater(function() {
        root.check(root.redeemDraft === "omaq://invite/next",
          "user edit did not update the draft")
        root.check(root.redeemFeedback === "",
          "user edit did not clear stale feedback")
        root.check(root.redeemSafety === "",
          "user edit did not clear the stale safety code")
        Qt.exit(root.failed ? 1 : 42)
      })
    })
  }
}
QML
out="$tmp/runtime.out"
set +e
timeout 5s env QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic \
  QT_QUICK_CONTROLS_STYLE=Basic "$qml" "$tmp/fixture.qml" >"$out" 2>&1
status=$?
set -e
if [ "$status" -ne 42 ] ||
   grep -Eq 'REDEEM_FEEDBACK_FAIL|ReferenceError|TypeError|Binding loop' "$out"; then
  cat "$out" >&2
  echo "redeem-feedback: QML assertions failed" >&2
  exit 1
fi

echo "redeem-feedback: ok"
