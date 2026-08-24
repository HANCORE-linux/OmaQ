#!/bin/sh
# Two identities, two homes: invite + one text. Requires a HAVE_TOX helper.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "two-homes: no helper" >&2; exit 1; }

ha=$(mktemp -d /tmp/omaq-a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-bs-XXXXXX)
fa=$(mktemp /tmp/omaq-oa-XXXXXX)
fb=$(mktemp /tmp/omaq-ob-XXXXXX)
holda=$(mktemp -u /tmp/omaq-fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-fb-XXXXXX)
pa=""
pb=""
cleanup() {
	exec 3>&- 4>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$holda" "$holdb"
}
trap cleanup EXIT

case "$ha" in
"$HOME/.local/share/omaq"*) echo "two-homes: refused real home" >&2; exit 1 ;;
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
if ! grep -q '"addr"' "$fa"; then
	echo "two-homes: helper has no tox. Install: omarchy pkg add toxcore && make clean helper" >&2
	exit 1
fi

echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url=$(grep '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
if [ -z "$url" ]; then
	echo "two-homes: no invite url" >&2
	exit 1
fi

printf '{"op":"invite.redeem","payload":"%s"}\n' "$url" >&4

ok=0
i=0
while [ "$i" -lt 90 ]; do
	if grep -q '"request"' "$fa"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 1
done
if [ "$ok" -ne 1 ]; then
	echo "two-homes: no friend request (timeout)" >&2
	exit 1
fi
echo '{"op":"contact.decide","id":"x","accept":true}' >&3

sent=0
i=0
while [ "$i" -lt 60 ]; do
	printf '{"op":"msg.send","conversation":"0","text":"ping","id":"two-homes-ping-%s"}\n' "$i" >&3
	sleep 1
	if grep -q '"message"' "$fb"; then
		sent=1
		break
	fi
	i=$((i + 1))
done
if [ "$sent" -ne 1 ]; then
	echo "two-homes: no message received" >&2
	exit 1
fi

set +e
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" --hold
rc=$?
set -e
if [ "$rc" -ne 2 ]; then
	echo "two-homes: expected lock exit 2, got $rc" >&2
	exit 1
fi

rss=$(ps -o rss= -p "$pa" | tr -d ' ')
echo "two-homes: ok rss_a_kb=$rss"
echo "$rss" >"$root/.rss-idle-kb" || true
exit 0
