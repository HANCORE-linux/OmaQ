#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin=${1:-"$root/tests/omaq_ipc_test_helper"}
printf '%s\n' 'source retained' 'managed removed' 'bundled immutable' | \
  cmp -s - "$root/tests/gold/sound/managed-copy.txt" || {
  echo "custom-sound: managed-copy gold contract changed" >&2
  exit 1
}
python3 - "$root/Panel.qml" "$root/Service.qml" "$root/ChatSurface.qml" <<'PY'
from pathlib import Path
import sys
panel = Path(sys.argv[1]).read_text()
service = Path(sys.argv[2]).read_text()
chat = Path(sys.argv[3]).read_text()
timer = panel[panel.index("id: soundActionTimer"):panel.index("id: identityActionTimer")]
if 'soundActionRequest = ""' in timer or 'soundAction = ""' in timer:
    raise SystemExit("custom-sound: timeout drops request correlation")
if "replay_sound" in service:
    raise SystemExit("custom-sound: helper replay leaked into QML protocol implementation")
if "function retryPendingSoundRequests()" not in service or \
        "pending[requestId] = { operation: action, command: command }" not in service:
    raise SystemExit("custom-sound: reconnect cannot retry an unaccepted request")
if "function managedCustomSoundPath()" not in chat or \
        "function stopUntrustedCustomSound()" not in chat or \
        "function onSoundTickChanged() { root.stopUntrustedCustomSound() }" not in chat or \
        'selectedSound === "custom" ? root.managedCustomSoundPath()' not in chat or \
        'root.service.helperCompatibility !== "compatible"' not in chat or \
        "!root.service.supportsCustomSounds" not in chat:
    raise SystemExit("custom-sound: playback bypasses helper projection")
if "root.customSounds = []" not in service or \
        "onActiveHelperProtocolChanged:" not in service or \
        'root.helperCompatibility !== "compatible" || !root.supportsCustomSounds' not in service:
    raise SystemExit("custom-sound: stale helper projection survives replacement")
PY
tmp=$(mktemp -d /tmp/omaq-custom-sound-XXXXXX)
pid=""
cleanup() {
  exec 3>&- 2>/dev/null || true
  [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
  [ -n "$pid" ] && wait "$pid" 2>/dev/null || true
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$tmp/home" "$tmp/state"
python3 - "$tmp/source.wav" <<'PY'
import struct, sys
samples = bytes((128, 129, 130, 131))
fmt = struct.pack("<HHIIHH", 1, 1, 8000, 8000, 1, 8)
body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
body += b"data" + struct.pack("<I", len(samples)) + samples
open(sys.argv[1], "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)
PY
chmod 600 "$tmp/source.wav"
ln -s "$tmp/source.wav" "$tmp/source-link.wav"
mkfifo "$tmp/input"
OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" \
  "$bin" <"$tmp/input" >"$tmp/output" 2>"$tmp/error" &
pid=$!
exec 3>"$tmp/input"
printf '%s\n' '{"op":"status","id":"custom-sound-status"}' >&3
i=0
while [ "$i" -lt 50 ] && ! grep -q '"request":"custom-sound-status"' "$tmp/output"; do
  i=$((i + 1))
  sleep 0.05
done
[ "$i" -lt 50 ] || { echo "custom-sound: helper status missing" >&2; exit 1; }
printf '{"op":"sound.import","path":"%s","request":"custom-sound-import"}\n' \
  "$tmp/source.wav" >&3
i=0
while [ "$i" -lt 50 ] && ! grep -q '"request":"custom-sound-import"' "$tmp/output"; do
  i=$((i + 1))
  sleep 0.05
done
[ "$i" -lt 50 ] || { echo "custom-sound: import result missing" >&2; exit 1; }
set -- $(python3 - "$tmp/output" <<'PY'
import json, sys
for line in open(sys.argv[1], encoding="utf-8"):
    event = json.loads(line)
    if event.get("event") == "sound.list" and event.get("request") == "custom-sound-import":
        assert event.get("op") == "import"
        assert len(event.get("items", [])) == 1
        item = event["items"][0]
        assert event.get("selected") == item["id"]
        print(item["id"], item["path"])
        break
else:
    raise SystemExit(1)
PY
)
id=$1
managed=$2
[ -f "$tmp/source.wav" ] && [ -f "$managed" ] && [ "$managed" != "$tmp/source.wav" ] || {
  echo "custom-sound: managed-copy scope failed" >&2
  exit 1
}
printf '{"op":"sound.import","path":"%s","request":"custom-sound-import"}\n' \
  "$tmp/source.wav" >&3
i=0
while [ "$i" -lt 50 ] &&
      [ "$(grep -c '"request":"custom-sound-import"' "$tmp/output")" -lt 2 ]; do
  i=$((i + 1))
  sleep 0.05
done
[ "$i" -lt 50 ] || { echo "custom-sound: idempotent import replay missing" >&2; exit 1; }
python3 - "$tmp/output" "$id" <<'PY'
import json, sys
matches = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")
           if '"request":"custom-sound-import"' in line]
assert len(matches) >= 2
assert all(len(event["items"]) == 1 and event["selected"] == sys.argv[2]
           for event in matches[-2:])
PY
before_replay=$(grep -c '"request":"custom-sound-import"' "$tmp/output")
printf '%s\n' '{"op":"status","id":"custom-sound-reconnect"}' >&3
i=0
while [ "$i" -lt 50 ] &&
      [ "$(grep -c '"request":"custom-sound-import"' "$tmp/output")" -le "$before_replay" ]; do
  i=$((i + 1))
  sleep 0.05
done
[ "$i" -lt 50 ] || { echo "custom-sound: status replay missing" >&2; exit 1; }
printf '{"op":"sound.import","path":"%s","request":"custom-sound-symlink"}\n' \
  "$tmp/source-link.wav" >&3
i=0
while [ "$i" -lt 50 ] && ! grep -q '"request":"custom-sound-symlink"' "$tmp/output"; do
  i=$((i + 1))
  sleep 0.05
done
grep '"request":"custom-sound-symlink"' "$tmp/output" | grep -q '"code":"invalid_sound"' || {
  echo "custom-sound: source symlink was accepted" >&2
  exit 1
}
printf '{"op":"sound.remove","id":"%s","request":"custom-sound-remove"}\n' "$id" >&3
i=0
while [ "$i" -lt 50 ] && ! grep -q '"request":"custom-sound-remove"' "$tmp/output"; do
  i=$((i + 1))
  sleep 0.05
done
[ "$i" -lt 50 ] || { echo "custom-sound: remove result missing" >&2; exit 1; }
grep '"request":"custom-sound-remove"' "$tmp/output" | grep -q '"items":\[\]' || {
  echo "custom-sound: removed entry remains projected" >&2
  exit 1
}
[ -f "$tmp/source.wav" ] && [ ! -e "$managed" ] || {
  echo "custom-sound: remove touched the source or retained the managed copy" >&2
  exit 1
}
[ -f "$root/sounds/icq-message.mp3" ] || {
  echo "custom-sound: bundled sound changed" >&2
  exit 1
}
echo "custom-sound: ok"
