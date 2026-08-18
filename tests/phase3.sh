#!/bin/sh
# Phase 3: 1:1 then private group invite, one text, dissolve. One helper.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase3: no helper" >&2; exit 1; }
test -f "$root/docs/stages/03-toxcore.md" || { echo "phase3: missing 03-toxcore.md" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p3a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p3as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p3b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p3bs-XXXXXX)
fa=$(mktemp /tmp/omaq-p3oa-XXXXXX)
fb=$(mktemp /tmp/omaq-p3ob-XXXXXX)
holda=$(mktemp -u /tmp/omaq-p3fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-p3fb-XXXXXX)
pa=""
pb=""
cleanup() {
	exec 3>&- 4>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$holda" "$holdb" "$fa.err" "$fb.err"
}
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase3: refused real home" >&2; exit 1 ;;
esac

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
if ! grep -a -q '"addr"' "$fa"; then
	echo "phase3: no tox" >&2
	exit 1
fi

echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
[ -n "$url" ] || { echo "phase3: no 1:1 url" >&2; exit 1; }
req_before=$(grep -a -c '"request"' "$fa" 2>/dev/null || true)
req_before=${req_before:-0}
printf '{"op":"invite.redeem","payload":"%s"}\n' "$url" >&4
ok=0
i=0
while [ "$i" -lt 90 ]; do
	req_now=$(grep -a -c '"request"' "$fa" 2>/dev/null || true)
	req_now=${req_now:-0}
	if [ "$req_now" -gt "$req_before" ]; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$ok" -eq 1 ] || { echo "phase3: no 1:1 request" >&2; exit 1; }
echo '{"op":"contact.decide","id":"x","accept":true}' >&3
sleep 1

echo '{"op":"group.create","title":"room"}' >&3
i=0
gid=""
while [ "$i" -lt 40 ]; do
	gid=$(grep -a '"group.changed"' "$fa" | grep -a create | tail -1 | sed -n 's/.*"group":"\([^"]*\)".*/\1/p')
	if [ -n "$gid" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ -n "$gid" ] || { echo "phase3: no group id" >&2; exit 1; }
sleep 1

greq_before=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
greq_before=${greq_before:-0}
ok=0
try=0
while [ "$try" -lt 3 ] && [ "$ok" -ne 1 ]; do
	printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0"}\n' "$gid" >&3
	i=0
	while [ "$i" -lt 20 ]; do
		greq_now=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
		greq_now=${greq_now:-0}
		if [ "$greq_now" -gt "$greq_before" ]; then
			ok=1
			break
		fi
		i=$((i + 1))
		sleep 1
	done
	try=$((try + 1))
done
if [ "$ok" -ne 1 ]; then
	echo "phase3: no group invite" >&2
	echo "--- A ---" >&2
	tail -30 "$fa" >&2
	echo "--- B ---" >&2
	tail -30 "$fb" >&2
	exit 1
fi
echo '{"op":"contact.decide","accept":true}' >&4
sleep 1

echo "{\"op\":\"msg.send\",\"conversation\":\"$gid\",\"text\":\"hi\"}" >&3
ok=0
i=0
while [ "$i" -lt 60 ]; do
	if grep -a -q '"hi"' "$fb"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$ok" -eq 1 ] || { echo "phase3: no group message" >&2; exit 1; }

peer=$(grep -a '"action":"join"' "$fa" | tail -1 | sed -n 's/.*"peer":"\([^"]*\)".*/\1/p')
if [ -n "$peer" ]; then
	printf '{"op":"group.member.setRole","group":"%s","member":"%s","role":"admin"}\n' "$gid" "$peer" >&3
	sleep 0.5
fi

echo "{\"op\":\"group.dissolve\",\"group\":\"$gid\"}" >&3
i=0
ok=0
while [ "$i" -lt 20 ]; do
	if grep -a -q '"action":"dissolve"' "$fa"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$ok" -eq 1 ] || { echo "phase3: dissolve missing" >&2; exit 1; }

set +e
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" --hold
rc=$?
set -e
[ "$rc" -eq 2 ] || { echo "phase3: expected lock exit 2, got $rc" >&2; exit 1; }

if ! kill -0 "$pa" 2>/dev/null; then
	echo "phase3: helper A died" >&2
	exit 1
fi

echo "phase3: ok"
exit 0
