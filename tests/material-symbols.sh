#!/bin/sh
# Private offscreen engine: cached missing family, explicit load, loss, recovery.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
command -v quickshell >/dev/null
command -v fc-match >/dev/null
python3 "$root/tests/material-symbols.py" --font
umask 077
tmp=$(mktemp -d /tmp/omaq-symbols-XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/home" "$tmp/runtime" "$tmp/cache" "$tmp/config" "$tmp/data" "$tmp/fonts" "$tmp/OmaQ"
base_font=$(fc-match -f '%{file}' sans-serif)
[ -f "$base_font" ]
ln -s "$base_font" "$tmp/fonts/base.ttf"
cp "$root/MaterialSymbols.qml" "$root/SafeText.qml" "$tmp/OmaQ/"
python3 - "$tmp" <<'PY'
from pathlib import Path
from html import escape
import sys
root = Path(sys.argv[1])
(root / 'fonts.conf').write_text('<fontconfig><dir>' + escape(str(root / 'fonts')) +
    '</dir><cachedir>' + escape(str(root / 'cache')) + '</cachedir></fontconfig>')
path = root / 'OmaQ/MaterialSymbols.qml'
source = path.read_text()
needle = '  id: symbols\n'
assert source.count(needle) == 1
path.write_text(source.replace(needle, needle + '  property alias testSource: symbolFont.source\n'))
PY
cat >"$tmp/OmaQ/qmldir" <<'EOF'
module OmaQ
singleton MaterialSymbols 1.0 MaterialSymbols.qml
SafeText 1.0 SafeText.qml
EOF
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import QtQuick.Window
import Quickshell
import "OmaQ" as OmaQ

ShellRoot {
  id: test
  property int phase: 0
  property int attempts: 0
  property string originalSource: ""
  function check(ok, message) {
    if (!ok) {
      console.error("material-symbols: FAIL: " + message)
      Qt.exit(1)
    }
  }
  function verify(ready) {
    check(OmaQ.MaterialSymbols.ready === ready, "readiness did not track the loader")
    check(OmaQ.MaterialSymbols.family === (ready ? "Material Symbols Rounded" : "sans-serif"), "stale family")
    check(OmaQ.MaterialSymbols.glyph("") === "", "empty icon acquired content")
    for (var key of ["unknown_icon", "__proto__", "constructor", "toString", "<b>person</b>"])
      check(OmaQ.MaterialSymbols.glyph(key) === "?", "unknown name escaped its bounded fallback")
    for (var i = 0; i < icons.item.cells.count; ++i) {
      var cell = icons.item.cells.itemAt(i)
      check(cell.glyph.text === (ready ? OmaQ.MaterialSymbols.glyphs[cell.modelData] : "?"), "stale displayed glyph")
      check(cell.glyph.text.length === 1 && cell.glyph.implicitWidth > 0 &&
            cell.glyph.implicitWidth <= 25, "icon overflow or empty rendering: " + cell.modelData +
            " width=" + cell.glyph.implicitWidth + " font=" + cell.glyph.font.family + " ready=" + ready)
      check(cell.glyph.textFormat === Text.PlainText, "icon lost PlainText")
    }
  }
  Window {
    id: window
    visible: true
    width: 720
    height: 590
    color: "#111111"
    OmaQ.SafeText {
      id: before
      text: "person"
      font.family: "Material Symbols Rounded"
      font.pixelSize: 24
      color: "white"
    }
    Loader {
      id: icons
      anchors.fill: parent
      active: false
      sourceComponent: Item {
        property alias cells: cells
        Grid {
          anchors.centerIn: parent
          columns: 6
          spacing: 2
          Repeater {
            id: cells
            model: Object.keys(OmaQ.MaterialSymbols.glyphs)
            Item {
              required property string modelData
              property alias glyph: glyph
              width: 116
              height: 46
              OmaQ.SafeText {
                id: glyph
                anchors.horizontalCenter: parent.horizontalCenter
                text: OmaQ.MaterialSymbols.glyph(parent.modelData)
                color: "#7dce6a"
                font.family: OmaQ.MaterialSymbols.family
                font.pixelSize: 24
                font.variableAxes: ({ "FILL": 0, "wght": 500 })
                font.features: ({ "liga": 0, "clig": 0, "rlig": 0, "calt": 0, "rclt": 0 })
                renderType: Text.QtRendering
              }
              OmaQ.SafeText {
                anchors.top: glyph.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                text: parent.modelData
                font.family: "sans-serif"
                font.pixelSize: 10
                color: "white"
              }
            }
          }
        }
      }
    }
  }
  Timer {
    interval: 100
    running: true
    repeat: true
    onTriggered: {
      if (++test.attempts > 80) { test.check(false, "timed out"); return }
      if (test.phase === 0) {
        test.check(Qt.fontFamilies().indexOf("Material Symbols Rounded") < 0, "font database was not isolated")
        test.check(before.implicitWidth > 48, "missing-font negative control did not overflow")
        before.visible = false
        icons.active = true
        test.phase = 1
      } else if (test.phase === 1) {
        if (!icons.item || !OmaQ.MaterialSymbols.ready) return
        test.phase = 2
      } else if (test.phase === 2) {
        test.verify(true)
        test.check(Qt.fontFamilies().indexOf("Material Symbols Rounded") >= 0, "font was not registered in the existing engine")
        test.originalSource = OmaQ.MaterialSymbols.testSource
        OmaQ.MaterialSymbols.testSource = Qt.resolvedUrl("missing-font.ttf")
        test.phase = 3
      } else if (test.phase === 3) {
        test.verify(false)
        OmaQ.MaterialSymbols.testSource = test.originalSource
        test.phase = 4
      } else if (test.phase === 4) {
        if (!OmaQ.MaterialSymbols.ready) return
        test.phase = 5
      } else if (test.phase === 5) {
        test.verify(true)
        if (Quickshell.env("OMAQ_ICON_CAPTURE") === "1") {
          test.phase = 6
          window.contentItem.grabToImage(function(image) {
            test.check(image.saveToFile(Qt.resolvedUrl("icons.png").toString().replace("file://", "")), "capture failed")
            console.log("material-symbols: qml: ok")
            Qt.quit()
          })
        } else {
          console.log("material-symbols: qml: ok")
          Qt.quit()
        }
      }
    }
  }
}
QML
set +e
env -u DISPLAY -u WAYLAND_DISPLAY -u FONTCONFIG_PATH \
  HOME="$tmp/home" XDG_RUNTIME_DIR="$tmp/runtime" XDG_CONFIG_HOME="$tmp/config" \
  XDG_CACHE_HOME="$tmp/cache" XDG_DATA_HOME="$tmp/data" FONTCONFIG_FILE="$tmp/fonts.conf" \
  DBUS_SESSION_BUS_ADDRESS="unix:path=$tmp/no-dbus" PULSE_SERVER="unix:$tmp/no-pulse" \
  PIPEWIRE_REMOTE="$tmp/no-pipewire" QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic \
  QT_QUICK_CONTROLS_STYLE=Basic QT_QUICK_BACKEND=software \
  timeout 15s quickshell -n -p "$tmp/shell.qml" >"$tmp/log" 2>&1
rc=$?
set -e
if [ "$rc" -ne 0 ] || ! grep -q 'material-symbols: qml: ok' "$tmp/log" ||
  grep -Eq 'FAIL:|ReferenceError|TypeError|Binding loop|Unable to assign' "$tmp/log"; then
  cat "$tmp/log" >&2
  exit 1
fi
# Missing-font diagnostics are intentional; all state transitions are asserted.
if [ "${OMAQ_ICON_CAPTURE:-0}" = 1 ]; then
  trap - EXIT HUP INT TERM
  printf 'material-symbols: retained capture: %s/icons.png\n' "$tmp"
fi
printf 'material-symbols: qml: ok\n'
