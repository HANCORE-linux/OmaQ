#!/bin/sh
# Phase 2: expire, revoke, nospam voids invites, safety match, QR PNG.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase2: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p2a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p2as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p2b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p2bs-XXXXXX)
fa=$(mktemp /tmp/omaq-p2oa-XXXXXX)
fb=$(mktemp /tmp/omaq-p2ob-XXXXXX)
holda=$(mktemp -u /tmp/omaq-p2fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-p2fb-XXXXXX)
png=$(mktemp /tmp/omaq-p2-XXXXXX.png)
pa=""
pb=""
cleanup() {
	exec 3>&- 4>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$fa" "$fb" "$holda" "$holdb" "$png" \
		"$fa.err" "$fb.err"
}
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase2: refused real home" >&2; exit 1 ;;
esac

# --- QR from a gold invite URL (no network) ---
url_gold="omaq://invite/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789ab?i=abc1&e=2000000000&k=direct"
mkfifo "$holda"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$fa" 2>"$fa.err" <"$holda" &
pa=$!
exec 3>"$holda"
sleep 0.3
printf '{"op":"invite.qr","payload":"%s","path":"%s"}\n' "$url_gold" "$png" >&3
i=0
qr_ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"qr"' "$fa" 2>/dev/null; then
		qr_ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
if [ "$qr_ok" -ne 1 ] || [ ! -f "$png" ]; then
	echo "phase2: qr write failed" >&2
	exit 1
fi
got=$(zbarimg --raw -q "$png" 2>/dev/null | tr -d '\r')
if [ "$got" != "$url_gold" ]; then
	echo "phase2: zbar mismatch" >&2
	exit 1
fi

# --- expire: redeem a past-e URL ---
printf '%s\n' '{"op":"invite.redeem","payload":"omaq://invite/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789ab?i=abc1&e=1&k=direct"}' >&3
i=0
exp_ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q 'invite_expired' "$fa" 2>/dev/null; then
		exp_ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
if [ "$exp_ok" -ne 1 ]; then
	echo "phase2: expire redeem did not fail" >&2
	exit 1
fi

# --- two homes ---
mkfifo "$holdb"
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >"$fb" 2>"$fb.err" <"$holdb" &
pb=$!
exec 4>"$holdb"
sleep 0.4

echo '{"op":"status"}' >&3
sleep 0.2
if ! grep -a -q '"addr"' "$fa"; then
	echo "phase2: helper has no tox" >&2
	exit 1
fi

# Accept first while B is not yet a friend, then compare safety codes.
before=$(wc -l <"$fa")
echo '{"op":"invite.create","ttlSec":86400,"kind":"bogus","request":"invite-phase2-bogus"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$fa" | grep -a '"code":"unsupported"' |
	grep -a -q '"request":"invite-phase2-bogus"' || {
	echo "phase2: malformed invite kind was accepted or uncorrelated" >&2
	exit 1
}
if tail -n +"$((before + 1))" "$fa" | grep -a -q '"event":"invite","url":"omaq://'; then
	echo "phase2: malformed invite kind created a link" >&2
	exit 1
fi
echo '{"op":"invite.create","ttlSec":2147483647,"kind":"direct","request":"invite-phase2-first"}' >&3
sleep 0.3
url=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
invite_event=$(grep -a '"event":"invite"' "$fa" | tail -1)
printf '%s\n' "$invite_event" |
	grep -E -a -q '"expires":[1-9][0-9]*.*"op":"create","request":"invite-phase2-first"' || {
	echo "phase2: invite expiry/request correlation missing" >&2
	exit 1
}
invite_expiry=$(printf '%s\n' "$invite_event" |
	sed -n 's/.*"expires":\([0-9]*\).*/\1/p')
invite_remaining=$((invite_expiry - $(date +%s)))
[ "$invite_remaining" -ge 86395 ] && [ "$invite_remaining" -le 86400 ] || {
	echo "phase2: helper did not enforce the 24-hour invite lifetime" >&2
	exit 1
}
if [ -z "$url" ]; then
	echo "phase2: no invite url" >&2
	exit 1
fi
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
if [ "$ok" -ne 1 ]; then
	echo "phase2: no friend request" >&2
	exit 1
