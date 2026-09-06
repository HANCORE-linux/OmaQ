#!/bin/sh
# The private Phase 6 audio server dies with a SIGKILLed test owner.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

for tool in pactl pipewire python3 wireplumber; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "phase6-audio-server-test: SKIP: $tool unavailable"
		exit 0
	fi
done

unsafe_runtime=""
runtime=""
info=""
owner=""
server=""
server_children=""
# shellcheck disable=SC2329 # Invoked by trap.
cleanup() {
	[ -n "${unsafe_runtime:-}" ] && rm -rf "$unsafe_runtime"
	[ -n "${owner:-}" ] && kill -KILL "$owner" 2>/dev/null || true
	[ -n "${owner:-}" ] && wait "$owner" 2>/dev/null || true
	[ -n "${server:-}" ] && kill -TERM "$server" 2>/dev/null || true
	[ -n "${runtime:-}" ] && rm -rf "$runtime"
	[ -n "${info:-}" ] && rm -f "$info"
}
trap cleanup EXIT

unsafe_runtime=$(mktemp -d /tmp/omaq-p6-unsafe-runtime-XXXXXX)
printf 'retain\n' >"$unsafe_runtime/sentinel"
if python3 "$root/tests/phase6-audio-server.py" "$$" "$unsafe_runtime" \
	>/dev/null 2>&1; then
	echo "phase6-audio-server-test: unsafe runtime path was accepted" >&2
	exit 1
fi
[ -f "$unsafe_runtime/sentinel" ] || {
	echo "phase6-audio-server-test: unsafe runtime path was modified" >&2
	exit 1
}
rm -rf "$unsafe_runtime"
unsafe_runtime=""

runtime=$(mktemp -d /tmp/omaq-p6-server-test-XXXXXX)
info=$(mktemp /tmp/omaq-p6-server-test-info-XXXXXX)
chmod 700 "$runtime"

/bin/sh -c '
	python3 "$1/tests/phase6-audio-server.py" "$$" "$2" &
	child=$!
	printf "%s %s\n" "$$" "$child" >"$3"
	wait "$child"
' sh "$root" "$runtime" "$info" &
owner=$!

i=0
while [ "$i" -lt 100 ]; do
	[ -s "$info" ] && [ -S "$runtime/pulse/native" ] &&
		[ -f "$runtime/ready" ] && break
	kill -0 "$owner" 2>/dev/null || break
	i=$((i + 1))
	sleep 0.05
done
[ -s "$info" ] && [ -S "$runtime/pulse/native" ] &&
	[ -f "$runtime/ready" ] || {
	echo "phase6-audio-server-test: private server unavailable" >&2
	exit 1
}
read -r recorded_owner server <"$info"
[ "$recorded_owner" = "$owner" ] || {
	echo "phase6-audio-server-test: owner PID binding changed" >&2
	exit 1
}
server_children=$(cat "/proc/$server/task/$server/children")
[ -n "$server_children" ] || {
	echo "phase6-audio-server-test: private server children are missing" >&2
	exit 1
}

sink="omaq_p6_${owner}_cap_a"
PULSE_SERVER="unix:$runtime/pulse/native" pactl info >/dev/null
PULSE_SERVER="unix:$runtime/pulse/native" pactl load-module module-null-sink \
	sink_name="$sink" \
	sink_properties=device.description=OmaQ-Call-Test-Isolation >/dev/null
if pactl list short sinks | grep -F "$sink" >/dev/null ||
   { command -v wpctl >/dev/null 2>&1 &&
     wpctl status -n 2>/dev/null | grep -F "$sink" >/dev/null; }; then
	echo "phase6-audio-server-test: private sink reached the user registry" >&2
	exit 1
fi

kill -KILL "$owner"
wait "$owner" 2>/dev/null || true
owner=""
i=0
while [ "$i" -lt 100 ]; do
	[ ! -d "/proc/$server" ] && [ ! -e "$runtime" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ ! -d "/proc/$server" ] && [ ! -e "$runtime" ] || {
	echo "phase6-audio-server-test: private server survived owner SIGKILL" >&2
	exit 1
}
for child in $server_children; do
	[ ! -d "/proc/$child" ] || {
		echo "phase6-audio-server-test: private audio child survived owner SIGKILL" >&2
		exit 1
	}
done
server=""

echo "phase6-audio-server-test: ok"
