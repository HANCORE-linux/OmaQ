#!/bin/sh
# Phase 8: a persisted Double Ratchet session survives one helper restart.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "ratchet-restart: no helper" >&2; exit 1; }

ha=$(mktemp -d /tmp/omaq-rra-XXXXXX)
sa=$(mktemp -d /tmp/omaq-rras-XXXXXX)
hb=$(mktemp -d /tmp/omaq-rrb-XXXXXX)
sb=$(mktemp -d /tmp/omaq-rrbs-XXXXXX)
fa=$(mktemp /tmp/omaq-rroa-XXXXXX)
fb=$(mktemp /tmp/omaq-rrob-XXXXXX)
fb2=$(mktemp /tmp/omaq-rrob2-XXXXXX)
holda=$(mktemp -u /tmp/omaq-rrfa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-rrfb-XXXXXX)
holdb2=$(mktemp -u /tmp/omaq-rrfb2-XXXXXX)
pa=""
pb=""
# shellcheck disable=SC2329 # Invoked by trap.
cleanup() {
	exec 3>&- 4>&- 5>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$fb2" "$holda" "$holdb" "$holdb2" \
		"$fa.err" "$fb.err" "$fb2.err"
}
trap cleanup EXIT

mkfifo "$holda" "$holdb"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$fa" 2>"$fa.err" <"$holda" &
pa=$!
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >"$fb" 2>"$fb.err" <"$holdb" &
pb=$!
exec 3>"$holda"
exec 4>"$holdb"
sleep 0.4

echo '{"op":"status"}' >&3
sleep 0.2
echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url=$(grep -a '"event":"invite"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^\"]*\)".*/\1/p')
[ -n "$url" ] || { echo "ratchet-restart: no invite" >&2; exit 1; }
printf '{"op":"invite.redeem","payload":"%s"}\n' "$url" >&4
i=0
while [ "$i" -lt 90 ]; do
	if grep -a -q '"event":"request"' "$fa"; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$i" -lt 90 ] || { echo "ratchet-restart: no request" >&2; exit 1; }
echo '{"op":"contact.decide","accept":true}' >&3
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
	echo "ratchet-restart: stable friend keys missing" >&2
	exit 1
}

# Bootstrap A -> B, then exchange one encrypted message in each direction.
i=0
while [ "$i" -lt 60 ]; do
	printf '{"op":"msg.send","conversation":"0","key":"%s","text":"restart-seed","id":"restart-seed-%s"}\n' "$friend_key_a" "$i" >&3
	sleep 1
	if grep -a -q 'restart-seed' "$fb"; then
		break
	fi
	i=$((i + 1))
done
[ "$i" -lt 60 ] || { echo "ratchet-restart: no seed" >&2; exit 1; }

printf '{"op":"msg.send","conversation":"0","key":"%s","text":"restart-before","id":"restart-before-1"}\n' "$friend_key_b" >&4
sleep 1
friend_key_b=$(grep -a '"event":"friend.info"' "$fb" | grep -a '"id":"0"' |
	tail -1 | sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
[ "${#friend_key_b}" -eq 64 ] || { echo "ratchet-restart: stable friend key missing" >&2; exit 1; }
[ -f "$hb/ratchet/sess/d:$friend_key_b-1" ] || {
	echo "ratchet-restart: no persisted session" >&2
	exit 1
}

# Restart only B with the same tox.save, ratchet state, home and state directories.
exec 4>&-
kill "$pb" 2>/dev/null || true
wait "$pb" 2>/dev/null || true
pb=""
rm -f "$holdb"

# Simulate the pre-stable-key layout and require startup migration before reuse.
[ -f "$hb/ratchet/rk/d:$friend_key_b" ] &&
	mv "$hb/ratchet/rk/d:$friend_key_b" "$hb/ratchet/rk/0"
[ -f "$hb/ratchet/ident/d:$friend_key_b" ] &&
	mv "$hb/ratchet/ident/d:$friend_key_b" "$hb/ratchet/ident/0"
[ -f "$hb/ratchet/sess/d:$friend_key_b-1" ] &&
	mv "$hb/ratchet/sess/d:$friend_key_b-1" "$hb/ratchet/sess/0-1"
[ -d "$hb/history/d:$friend_key_b" ] &&
	mv "$hb/history/d:$friend_key_b" "$hb/history/0"

mkfifo "$holdb2"
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >"$fb2" 2>"$fb2.err" <"$holdb2" &
pb=$!
exec 5>"$holdb2"
echo '{"op":"status"}' >&5
i=0
while [ "$i" -lt 60 ]; do
	if grep -a -q '"event":"friend.info".*"online":true' "$fb2"; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$i" -lt 60 ] || { echo "ratchet-restart: friend did not reconnect" >&2; exit 1; }
sleep 1
[ -f "$hb/ratchet/rk/d:$friend_key_b" ] &&
	[ -f "$hb/ratchet/ident/d:$friend_key_b" ] &&
	[ -f "$hb/ratchet/sess/d:$friend_key_b-1" ] &&
	[ -d "$hb/history/d:$friend_key_b" ] || {
	echo "ratchet-restart: legacy direct state was not migrated" >&2
	exit 1
}
[ ! -e "$hb/ratchet/rk/0" ] && [ ! -e "$hb/ratchet/ident/0" ] &&
	[ ! -e "$hb/ratchet/sess/0-1" ] && [ ! -e "$hb/history/0" ] || {
	echo "ratchet-restart: legacy direct state remained active" >&2
	exit 1
}

printf '{"op":"msg.send","conversation":"0","key":"%s","text":"restart-after","id":"restart-after-1"}\n' "$friend_key_a" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a -q 'restart-after' "$fb2"; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$i" -lt 30 ] || { echo "ratchet-restart: no post-restart message" >&2; exit 1; }

echo "ratchet-restart: ok"
