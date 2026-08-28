#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
source_bin=${1:-$root/helper/omaq}
[ -x "$source_bin" ] || { echo "helper-detached: helper binary missing" >&2; exit 1; }
tmp=$(mktemp -d /tmp/omaq-helper-detached-XXXXXX)
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
mkdir -m 700 "$tmp/home" "$tmp/state" "$tmp/helper"
cp "$source_bin" "$tmp/helper/omaq"
cp "$root/Service.qml" "$tmp/Service.qml"
chmod 755 "$tmp/helper/omaq"
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
      if (service.helperCompatibility === "compatible" && service.helperInstance !== "") {
        console.log("OMAQ_DETACHED_INSTANCE", service.helperInstance)
        Qt.quit()
      } else if (attempts >= 200) {
        console.log("OMAQ_DETACHED_TIMEOUT", service.lastError)
        Qt.quit()
      }
    }
  }
}
QML
run_service() {
	out=$1
	if ! OMAQ_HOME="$tmp/home" OMAQ_STATE="$tmp/state" QT_QPA_PLATFORM=offscreen \
		timeout 12 quickshell -p "$tmp/shell.qml" >"$out" 2>&1; then
		cat "$out" >&2
		return 1
	fi
	grep 'OMAQ_DETACHED_INSTANCE' "$out" | tail -1 | awk '{print $NF}'
}
first=$(run_service "$tmp/first.out") || {
	echo "helper-detached: first Service failed" >&2
	exit 1
}
pid=$(cat "$tmp/state/omaq.pid" 2>/dev/null || true)
case "$pid" in ''|*[!0-9]*) echo "helper-detached: invalid helper pid" >&2; exit 1;; esac
[ "$(readlink "/proc/$pid/exe" 2>/dev/null || true)" = "$tmp/helper/omaq" ] || {
	echo "helper-detached: helper died with the first shell" >&2
	exit 1
}
python3 - "$tmp/state/omaq.sock" <<'PY'
import json, os, socket, sys, time
sock_path = sys.argv[1]
sequence = 0

def receive(client, buffer, predicate):
    deadline = time.time() + 5
    while time.time() < deadline:
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            event = json.loads(line)
            if predicate(event):
                return event, buffer
        try:
            chunk = client.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        buffer += chunk
    raise RuntimeError("helper event timeout")

def connect_ready(label):
    global sequence
    sequence += 1
    request = f"detached-status-{label}-{sequence}"
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(0.5)
    client.connect(sock_path)
    client.sendall((json.dumps({"op": "status", "id": request}) + "\n").encode())
    event, buffer = receive(client, b"", lambda value:
        value.get("event") == "snapshot" and value.get("request") == request)
    instance = str(event.get("instance", ""))
    if not instance:
        raise RuntimeError("missing detached helper instance")
    client.sendall((json.dumps({"op": "identity.ready", "id": instance}) + "\n").encode())
    return client, buffer

def create_stage(client, buffer, request):
    client.sendall((json.dumps({"op": "attachment.stage.create", "id": request}) + "\n").encode())
    event, buffer = receive(client, buffer, lambda value:
        value.get("event") == "attachment.stage" and value.get("request") == request)
    path = str(event.get("path", ""))
    if not path or not os.path.exists(path):
        raise RuntimeError("missing owned attachment stage")
    return path, buffer

owner, owner_buffer = connect_ready("owner-one")
first_path, owner_buffer = create_stage(owner, owner_buffer, "detached-stage-owner-one")
thief, thief_buffer = connect_ready("thief")
thief.sendall(b'{"op":"attachment.stage.create","id":"detached-stage-owner-one"}\n')
_, thief_buffer = receive(thief, thief_buffer, lambda value:
    value.get("event") == "attachment.rejected" and
    value.get("request") == "detached-stage-owner-one")
if not os.path.exists(first_path):
    raise RuntimeError("foreign create removed the owned stage")
thief.sendall((json.dumps({"op": "attachment.inspect",
                           "id": "detached-stage-owner-one",
                           "path": first_path}) + "\n").encode())
_, thief_buffer = receive(thief, thief_buffer, lambda value:
    value.get("event") == "attachment.rejected" and
    value.get("request") == "detached-stage-owner-one")
if not os.path.exists(first_path):
    raise RuntimeError("foreign inspect removed the owned stage")
thief.sendall((json.dumps({"op": "test.attachment.adopt", "id": "foreign-adopt",
                           "path": first_path}) + "\n").encode())
_, thief_buffer = receive(thief, thief_buffer, lambda value:
    value.get("event") == "error" and value.get("code") == "forbidden")
if not os.path.exists(first_path):
    raise RuntimeError("foreign client removed the owned stage")
thief.close()
owner.close()
for _ in range(100):
    if not os.path.exists(first_path):
        break
    time.sleep(0.025)
if os.path.exists(first_path):
    raise RuntimeError("disconnected owner left an unadopted stage")

owner, owner_buffer = connect_ready("owner-two")
second_path, owner_buffer = create_stage(owner, owner_buffer, "detached-stage-owner-two")
owner.sendall((json.dumps({"op": "test.attachment.adopt", "id": "owned-adopt",
                           "path": second_path}) + "\n").encode())
_, owner_buffer = receive(owner, owner_buffer, lambda value:
    value.get("event") == "test.attachment.adopted" and value.get("id") == "owned-adopt")
owner.close()
time.sleep(0.2)
if not os.path.exists(second_path):
    raise RuntimeError("owner disconnect removed an adopted stage")
cleaner, cleaner_buffer = connect_ready("cleaner")
cleaner.sendall((json.dumps({"op": "attachment.stage.discard",
                            "id": "detached-stage-owner-two",
                            "path": second_path}) + "\n").encode())
_, cleaner_buffer = receive(cleaner, cleaner_buffer, lambda value:
    value.get("event") == "attachment.discarded" and
    value.get("request") == "detached-stage-owner-two")
cleaner.close()
if os.path.exists(second_path):
    raise RuntimeError("adopted stage cleanup failed")
PY
second=$(run_service "$tmp/second.out") || {
	echo "helper-detached: second Service failed" >&2
	exit 1
}
[ -n "$first" ] && [ "$first" = "$second" ] || {
	cat "$tmp/first.out" "$tmp/second.out" >&2
	echo "helper-detached: shell restart replaced the helper instance" >&2
	exit 1
}
[ "$(cat "$tmp/state/omaq.pid")" = "$pid" ] || {
	echo "helper-detached: shell restart changed the helper pid" >&2
	exit 1
}
echo "helper-detached: ok"