fi
echo '{"op":"contact.decide","id":"x","accept":true}' >&3
sleep 1
grep -a '"event":"invite"' "$fa" | tail -1 |
	grep -a -q '"url":"","expires":0,"op":"clear"' || {
	echo "phase2: accepted invite state was not cleared" >&2
	exit 1
}
key_a=$(grep -a '"event":"friend.info"' "$fa" | grep -a '"id":"0"' | tail -1 |
	sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
key_b=$(grep -a '"event":"friend.info"' "$fb" | grep -a '"id":"0"' | tail -1 |
	sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
[ "${#key_a}" -eq 64 ] && [ "${#key_b}" -eq 64 ] || {
	echo "phase2: stable friend keys missing" >&2
	exit 1
}
before=$(wc -l <"$fa")
printf '%s\n' '{"op":"safety.get","conversation":"0","key":"0000000000000000000000000000000000000000000000000000000000000000","id":"safety-stale-key"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$fa" | grep -a '"code":"forbidden"' |
	grep -a -q '"request":"safety-stale-key"' || {
	echo "phase2: stale safety key was accepted" >&2
	exit 1
}
printf '{"op":"safety.get","conversation":"0","key":"%s","id":"safety-a"}\n' "$key_a" >&3
printf '{"op":"safety.get","conversation":"0","key":"%s","id":"safety-b"}\n' "$key_b" >&4
i=0
ca=""
cb=""
while [ "$i" -lt 20 ]; do
	ca=$(grep -a '"safety"' "$fa" | tail -1 | sed -n 's/.*"code":"\([^"]*\)".*/\1/p')
	cb=$(grep -a '"safety"' "$fb" | tail -1 | sed -n 's/.*"code":"\([^"]*\)".*/\1/p')
	if [ -n "$ca" ] && [ -n "$cb" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.3
done
if [ -z "$ca" ] || [ "$ca" != "$cb" ]; then
	echo "phase2: safety codes differ" >&2
	exit 1
fi
grep -a '"event":"safety"' "$fa" | tail -1 | grep -a -q '"request":"safety-a"' &&
grep -a '"event":"safety"' "$fb" | tail -1 | grep -a -q '"request":"safety-b"' || {
	echo "phase2: safety request correlation missing" >&2
	exit 1
}

forbidden_before=$(grep -a -c '"event":"error","code":"forbidden"' "$fa" || true)
echo '{"op":"contact.remove","id":"0","key":"0000000000000000000000000000000000000000000000000000000000000000"}' >&3
i=0
while [ "$i" -lt 20 ]; do
	forbidden_after=$(grep -a -c '"event":"error","code":"forbidden"' "$fa" || true)
	[ "$forbidden_after" -gt "$forbidden_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 20 ] || { echo "phase2: stale friend key was not rejected" >&2; exit 1; }
printf '{"op":"contact.remove","id":"0","key":"%s"}\n' "$key_a" >&3
printf '{"op":"contact.remove","id":"0","key":"%s"}\n' "$key_b" >&4
sleep 0.4
if ! kill -0 "$pa" 2>/dev/null; then
	echo "phase2: helper A died" >&2
	exit 1
fi

# Revoke: B may knock; A must not emit a contact request.
req_before=$(grep -a '"event":"request"' "$fa" | grep -a -c '"kind":"direct"' || true)
req_before=${req_before:-0}
echo '{"op":"invite.create","ttlSec":86400,"kind":"direct","request":"invite-phase2-revoke-source"}' >&3
sleep 0.3
url2=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
echo '{"op":"invite.revoke","request":"invite-phase2-revoke"}' >&3
sleep 0.2
grep -a '"event":"invite"' "$fa" | tail -1 |
	grep -a -q '"url":"","expires":0,"op":"revoke","request":"invite-phase2-revoke"' || {
	echo "phase2: invite revoke was not correlated" >&2
	exit 1
}
printf '{"op":"invite.redeem","payload":"%s"}\n' "$url2" >&4
i=0
while [ "$i" -lt 8 ]; do
	req_now=$(grep -a '"event":"request"' "$fa" | grep -a -c '"kind":"direct"' || true)
	req_now=${req_now:-0}
	if [ "$req_now" -gt "$req_before" ]; then
		echo "phase2: revoked invite still requested" >&2
		exit 1
	fi
	i=$((i + 1))
	sleep 0.5
done
printf '{"op":"contact.remove","id":"0","key":"%s"}\n' "$key_b" >&4
sleep 0.3

# Nospam changes the address and voids the live token.
echo '{"op":"status"}' >&3
sleep 0.2
addr1=$(grep -a '"addr"' "$fa" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
echo '{"op":"invite.create","ttlSec":86400,"kind":"direct"}' >&3
sleep 0.3
url3=$(grep -a '"url"' "$fa" | tail -1 | sed -n 's/.*"url":"\([^"]*\)".*/\1/p')
echo '{"op":"nospam.rotate"}' >&3
sleep 0.3
addr2=$(grep -a '"addr"' "$fa" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
if [ -z "$addr1" ] || [ -z "$addr2" ] || [ "$addr1" = "$addr2" ]; then
	echo "phase2: nospam did not change addr" >&2
	exit 1
fi
req_before=$(grep -a -c '"request"' "$fa" 2>/dev/null || true)
req_before=${req_before:-0}
printf '{"op":"invite.redeem","payload":"%s"}\n' "$url3" >&4
i=0
while [ "$i" -lt 8 ]; do
	req_now=$(grep -a -c '"request"' "$fa" 2>/dev/null || true)
	req_now=${req_now:-0}
	if [ "$req_now" -gt "$req_before" ]; then
		echo "phase2: nospam-voided invite still requested" >&2
		exit 1
	fi
	i=$((i + 1))
	sleep 0.5
done

rss=$(ps -o rss= -p "$pa" | tr -d ' ')
base=6648
if [ -f "$root/.rss-idle-kb" ]; then
	base=$(tr -d ' ' <"$root/.rss-idle-kb")
fi
max=$((base * 3 / 2))
if [ -n "$rss" ] && [ "$rss" -gt "$max" ]; then
	echo "phase2: rss ${rss}kB > 1.5x baseline ${base}kB" >&2
	exit 1
fi

echo "phase2: ok rss_a_kb=$rss"
exit 0
