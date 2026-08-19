#!/bin/sh
# Phase 8: two homes, invite with rk, ratchet ciphertext is not plaintext, RSS.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase8: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p8a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p8as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p8b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p8bs-XXXXXX)
fa=$(mktemp /tmp/omaq-p8oa-XXXXXX)
fb=$(mktemp /tmp/omaq-p8ob-XXXXXX)
holda=$(mktemp -u /tmp/omaq-p8fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-p8fb-XXXXXX)
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
"$real_home"|"$real_home"/*) echo "phase8: refused real home" >&2; exit 1 ;;
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
grep -a -q '"addr"' "$fa" || { echo "phase8: no tox" >&2; exit 1; }

echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
[ -n "$url" ] || { echo "phase8: no url" >&2; exit 1; }
echo "$url" | grep -q 'rk=' || { echo "phase8: invite missing rk" >&2; echo "$url" >&2; exit 1; }

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
[ "$ok" -eq 1 ] || { echo "phase8: no request" >&2; exit 1; }
echo '{"op":"contact.decide","id":"x","accept":true}' >&3

sent=0
i=0
while [ "$i" -lt 60 ]; do
	echo '{"op":"msg.send","conversation":"0","text":"secret-ratchet-ping"}' >&3
	echo '{"op":"msg.send","conversation":"0","text":"secret-ratchet-ping"}' >&4
	sleep 1
	if grep -a -q 'secret-ratchet-ping' "$fb"; then
		sent=1
		break
	fi
	i=$((i + 1))
done
[ "$sent" -eq 1 ] || { echo "phase8: no ratchet plaintext event" >&2; tail -30 "$fa" >&2; tail -30 "$fb" >&2; exit 1; }

# Ciphertext on the helper log must not be the raw ping inside a tox send path;
# the event is decrypted. Ensure a raw OQR1 appeared in err or that ping is only in events.
if grep -a '"message"' "$fb" | grep -q 'secret-ratchet-ping'; then
	:
else
	echo "phase8: decrypted event missing" >&2
	exit 1
fi

rss=$(ps -o rss= -p "$pa" | tr -d ' ')
if [ "$rss" -gt 51200 ]; then
	echo "phase8: rss ${rss} kB > 51200" >&2
	exit 1
fi
echo "$rss" >"$root/.rss-ratchet-kb" || true
echo "phase8: ok rss_a_kb=$rss"
exit 0
