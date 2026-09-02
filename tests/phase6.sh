#!/bin/sh
# Phase 6: two homes, one file on disk, call start/stop, record peak RSS.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
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
# shellcheck disable=SC2329 # Invoked by trap.
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
i=0
friend_key_a=""
friend_key_b=""
while [ "$i" -lt 60 ]; do
	friend_key_a=$(grep -a '"event":"friend.info"' "$fa" | grep -a '"id":"0"' |
		tail -1 | sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
	friend_key_b=$(grep -a '"event":"friend.info"' "$fb" | grep -a '"id":"0"' |
		tail -1 | sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
	[ "${#friend_key_a}" -eq 64 ] && [ "${#friend_key_b}" -eq 64 ] && break
	i=$((i + 1))
	sleep 0.2
done
[ "${#friend_key_a}" -eq 64 ] && [ "${#friend_key_b}" -eq 64 ] || {
	echo "phase6: stable friend keys missing" >&2
	exit 1
}

status_sequence=0
direct_peers_online() {
	status_sequence=$((status_sequence + 1))
	request_a="phase6-online-a-$status_sequence"
	request_b="phase6-online-b-$status_sequence"
	printf '{"op":"status","id":"%s"}\n' "$request_a" >&3
	printf '{"op":"status","id":"%s"}\n' "$request_b" >&4
	python3 - "$fa" "$request_a" "$fb" "$request_b" <<'PY'
import json
import sys
import time


def decode_records(lines, path):
    result = []
    for index, line in enumerate(lines):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            if index == len(lines) - 1 and not line.endswith("\n"):
                continue
            raise
        if not isinstance(value, dict):
            raise SystemExit(f"phase6: non-object JSON record in {path}")
        result.append(value)
    return result


def records(path):
    with open(path, encoding="utf-8", errors="strict") as stream:
        return decode_records(stream.readlines(), path)


def check_parser_fixtures():
    for label, payload in (("terminated", "null\n"),
                           ("unterminated", "null")):
        try:
            decode_records([payload], f"<status-{label}-null-fixture>")
        except SystemExit as error:
            if "non-object JSON record" in str(error):
                continue
        raise SystemExit(
            f"phase6: status parser accepted a complete {label} null record"
        )
    complete = decode_records(["{\"event\":\"fixture\"}"],
                              "<status-complete-fixture>")
    incomplete = decode_records(["{\"event\":"],
                                "<status-incomplete-fixture>")
    if complete != [{"event": "fixture"}] or incomplete:
        raise SystemExit("phase6: status parser partial-line fixture failed")


check_parser_fixtures()


def projection(path, request):
    values = records(path)
    snapshot_index = next((
        index for index in range(len(values) - 1, -1, -1)
        if values[index].get("event") == "snapshot"
        and values[index].get("request") == request
    ), None)
    if snapshot_index is None:
        return None
    generation = None
    friend_online = False
    for value in values[snapshot_index + 1:]:
        if value.get("event") == "friend.list.begin" and generation is None:
            generation = value.get("generation")
        elif generation is not None and value.get("generation") == generation:
            if value.get("event") == "friend.info" and value.get("id") == "0":
                friend_online = value.get("online") is True
            elif value.get("event") == "friend.list.end":
                return values[snapshot_index].get("online") is True and friend_online
    return None


deadline = time.monotonic() + 3
while time.monotonic() < deadline:
    states = (projection(sys.argv[1], sys.argv[2]),
              projection(sys.argv[3], sys.argv[4]))
    if all(state is not None for state in states):
        raise SystemExit(0 if all(states) else 1)
    time.sleep(0.05)
raise SystemExit(2)
PY
}

online=0
i=0
while [ "$i" -lt 90 ]; do
	if direct_peers_online; then
		online=1
		break
	fi
	i=$((i + 1))
	sleep 1
done
if [ "$online" -ne 1 ]; then
	echo "phase6: public Tox connectivity did not make both direct peers online" >&2
	tail -20 "$fa.err" "$fb.err" >&2
	exit 1
fi

sent=0
offline_during_send=0
i=0
while [ "$i" -lt 60 ]; do
	printf '{"op":"msg.send","conversation":"0","key":"%s","text":"ping","id":"phase6-ping-%s"}\n' "$friend_key_a" "$i" >&3
	sleep 1
	if grep -a -q '"message"' "$fb"; then
		sent=1
		break
	fi
	if ! direct_peers_online; then
		offline_during_send=1
	fi
	i=$((i + 1))
done
if [ "$sent" -ne 1 ]; then
	if [ "$offline_during_send" -eq 1 ] || ! direct_peers_online; then
		echo "phase6: public Tox connectivity was unavailable during direct messaging" >&2
	else
		echo "phase6: encrypted direct messaging did not establish while both peers were online" >&2
	fi
	tail -20 "$fa.err" "$fb.err" >&2
	exit 1
fi

printf '{"op":"file.send","conversation":"0","key":"%s","path":"%s","id":"phase6-file-send"}\n' "$friend_key_a" "$src" >&3
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
busy_before=$(grep -a -c '"code":"busy"' "$fb" || true)
printf '{"op":"contact.remove","id":"0","key":"%s"}\n' "$friend_key_b" >&4
i=0
while [ "$i" -lt 20 ]; do
	busy_after=$(grep -a -c '"code":"busy"' "$fb" || true)
	[ "$busy_after" -gt "$busy_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase6: contact removal ignored active file" >&2; exit 1; }

printf '{"op":"file.accept","conversation":"0","key":"%s","id":"%s"}\n' "$friend_key_b" "$fid" >&4
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
python3 - "$fa" "$fb" "$ha/history/d:$friend_key_a/messages.jsonl" \
	"$hb/history/d:$friend_key_b/messages.jsonl" "$src" "$got" <<'PY'
import json
import sys
import time


def decode_records(lines, path):
    result = []
    for index, line in enumerate(lines):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            if index == len(lines) - 1 and not line.endswith("\n"):
                continue
            raise
        if not isinstance(value, dict):
            raise SystemExit(f"phase6: non-object JSON record in {path}")
        result.append(value)
    return result


def records(path):
    try:
        with open(path, encoding="utf-8", errors="strict") as stream:
            return decode_records(stream.readlines(), path)
    except FileNotFoundError:
        return []


def check_parser_fixtures():
    for label, payload in (("terminated", "null\n"),
                           ("unterminated", "null")):
        try:
            decode_records([payload], f"<attachment-{label}-null-fixture>")
        except SystemExit as error:
            if "non-object JSON record" in str(error):
                continue
        raise SystemExit(
            f"phase6: attachment parser accepted a complete {label} null record"
        )
    complete = decode_records(["{\"event\":\"fixture\"}"],
                              "<attachment-complete-fixture>")
    incomplete = decode_records(["{\"event\":"],
                                "<attachment-incomplete-fixture>")
    if complete != [{"event": "fixture"}] or incomplete:
        raise SystemExit("phase6: attachment parser partial-line fixture failed")


check_parser_fixtures()


sides = (
    ("sender", sys.argv[1], sys.argv[3], sys.argv[5], "out"),
    ("receiver", sys.argv[2], sys.argv[4], sys.argv[6], "in"),
)
deadline = time.monotonic() + 10
while True:
    synchronized = []
    for label, event_path, history_path, message_path, direction in sides:
        event = next((
            item for item in reversed(records(event_path))
            if item.get("event") == "message" and item.get("dir") == direction
            and item.get("kind") == "file" and item.get("text") == message_path
        ), None)
        stored = next((
            item for item in reversed(records(history_path))
            if event and item.get("id") == event.get("id")
        ), None)
        synchronized.append((label, event, stored))
    if all(event and stored for _, event, stored in synchronized):
        break
    if time.monotonic() >= deadline:
        missing = []
        for label, event, stored in synchronized:
            if not event:
                missing.append(f"{label} event")
            elif not stored:
                missing.append(f"{label} history entry")
        raise SystemExit(
            "phase6: attachment timestamp synchronization timed out waiting for "
            + ", ".join(missing)
        )
    time.sleep(0.1)

def timestamp_failures(pairs):
    missing = []
    mismatches = []
    for label, event, stored in pairs:
        event_stamp = event.get("ts")
        history_stamp = stored.get("ts")
        event_valid = (isinstance(event_stamp, int)
                       and not isinstance(event_stamp, bool) and event_stamp > 0)
        history_valid = (isinstance(history_stamp, int)
                         and not isinstance(history_stamp, bool) and history_stamp > 0)
        if not event_valid:
            missing.append(f"{label} event")
        if not history_valid:
            missing.append(f"{label} history")
        if event_valid and history_valid and event_stamp != history_stamp:
            mismatches.append(
                f"{label} event={event_stamp}, {label} history={history_stamp}"
            )
    return missing, mismatches


fixture = (
    ("sender", {"ts": 100}, {"ts": 100}),
    ("receiver", {"ts": 101}, {"ts": 101}),
)
if timestamp_failures(fixture) != ([], []):
    raise SystemExit("phase6: cross-peer timestamp fixture failed")
mismatch_fixture = (("sender", {"ts": 100}, {"ts": 101}),)
if not timestamp_failures(mismatch_fixture)[1]:
    raise SystemExit("phase6: local timestamp mismatch fixture failed")

missing, mismatches = timestamp_failures(synchronized)
if missing:
    raise SystemExit(
        "phase6: authoritative attachment timestamp missing from "
        + ", ".join(missing)
    )
if mismatches:
    raise SystemExit(
        "phase6: attachment event/history timestamp mismatch: "
        + "; ".join(mismatches)
    )
PY

# A recipient decline must produce a visible terminal cancellation on both peers.
offer_before=$(grep -a -c '"event":"file.offer"' "$fb" || true)
cancel_a_before=$(grep -a -c '"event":"file.canceled"' "$fa" || true)
cancel_b_before=$(grep -a -c '"event":"file.canceled"' "$fb" || true)
printf '{"op":"file.send","conversation":"0","key":"%s","path":"%s","id":"phase6-file-cancel"}\n' "$friend_key_a" "$src" >&3
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
printf '{"op":"file.cancel","conversation":"0","key":"%s","id":"%s"}\n' "$friend_key_b" "$cancel_fid" >&4
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

printf '{"op":"call.start","conversation":"0","key":"%s"}\n' "$friend_key_a" >&3
ok=0
i=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"call.incoming"' "$fb"; then
		ok=1
		break
	fi
	printf '{"op":"call.start","conversation":"0","key":"%s"}\n' "$friend_key_a" >&3
	i=$((i + 1))
	sleep 0.5
done
[ "$ok" -eq 1 ] || { echo "phase6: no call.incoming" >&2; tail -20 "$fb" >&2; exit 1; }

ended_before=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"state":"ended"' || true)
printf '{"op":"call.stop","conversation":"0","key":"%s"}\n' "$friend_key_b" >&4
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
	printf '{"op":"call.start","conversation":"0","key":"%s"}\n' "$friend_key_a" >&3
	sleep 0.2
	incoming_after=$(grep -a -c '"event":"call.incoming"' "$fb" || true)
	if [ "$incoming_after" -gt "$incoming_before" ]; then
		break
	fi
	i=$((i + 1))
done
[ "$i" -lt 40 ] || { echo "phase6: second incoming call missing" >&2; exit 1; }

printf '{"op":"call.answer","conversation":"0","key":"%s"}\n' "$friend_key_b" >&4
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
busy_before=$(grep -a -c '"code":"busy"' "$fa" || true)
printf '{"op":"contact.remove","id":"0","key":"%s"}\n' "$friend_key_a" >&3
i=0
while [ "$i" -lt 20 ]; do
	busy_after=$(grep -a -c '"code":"busy"' "$fa" || true)
	[ "$busy_after" -gt "$busy_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase6: contact removal ignored active call" >&2; exit 1; }
probe_call_audio "$cap_a" "${out_b}.monitor" "$audio_b"
probe_call_audio "$cap_b" "${out_a}.monitor" "$audio_a"
if grep -a -q '"code":"audio_unavailable"' "$fa" "$fb"; then
	echo "phase6: audio backend unavailable" >&2
	exit 1
fi
ended_before=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"conversation":"0".*"state":"ended"' || true)
printf '{"op":"call.stop","conversation":"999","key":"%s"}\n' "$friend_key_a" >&3
i=0
while [ "$i" -lt 20 ]; do
	if grep -a '"event":"error"' "$fa" | grep -a '"code":"identity_changed"' |
	   grep -a -q '"conversation":"999"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase6: stale call stop was not rejected" >&2; exit 1; }
ended_after=$(grep -a '"event":"call.state"' "$fa" | grep -a -c '"conversation":"0".*"state":"ended"' || true)
[ "$ended_after" -eq "$ended_before" ] || { echo "phase6: stale stop ended active call" >&2; exit 1; }
peak_a=$(ps -o rss= -p "$pa" | tr -d ' ')
peak_b=$(ps -o rss= -p "$pb" | tr -d ' ')
peak=$peak_a
if [ "$peak_b" -gt "$peak" ]; then
	peak=$peak_b
fi
remote_ended_before=$(grep -a '"event":"call.state"' "$fb" | grep -a -c '"conversation":"0".*"state":"ended"' || true)
printf '{"op":"call.stop","conversation":"0","key":"%s"}\n' "$friend_key_a" >&3
i=0
while [ "$i" -lt 40 ]; do
	remote_ended_after=$(grep -a '"event":"call.state"' "$fb" | grep -a -c '"conversation":"0".*"state":"ended"' || true)
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

echo "phase6: ok rss_call_kb=$peak dest=$got"
exit 0
