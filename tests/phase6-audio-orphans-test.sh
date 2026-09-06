#!/bin/sh
# A Phase 6 start reclaims exact null sinks whose PID owner died by SIGKILL.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

for tool in pactl pipewire python3 wireplumber; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "phase6-audio-orphans-test: SKIP: $tool unavailable"
		exit 0
	fi
done

owner=""
stale_module=""
live_module=""
near_module=""
pulse_runtime=""
pulse_server_pid=""
# shellcheck disable=SC2329 # Invoked by trap.
cleanup() {
	[ -n "${owner:-}" ] && kill -KILL "$owner" 2>/dev/null || true
	[ -n "${owner:-}" ] && wait "$owner" 2>/dev/null || true
	[ -n "${stale_module:-}" ] && pactl unload-module "$stale_module" 2>/dev/null || true
	[ -n "${live_module:-}" ] && pactl unload-module "$live_module" 2>/dev/null || true
	[ -n "${near_module:-}" ] && pactl unload-module "$near_module" 2>/dev/null || true
	[ -n "${pulse_server_pid:-}" ] && kill "$pulse_server_pid" 2>/dev/null || true
	[ -n "${pulse_server_pid:-}" ] && wait "$pulse_server_pid" 2>/dev/null || true
	[ -n "${pulse_runtime:-}" ] && rm -rf "$pulse_runtime"
}
trap cleanup EXIT

module_exists() {
	pactl list short modules | awk -v wanted="$1" '
		$1 == wanted { found = 1 }
		END { exit found ? 0 : 1 }
	'
}

pulse_runtime=$(mktemp -d /tmp/omaq-p6-orphan-pulse-XXXXXX)
chmod 700 "$pulse_runtime"
python3 "$root/tests/phase6-audio-server.py" "$$" "$pulse_runtime" &
pulse_server_pid=$!
i=0
while [ "$i" -lt 100 ]; do
	[ -S "$pulse_runtime/pulse/native" ] && [ -f "$pulse_runtime/ready" ] && break
	kill -0 "$pulse_server_pid" 2>/dev/null || break
	i=$((i + 1))
	sleep 0.05
done
[ -S "$pulse_runtime/pulse/native" ] && [ -f "$pulse_runtime/ready" ] || {
	echo "phase6-audio-orphans-test: private PulseAudio unavailable" >&2
	exit 1
}
PULSE_SERVER="unix:$pulse_runtime/pulse/native"
export PULSE_SERVER
pactl info >/dev/null

sleep 300 &
owner=$!
stale_sink="omaq_p6_${owner}_cap_a"
live_sink="omaq_p6_$$_out_b"
near_sink="omaq_p6_${owner}_cap_c"
stale_module=$(pactl load-module module-null-sink \
	sink_name="$stale_sink" \
	sink_properties=device.description=OmaQ-Call-Orphan-Test)
live_module=$(pactl load-module module-null-sink \
	sink_name="$live_sink" \
	sink_properties=device.description=OmaQ-Call-Live-Test)
near_module=$(pactl load-module module-null-sink \
	sink_name="$near_sink" \
	sink_properties=device.description=OmaQ-Call-Near-Match-Test)

if command -v wpctl >/dev/null 2>&1 &&
   wpctl status -n 2>/dev/null |
     grep -F -e "$stale_sink" -e "$live_sink" -e "$near_sink" >/dev/null; then
	echo "phase6-audio-orphans-test: private test device reached the user server" >&2
	exit 1
fi

kill -KILL "$owner"
wait "$owner" 2>/dev/null || true
owner=""

sh "$root/tests/phase6-audio-orphans.sh"
if module_exists "$stale_module"; then
	echo "phase6-audio-orphans-test: SIGKILL orphan remained" >&2
	exit 1
fi
if ! module_exists "$live_module"; then
	echo "phase6-audio-orphans-test: live Phase 6 module was removed" >&2
	exit 1
fi
if ! module_exists "$near_module"; then
	echo "phase6-audio-orphans-test: near-match module was removed" >&2
	exit 1
fi
stale_module=""

echo "phase6-audio-orphans-test: ok"
