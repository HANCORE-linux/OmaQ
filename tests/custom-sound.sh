#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin=${1:-"$root/tests/omaq_ipc_test_helper"}
printf '%s\n' 'source retained' 'managed removed' 'bundled immutable' | \
  cmp -s - "$root/tests/gold/sound/managed-copy.txt" || {
  echo "custom-sound: managed-copy gold contract changed" >&2
  exit 1
}
python3 - "$root/Panel.qml" "$root/Service.qml" "$root/ChatSurface.qml" \
  "$root/manifest.json" "$root/sounds" <<'PY'
import hashlib
import json
from pathlib import Path
import re
import sys
panel = Path(sys.argv[1]).read_text()
service = Path(sys.argv[2]).read_text()
chat = Path(sys.argv[3]).read_text()
manifest = json.loads(Path(sys.argv[4]).read_text())
sounds = Path(sys.argv[5])
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
preset_block = panel[panel.index("readonly property var bundledNotificationSounds:"):
                     panel.index("readonly property var notificationSounds:")]
presets = re.findall(r'\{ id: "([^"]+)", label: "([^"]+)"', preset_block)
expected_presets = [
    ("off", "Off"), ("icq-message", "UHOH"), ("qq", "PING"),
    ("msn", "MAIL"), ("aurora", "Aurora"), ("glow", "Glow"),
    ("click", "Click"), ("knock", "Knock")]
if presets != expected_presets:
    raise SystemExit(f"custom-sound: unexpected bundled presets: {presets!r}")
sound_schema = next(item for item in manifest["barWidget"]["schema"]
                    if item["key"] == "sound")
expected_options = ["off", "icq-message", "qq", "msn", "aurora", "glow",
                    "click", "knock", "custom"]
if sound_schema["options"] != expected_options or \
        sound_schema["defaultValue"] != "icq-message":
    raise SystemExit("custom-sound: manifest presets differ from the panel")
expected_license = (
    "MIT AND GPL-3.0-only AND Apache-2.0 AND CC-BY-SA-4.0 AND "
    "CC0-1.0 AND OFL-1.1-no-RFN AND LicenseRef-Pixabay-Content"
)
if manifest["license"] != expected_license:
    raise SystemExit("custom-sound: manifest license expression is incomplete")
if 'return ["off", "icq-message", "qq", "msn", "aurora", "glow", "click",' not in chat or \
        '"knock", "custom"].indexOf(value) >= 0 ? value : "icq-message"' not in chat or \
        '["qq", "msn", "aurora", "glow"].indexOf(selectedSound)' not in chat or \
        'Qt.resolvedUrl("sounds/icq-message.mp3")' not in chat:
    raise SystemExit("custom-sound: playback allowlist or fallback changed")
expected_audio = {"icq-message.mp3", "qq.oga", "msn.oga", "aurora.oga",
                  "glow.oga", "click.wav", "knock.wav", "phone.oga"}
actual_audio = {path.name for path in sounds.iterdir()
                if path.suffix in {".mp3", ".oga", ".wav"}}
if actual_audio != expected_audio:
    raise SystemExit(f"custom-sound: unexpected bundled audio: {actual_audio!r}")
if (sounds / "LICENSES" / "CC-BY-4.0.txt").exists():
    raise SystemExit("custom-sound: removed CC BY preset license remains")
icq_path = sounds / "icq-message.mp3"
expected_icq_sha256 = "14dcb321bb71e37bdd1cf7a9e2b3b3fbcf759e2043eeff1ad69885c13c244cf1"
if hashlib.sha256(icq_path.read_bytes()).hexdigest() != expected_icq_sha256:
    raise SystemExit("custom-sound: ICQ asset hash changed")
attribution = (sounds / "ATTRIBUTION.md").read_text()
for required in (
    "https://github.com/mail-ru-im/im-desktop/blob/78924d804fc38a5746a073d5bdb71c1c4cc97780/products/icq/app/resources/sounds/incoming.wav",
    "78924d804fc38a5746a073d5bdb71c1c4cc97780",
    "Copyright (C) 2016 ICQ LLC (Mail.Ru Group)",
    "Apache License 2.0 (`LICENSES/Apache-2.0.txt`)",
    "6060dfb8fc8fdc1b58bd9482f57c491a3b73a61f4289dbc8d2b5c7d4d54f406f",
    expected_icq_sha256,
    "leading and trailing silence",
    "does not claim ICQ endorsement or trademark rights",
    "Upstream notice: `LICENSES/ICQ-NOTICE.md`",
):
    if required not in attribution:
        raise SystemExit(f"custom-sound: missing ICQ attribution: {required}")
apache = sounds / "LICENSES" / "Apache-2.0.txt"
if hashlib.sha256(apache.read_bytes()).hexdigest() != \
        "074e6e32c86a4c0ef8b3ed25b721ca23aca83df277cd88106ef7177c354615ff":
    raise SystemExit("custom-sound: Apache-2.0 license text changed")
notice = (sounds / "LICENSES" / "ICQ-NOTICE.md").read_text()
for required in (
    "Copyright 2016 ICQ LLC (Mail.Ru Group)",
    "Licensed under the Apache License, Version 2.0",
    "not redistributed as part of this sound",
):
    if required not in notice:
        raise SystemExit(f"custom-sound: missing ICQ notice text: {required}")
PY
grep -Fxq "license=('MIT' 'GPL-3.0-only' 'Apache-2.0' 'CC-BY-SA-4.0' 'CC0-1.0' 'OFL-1.1-no-RFN' 'custom:Pixabay Content License')" \
  "$root/packaging/PKGBUILD" || {
  echo "custom-sound: PKGBUILD license array is incomplete" >&2
  exit 1
}
grep -Fxq "| ICQ Desktop incoming-message sound | Derived \`sounds/icq-message.mp3\` | Apache-2.0 | Bundled UHOH notification sound; see [\`sounds/ATTRIBUTION.md\`](sounds/ATTRIBUTION.md) |" \
  "$root/THIRD_PARTY.md" || {
  echo "custom-sound: third-party ICQ attribution is missing" >&2
  exit 1
}
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
# shellcheck disable=SC2046 # Split the helper's exact id/path pair into positional fields.
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
