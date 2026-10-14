#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
qml=/usr/lib/qt6/bin/qml
[ -x "$qml" ] || {
  echo "identity-feedback: Qt 6 qml runtime is required" >&2
  exit 1
}
tmp=$(mktemp -d /tmp/omaq-identity-feedback-XXXXXX)
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT HUP INT TERM
python3 - "$root/Panel.qml" >"$tmp/fixture.qml" <<'PY'
import sys

panel = open(sys.argv[1], encoding="utf-8").read()
start = panel.index("  function identityFailureText(code) {\n")
end = panel.index("\n  }\n", start) + 4
error_start = panel.index("  function errorText(code) {\n")
error_end = panel.index("\n  }\n", error_start) + 4
print("""import QtQuick

QtObject {
  id: root
  property string identityAction: ""
  property QtObject state: QtObject {
    id: omaq
    property bool saveProtected: false
  }
""")
print(panel[error_start:error_end])
print(panel[start:end])
print("""  Component.onCompleted: {
    var cases = [
      ["protect", false, "Use at least 8 characters and at most 128 bytes."],
      ["protect", true, "Your identity already has a passphrase."],
      ["unprotect", true, "The current identity passphrase is incorrect."],
      ["export", false, "The identity could not be exported to that path."]
    ]
    for (var i = 0; i < cases.length; i++) {
      root.identityAction = cases[i][0]
      omaq.saveProtected = cases[i][1]
      if (root.identityFailureText("forbidden") !== cases[i][2]) {
        Qt.exit(10 + i)
        return
      }
    }
    if (root.errorText("runtime_marker_failed") !== "Your identity change is saved, but OmaQ could not update its runtime file. Free some disk space, then restart OmaQ; until then, quitting may leave it running.") {
      Qt.exit(20)
      return
    }
    Qt.exit(42)
  }
}""")
PY
out="$tmp/runtime.out"
set +e
timeout 5s env QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic "$qml" "$tmp/fixture.qml" >"$out" 2>&1
status=$?
set -e
if [ "$status" -ne 42 ] || grep -Eq 'ReferenceError|TypeError' "$out"; then
  cat "$out" >&2
  case "$status" in
  1[0-3]) echo "identity-feedback: forbidden case $((status - 10)) shows the wrong text" >&2 ;;
  20) echo "identity-feedback: the runtime marker failure shows the wrong text" >&2 ;;
  *) echo "identity-feedback: QML fixture failed with status $status" >&2 ;;
  esac
  exit 1
fi

echo "identity-feedback: ok"
