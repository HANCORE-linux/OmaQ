#!/bin/sh
# Phase 6: two homes, one file on disk, call start/stop, record peak RSS.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase6: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p6a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p6as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p6b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p6bs-XXXXXX)
fa=$(mktemp /tmp/omaq-p6oa-XXXXXX)
fb=$(mktemp /tmp/omaq-p6ob-XXXXXX)
src=$(mktemp /tmp/omaq-p6-XXXXXX.bin)
holda=$(mktemp -u /tmp/omaq-p6fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-p6fb-XXXXXX)
audio_a=$(mktemp /tmp/omaq-p6-audio-a-XXXXXX.raw)
audio_b=$(mktemp /tmp/omaq-p6-audio-b-XXXXXX.raw)
pa=""
pb=""
pulse_modules=""
pulse_tag="omaq_p6_$$"
cap_a="${pulse_tag}_cap_a"
out_a="${pulse_tag}_out_a"
cap_b="${pulse_tag}_cap_b"
out_b="${pulse_tag}_out_b"
cleanup() {
	exec 3>&- 4>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	for module in $pulse_modules; do
		pactl unload-module "$module" 2>/dev/null || true
	done
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$src" "$holda" "$holdb" \
		"$audio_a" "$audio_b" "$fa.err" "$fb.err"
}
trap cleanup EXIT

probe_call_audio() {
	capture_sink=$1
	playback_monitor=$2
	capture_file=$3
	: >"$capture_file"
	timeout 5 parec --raw --format=s16le --rate=48000 --channels=1 \
		--latency-msec=20 --process-time-msec=20 \
		--device="$playback_monitor" >"$capture_file" &
	recorder=$!
	sleep 0.3
	python3 - <<'PY' | pacat --playback --raw --format=s16le --rate=48000 \
		--channels=1 --latency-msec=20 --process-time-msec=20 \
		--device="$capture_sink"
import math
import struct
import sys
samples = (int(14000 * math.sin(2 * math.pi * 523.25 * i / 48000)) for i in range(96000))
sys.stdout.buffer.write(b"".join(struct.pack("<h", sample) for sample in samples))
PY
	sleep 1
	kill "$recorder" 2>/dev/null || true
	wait "$recorder" 2>/dev/null || true
	python3 - "$capture_file" <<'PY'
import array
import sys
samples = array.array("h")
with open(sys.argv[1], "rb") as source:
    samples.frombytes(source.read())
if sys.byteorder != "little":
    samples.byteswap()
peak = max((abs(sample) for sample in samples), default=0)
if peak < 100:
    raise SystemExit(f"phase6: transported audio peak too low: {peak}")
PY
}

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase6: refused real home" >&2; exit 1 ;;
esac

printf 'omaq-file-probe\n' >"$src"

for tool in pactl pacat parec python3; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "phase6: missing audio test tool: $tool" >&2
		exit 1
	}
done
for sink in "$cap_a" "$out_a" "$cap_b" "$out_b"; do
	module=$(pactl load-module module-null-sink sink_name="$sink" \
		sink_properties=device.description=OmaQPhase6)
	pulse_modules="$module $pulse_modules"
done

mkfifo "$holda" "$holdb"
PULSE_SOURCE="${cap_a}.monitor" PULSE_SINK="$out_a" \
	OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$fa" 2>"$fa.err" <"$holda" &
pa=$!
PULSE_SOURCE="${cap_b}.monitor" PULSE_SINK="$out_b" \
	OMAQ_HOME="$hb" OMAQ_STATE="$sb" OMAQ_DOWNLOAD_DIR="$hb/Downloads" \
	"$bin" >"$fb" 2>"$fb.err" <"$holdb" &
pb=$!
exec 3>"$holda"
exec 4>"$holdb"
sleep 0.4

echo '{"op":"status"}' >&3
sleep 0.2
if ! grep -a -q '"addr"' "$fa"; then
	echo "phase6: no tox" >&2
	exit 1
