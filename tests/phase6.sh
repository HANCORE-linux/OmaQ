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
pa=""
pb=""
cleanup() {
	exec 3>&- 4>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$src" "$holda" "$holdb" \
		"$fa.err" "$fb.err"
}
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase6: refused real home" >&2; exit 1 ;;
esac

printf 'omaq-file-probe\n' >"$src"

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
	echo '{"op":"msg.send","conversation":"0","text":"ping"}' >&3
	sleep 1
	if grep -a -q '"message"' "$fb"; then
		sent=1
		break
	fi
	i=$((i + 1))
done
[ "$sent" -eq 1 ] || { echo "phase6: not connected" >&2; exit 1; }

printf '{"op":"file.send","conversation":"0","path":"%s"}\n' "$src" >&3
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

got=$(find "$hb/files" -type f 2>/dev/null | head -1)
[ -n "$got" ] || { echo "phase6: no dest file" >&2; exit 1; }
grep -a -q 'omaq-file-probe' "$got" || { echo "phase6: dest mismatch" >&2; exit 1; }

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

echo '{"op":"call.answer","conversation":"0"}' >&4
sleep 0.4
peak_a=$(ps -o rss= -p "$pa" | tr -d ' ')
peak_b=$(ps -o rss= -p "$pb" | tr -d ' ')
peak=$peak_a
if [ "$peak_b" -gt "$peak" ]; then
	peak=$peak_b
fi
echo '{"op":"call.stop","conversation":"0"}' >&3
sleep 0.3
echo '{"op":"call.stop","conversation":"0"}' >&4

if [ "$peak" -gt 40960 ]; then
	echo "phase6: call peak rss ${peak} kB > 40960" >&2
	exit 1
fi

echo "$peak" >"$root/.rss-call-kb" || true
echo "phase6: ok rss_call_kb=$peak dest=$got"
exit 0
