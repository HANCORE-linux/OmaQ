#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-protocol-compat-XXXXXX)
cleanup() {
	rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$tmp/home" "$tmp/state" "$tmp/helper"
cp "$root/Service.qml" "$tmp/Service.qml"

make -s -C "$root" \
	BIN_IPC_TEST_HELPER="$tmp/helper/omaq" \
	SANFLAGS="-DOMAQ_PROTOCOL_VERSION=7" \
	"$tmp/helper/omaq"

cat >"$tmp/shell.qml" <<'QML'
import QtQuick
import Quickshell
import "."

ShellRoot {
  Service { id: service }
  Timer {
    interval: 50
    repeat: true
    running: true
    property int attempts: 0
    onTriggered: {
      attempts++
      if (service.helperCompatibility === "compatible") {
        if (service.activeHelperProtocol === 7 &&
            !service.supportsIdentityActions &&
            !service.supportsAttachments &&
            !service.supportsDirectRecovery &&
            !service.supportsRedeemResults &&
            !service.exportIdentity("/tmp/blocked", "blocked-export") &&
            !service.inspectIdentity("/tmp/blocked", "", "blocked-inspect") &&
            !service.importIdentity("/tmp/blocked", true, "", "blocked-import") &&
            !service.protectIdentity("blocked-pass", "blocked-protect") &&
            !service.unprotectIdentity("blocked-pass", "blocked-unprotect") &&
            !service.setNickname("blocked", "blocked-nickname") &&
            service.redeem("legacy-invite") === "legacy") {
          console.log("OMAQ_PROTOCOL_COMPAT_OK")
        } else {
          console.log("OMAQ_PROTOCOL_COMPAT_BAD_CAPABILITIES")
        }
        Qt.quit()
      } else if (attempts >= 160) {
        console.log("OMAQ_PROTOCOL_COMPAT_TIMEOUT", service.helperCompatibility,
                    service.lastError)
        Qt.quit()
      }
    }
  }
}
QML

out="$tmp/quickshell.out"
if ! OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" \
	QT_QPA_PLATFORM=offscreen timeout 12 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
	cat "$out" >&2
	echo "protocol-compat: Quickshell fixture failed" >&2
	exit 1
fi
if ! grep -q 'OMAQ_PROTOCOL_COMPAT_OK' "$out"; then
	cat "$out" >&2
	echo "protocol-compat: Protocol-7 helper was not accepted with newer capabilities disabled" >&2
	exit 1
fi

echo "protocol-compat: ok"
