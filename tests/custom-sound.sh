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
    ("off", "Off"), ("qq", "PING"), ("msn", "MAIL"),
    ("aurora", "Aurora"), ("glow", "Glow"), ("click", "Click"),
    ("knock", "Knock")]
if presets != expected_presets:
    raise SystemExit(f"custom-sound: unexpected bundled presets: {presets!r}")
sound_schema = next(item for item in manifest["barWidget"]["schema"]
                    if item["key"] == "sound")
expected_options = ["off", "qq", "msn", "aurora", "glow", "click",
                    "knock", "custom"]
if sound_schema["options"] != expected_options or \
        sound_schema["defaultValue"] != "knock" or \
        manifest["barWidget"]["defaults"]["sound"] != "knock":
    raise SystemExit("custom-sound: manifest presets differ from the panel")
expected_license = (
    "MIT AND GPL-3.0-only AND CC-BY-SA-4.0 AND CC0-1.0 AND "
    "OFL-1.1-no-RFN AND LicenseRef-Pixabay-Content"
)
if manifest["license"] != expected_license:
    raise SystemExit("custom-sound: manifest license expression is incomplete")
panel_sound = panel[panel.index("readonly property string notificationSound:"):
                    panel.index("readonly property string notificationSoundPath:")]
panel_default = re.search(r'root\.settings\.sound \|\| "([^"]+)"', panel_sound)
panel_fallback = re.search(r'\n    return "([^"]+)"\n  }', panel_sound)
chat_sound = chat[chat.index("readonly property string soundName:"):
                  chat.index("readonly property string soundCustom:")]
chat_default = re.search(r'setting\("sound", "([^"]+)"\)', chat_sound)
chat_rule = re.search(
    r'return \[(.*?)\]\.indexOf\(value\) >= 0 \? value : "([^"]+)"',
    chat_sound,
    re.DOTALL,
)
if not all((panel_default, panel_fallback, chat_default, chat_rule)):
    raise SystemExit("custom-sound: sound fallback source could not be parsed")
chat_options = re.findall(r'"([^"]+)"', chat_rule.group(1))
if chat_options != expected_options or \
        panel_default.group(1) != "knock" or panel_fallback.group(1) != "knock" or \
        chat_default.group(1) != "knock" or chat_rule.group(2) != "knock" or \
        '["qq", "msn", "aurora", "glow"].indexOf(selectedSound)' not in chat or \
        'if (selectedSound !== "custom")' not in chat:
    raise SystemExit("custom-sound: playback allowlist or fallback changed")
retired_preset = "i" + "cq-message"
panel_options = [preset[0] for preset in presets] + ["custom"]
panel_resolved = retired_preset if retired_preset in panel_options \
    else panel_fallback.group(1)
chat_resolved = retired_preset if retired_preset in chat_options \
    else chat_rule.group(2)
if panel_resolved != "knock" or chat_resolved != "knock":
    raise SystemExit("custom-sound: retired persisted preset did not resolve to Knock")
expected_audio = {"qq.oga", "msn.oga", "aurora.oga", "glow.oga",
                  "click.wav", "knock.wav", "phone.oga"}
actual_audio = {path.name for path in sounds.iterdir()
                if path.suffix in {".mp3", ".oga", ".wav"}}
if actual_audio != expected_audio:
    raise SystemExit(f"custom-sound: unexpected bundled audio: {actual_audio!r}")
expected_licenses = {"CC-BY-SA-4.0.txt", "CC0-1.0.txt"}
actual_licenses = {path.name for path in (sounds / "LICENSES").iterdir()
                   if path.is_file()}
if actual_licenses != expected_licenses:
    raise SystemExit(f"custom-sound: unexpected sound licenses: {actual_licenses!r}")
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
[ -f "$root/sounds/knock.wav" ] || {
  echo "custom-sound: bundled sound changed" >&2
  exit 1
}
echo "custom-sound: ok"