fi

echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
[ -n "$url" ] || { echo "phase6: no invite url" >&2; exit 1; }

printf '{"op":"invite.redeem","payload":"%s"}\n' "$url" >&4
ok=0
i=0
while [ "$i" -lt 90 ]; do
	if grep -a -q '"request"' "$fa"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$ok" -eq 1 ] || { echo "phase6: no friend request" >&2; exit 1; }
echo '{"op":"contact.decide","id":"x","accept":true}' >&3

sent=0
i=0
while [ "$i" -lt 60 ]; do
	printf '{"op":"msg.send","conversation":"0","text":"ping","id":"phase6-ping-%s"}\n' "$i" >&3
	sleep 1
	if grep -a -q '"message"' "$fb"; then
		sent=1
		break
	fi
	i=$((i + 1))
done
[ "$sent" -eq 1 ] || { echo "phase6: not connected" >&2; exit 1; }

printf '{"op":"file.send","conversation":"0","path":"%s","id":"phase6-file-send"}\n' "$src" >&3
ok=0
fid=""
i=0
while [ "$i" -lt 40 ]; do
	fid=$(grep -a '"file.offer"' "$fb" | tail -1 | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
	if [ -n "$fid" ]; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.25
done
[ "$ok" -eq 1 ] || { echo "phase6: no file.offer" >&2; tail -20 "$fb" >&2; exit 1; }

printf '{"op":"file.accept","id":"%s"}\n' "$fid" >&4
ok=0
i=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"file.done"' "$fb"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.25
done
[ "$ok" -eq 1 ] || { echo "phase6: no file.done" >&2; tail -20 "$fb" >&2; exit 1; }

got=$(find "$hb/Downloads/omaq" -type f 2>/dev/null | head -1)
[ -n "$got" ] || { echo "phase6: no dest file" >&2; exit 1; }
grep -a -q 'omaq-file-probe' "$got" || { echo "phase6: dest mismatch" >&2; exit 1; }

# A recipient decline must produce a visible terminal cancellation on both peers.
offer_before=$(grep -a -c '"event":"file.offer"' "$fb" || true)
cancel_a_before=$(grep -a -c '"event":"file.canceled"' "$fa" || true)
cancel_b_before=$(grep -a -c '"event":"file.canceled"' "$fb" || true)
printf '{"op":"file.send","conversation":"0","path":"%s","id":"phase6-file-cancel"}\n' "$src" >&3
i=0
cancel_fid=""
while [ "$i" -lt 40 ]; do
	offer_after=$(grep -a -c '"event":"file.offer"' "$fb" || true)
	if [ "$offer_after" -gt "$offer_before" ]; then
		cancel_fid=$(grep -a '"file.offer"' "$fb" | tail -1 |
			sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
		[ -n "$cancel_fid" ] && break
	fi
	i=$((i + 1))
	sleep 0.25
done
[ -n "$cancel_fid" ] || { echo "phase6: cancel offer missing" >&2; exit 1; }
printf '{"op":"file.cancel","id":"%s"}\n' "$cancel_fid" >&4
i=0
while [ "$i" -lt 40 ]; do
	cancel_a_after=$(grep -a -c '"event":"file.canceled"' "$fa" || true)
	cancel_b_after=$(grep -a -c '"event":"file.canceled"' "$fb" || true)
	[ "$cancel_a_after" -gt "$cancel_a_before" ] &&
		[ "$cancel_b_after" -gt "$cancel_b_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase6: file cancellation was not visible to both peers" >&2; exit 1; }
grep -a '"event":"file.canceled"' "$fa" | tail -1 | grep -a -q '"dir":"out"' || {
	echo "phase6: sender cancellation direction missing" >&2
	exit 1
}
grep -a '"event":"file.canceled"' "$fb" | tail -1 | grep -a -q '"dir":"in"' || {
	echo "phase6: recipient cancellation direction missing" >&2
	exit 1
}

echo '{"op":"call.start","conversation":"0"}' >&3
ok=0
i=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"call.incoming"' "$fb"; then
		ok=1
		break
	fi
	echo '{"op":"call.start","conversation":"0"}' >&3
	i=$((i + 1))
	sleep 0.5
done
[ "$ok" -eq 1 ] || { echo "phase6: no call.incoming" >&2; tail -20 "$fb" >&2; exit 1; }

ended_before=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"state":"ended"' || true)
echo '{"op":"call.stop","conversation":"0"}' >&4
i=0
while [ "$i" -lt 40 ]; do
	ended_after=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"state":"ended"' || true)
	if [ "$ended_after" -gt "$ended_before" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase6: incoming decline did not end caller" >&2; exit 1; }
sleep 0.5
incoming_before=$(grep -a -c '"event":"call.incoming"' "$fb" || true)
i=0
while [ "$i" -lt 40 ]; do
	echo '{"op":"call.start","conversation":"0"}' >&3
	sleep 0.2
	incoming_after=$(grep -a -c '"event":"call.incoming"' "$fb" || true)
	if [ "$incoming_after" -gt "$incoming_before" ]; then
		break
	fi
	i=$((i + 1))
done
[ "$i" -lt 40 ] || { echo "phase6: second incoming call missing" >&2; exit 1; }

echo '{"op":"call.answer","conversation":"0"}' >&4
i=0
while [ "$i" -lt 40 ]; do
	if grep -a '"event":"call.state"' "$fa" | grep -a -q '"state":"active"' &&
	   grep -a '"event":"call.state"' "$fb" | grep -a -q '"state":"active"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase6: call did not become active on both peers" >&2; exit 1; }
probe_call_audio "$cap_a" "${out_b}.monitor" "$audio_b"
probe_call_audio "$cap_b" "${out_a}.monitor" "$audio_a"
if grep -a -q '"code":"audio_unavailable"' "$fa" "$fb"; then
	echo "phase6: audio backend unavailable" >&2
	exit 1
fi
ended_before=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"conversation":"0","state":"ended"' || true)
echo '{"op":"call.stop","conversation":"999"}' >&3
i=0
while [ "$i" -lt 20 ]; do
	if grep -a '"event":"error"' "$fa" | grep -a '"code":"forbidden"' |
	   grep -a -q '"conversation":"999"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase6: stale call stop was not rejected" >&2; exit 1; }
ended_after=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"conversation":"0","state":"ended"' || true)
[ "$ended_after" -eq "$ended_before" ] || { echo "phase6: stale stop ended active call" >&2; exit 1; }
peak_a=$(ps -o rss= -p "$pa" | tr -d ' ')
peak_b=$(ps -o rss= -p "$pb" | tr -d ' ')
peak=$peak_a
if [ "$peak_b" -gt "$peak" ]; then
	peak=$peak_b
fi
remote_ended_before=$(grep -a '"event":"call.state"' "$fb" | grep -a -c '"conversation":"0","state":"ended"' || true)
echo '{"op":"call.stop","conversation":"0"}' >&3
i=0
while [ "$i" -lt 40 ]; do
	remote_ended_after=$(grep -a '"event":"call.state"' "$fb" | grep -a -c '"conversation":"0","state":"ended"' || true)
	if [ "$remote_ended_after" -gt "$remote_ended_before" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase6: active hangup did not end remote" >&2; exit 1; }
echo '{"op":"status","id":"phase6-post-hangup"}' >&4
i=0
while [ "$i" -lt 20 ]; do
	if grep -a '"event":"snapshot"' "$fb" | grep -a -q '"request":"phase6-post-hangup"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase6: remote helper blocked after hangup" >&2; exit 1; }

if [ "$peak" -gt 40960 ]; then
	echo "phase6: call peak rss ${peak} kB > 40960" >&2
	exit 1
fi

echo "$peak" >"$root/.rss-call-kb" || true
echo "phase6: ok rss_call_kb=$peak dest=$got"
exit 0
