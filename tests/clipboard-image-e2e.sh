#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
source_bin=${1:-$root/helper/omaq}
[ -x "$source_bin" ] || { echo "clipboard-image-e2e: helper binary missing" >&2; exit 1; }
tmp=$(mktemp -d /tmp/omaq-clipboard-image-XXXXXX)
cleanup() {
	if [ -f "$tmp/state/omaq.pid" ]; then
		pid=$(cat "$tmp/state/omaq.pid" 2>/dev/null || true)
		case "$pid" in
			''|*[!0-9]*) ;;
			*)
				exe=$(readlink "/proc/$pid/exe" 2>/dev/null || true)
				if [ "$exe" = "$tmp/helper/omaq" ]; then
					kill "$pid" 2>/dev/null || true
					i=0
					while [ "$i" -lt 50 ] && kill -0 "$pid" 2>/dev/null; do
						i=$((i + 1))
						sleep 0.02
					done
				fi
				;;
		esac
	fi
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$tmp/home" "$tmp/state" "$tmp/bin" "$tmp/helper"
cp "$source_bin" "$tmp/helper/omaq"
chmod 755 "$tmp/helper/omaq"
ln -s "$root/pages" "$tmp/pages"
ln -s "$root/assets" "$tmp/assets"
ln -s "$root/scripts" "$tmp/scripts"
ln -s "$root/CallTone.qml" "$tmp/CallTone.qml"
ln -s "$root/Emoji.js" "$tmp/Emoji.js"
ln -s "$root/MessageLayout.js" "$tmp/MessageLayout.js"
ln -s "$root/SurfaceCoordinator.qml" "$tmp/SurfaceCoordinator.qml"
ln -s "$root/Service.qml" "$tmp/Service.qml"
ln -s "$root/SafeText.qml" "$tmp/SafeText.qml"
ln -s "$root/qmldir" "$tmp/qmldir"
ln -s /usr/share/omarchy/shell/Ui "$tmp/Ui"
ln -s /usr/share/omarchy/shell/Commons "$tmp/Commons"
cat >"$tmp/bin/wl-paste" <<'SH'
#!/bin/sh
case "${1:-}" in
  --list-types) printf '%s\n' image/png ;;
  --type) cat -- "$OMAQ_TEST_CLIPBOARD" ;;
  *) exit 2 ;;
esac
SH
chmod 755 "$tmp/bin/wl-paste"
cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "." as OmaQ
import "pages" as Pages

ShellRoot {
  OmaQ.Service { id: service }
  Item {
    Pages.ChatPage {
      id: page
      width: 420
      height: 520
      service: service
      conversation: "g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      peerName: "Clipboard test"
      theme: ({ bg: "#111111", fg: "#eeeeee", accent: "#77cc66", unread: "#cc7777" })
    }
  }
  Timer {
    interval: 50
    repeat: true
    running: true
    property int attempts: 0
    property bool started: false
    onTriggered: {
      attempts++
      if (!started && service.helperCompatibility === "compatible" &&
          service.supportsGroupAttachments && !service.awaitingHelperInstance) {
        started = true
        page.pasteComposer()
      }
      if (started && page.pendingImagePath !== "") {
        var ok = page.pendingImageStageRequest !== "" &&
          page.pendingImagePath.slice(-4) === ".png" &&
          page.fileStatus === "" && page.attachmentsAvailable
        console.log(ok ? "OMAQ_CLIPBOARD_IMAGE_OK" : "OMAQ_CLIPBOARD_IMAGE_BAD",
          page.fileStatus)
        page.clearPendingImage()
        Qt.quit()
      } else if (attempts >= 240) {
        console.log("OMAQ_CLIPBOARD_IMAGE_TIMEOUT", service.helperCompatibility,
          service.lastError, page.fileStatus)
        Qt.quit()
      }
    }
  }
}
QML
out="$tmp/out"
if ! PATH="$tmp/bin:$PATH" OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" \
	OMAQ_TEST_CLIPBOARD="$root/assets/OmaQ_Final-panel.png" \
	QT_QPA_PLATFORM=offscreen timeout 15 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
	cat "$out" >&2
	echo "clipboard-image-e2e: Quickshell fixture failed" >&2
	exit 1
fi
if ! grep -q 'OMAQ_CLIPBOARD_IMAGE_OK' "$out"; then
	cat "$out" >&2
	echo "clipboard-image-e2e: staged group image did not reach pending preview" >&2
	exit 1
fi
echo "clipboard-image-e2e: ok"
