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
	printf '{"op":"msg.send","conversation":"0","text":"secret-ratchet-ping","id":"phase8-ping-a-%s"}\n' "$i" >&3
	printf '{"op":"msg.send","conversation":"0","text":"secret-ratchet-ping","id":"phase8-ping-b-%s"}\n' "$i" >&4
	sleep 1
	if grep -a -q 'secret-ratchet-ping' "$fb"; then
		sent=1
		break
	fi
	i=$((i + 1))
done
[ "$sent" -eq 1 ] || { echo "phase8: no ratchet plaintext event" >&2; tail -30 "$fa" >&2; tail -30 "$fb" >&2; exit 1; }

message_id=$(grep -a '"event":"message"' "$fb" | grep -a '"dir":"in"' | grep -a 'secret-ratchet-ping' | tail -1 | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
[ -n "$message_id" ] || { echo "phase8: no message id" >&2; exit 1; }
printf '{"op":"conversation.read","conversation":"0"}\n' >&4
i=0
while [ "$i" -lt 60 ]; do
	if grep -a -q '"event":"receipt".*"id":"'"$message_id"'".*"state":"read"' "$fa" &&
	   grep -a -q '"event":"conversation.read".*"conversation":"0"' "$fb" &&
	   grep -a -q '"event":"receipt.sent".*"id":"'"$message_id"'".*"state":"read"' "$fb" &&
	   [ ! -s "$sb/read-receipts.tsv" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 60 ] || { echo "phase8: no acknowledged read receipt" >&2; tail -30 "$fa" >&2; tail -30 "$fb" >&2; exit 1; }
printf '{"op":"msg.send","conversation":"0","text":"semantic-reply","reply":"%s","id":"phase8-reply-1"}\n' "$message_id" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a -q '"reply":"'"$message_id"'"' "$fb" && grep -a -q 'semantic-reply' "$fb"; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 30 ] || { echo "phase8: no semantic reply" >&2; exit 1; }
grep -a '"event":"message"' "$fa" | grep -a 'semantic-reply' | grep -a -q '"request":"phase8-reply-1"' || {
	echo "phase8: outgoing message request correlation missing" >&2
	exit 1
}
reply_id=$(grep -a '"event":"message"' "$fa" | grep -a 'semantic-reply' | tail -1 | sed -n 's/.*"id":"\([^" ]*\)".*/\1/p')
[ -n "$reply_id" ] || { echo "phase8: no reply id" >&2; exit 1; }
printf '{"op":"message.edit","conversation":"0","id":"%s","text":"semantic-edited"}\n' "$reply_id" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a -q '"event":"message.updated"' "$fb" && grep -a -q 'semantic-edited' "$fb"; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 30 ] || { echo "phase8: no message edit" >&2; exit 1; }
printf '{"op":"message.delete","conversation":"0","id":"%s"}\n' "$reply_id" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a -q '"event":"message.updated"' "$fa" && grep -a -q '"deleted":true' "$fa"; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 30 ] || { echo "phase8: no message delete" >&2; exit 1; }

if grep -a '"message"' "$fa" "$fb" | grep -E -q 'OQR1|OQB1'; then
	echo "phase8: ciphertext leaked into message event" >&2
	exit 1
fi

# Parallel identical texts must remain correlated when one request fails and the other succeeds.
printf '{"op":"msg.send","conversation":"999999","text":"parallel-identical","reply":"missing","id":"phase8-parallel-fail"}\n' >&3
printf '{"op":"msg.send","conversation":"0","text":"parallel-identical","reply":"%s","id":"phase8-parallel-ok"}\n' "$message_id" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a '"event":"message.failed"' "$fa" | grep -a -q '"request":"phase8-parallel-fail"' &&
	   grep -a '"event":"message"' "$fa" | grep -a '"request":"phase8-parallel-ok"' |
	   grep -a -q '"reply":"'"$message_id"'"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 30 ] || { echo "phase8: parallel message correlation missing" >&2; exit 1; }

# Transport success followed by a local history failure must not invite a duplicate resend.
friend_key_a=$(grep -a '"event":"friend.info"' "$fa" | grep -a '"id":"0"' |
	tail -1 | sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
friend_key_b=$(grep -a '"event":"friend.info"' "$fb" | grep -a '"id":"0"' |
	tail -1 | sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
[ "${#friend_key_a}" -eq 64 ] && [ "${#friend_key_b}" -eq 64 ] || {
	echo "phase8: stable friend keys missing" >&2
	exit 1
}
history_file="$ha/history/d:$friend_key_a/messages.jsonl"
[ -f "$history_file" ] || { echo "phase8: sender history missing" >&2; exit 1; }
chmod 400 "$history_file"
echo '{"op":"msg.send","conversation":"0","text":"delivered-history-failure","id":"phase8-store-fail"}' >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a '"event":"message.failed"' "$fa" | grep -a '"request":"phase8-store-fail"' |
	   grep -a -q '"code":"history_failed","delivered":true' &&
	   grep -a -q 'delivered-history-failure' "$fb"; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
chmod 600 "$history_file"
[ "$i" -lt 30 ] || { echo "phase8: post-delivery history failure semantics missing" >&2; exit 1; }

sent2=0
i=0
while [ "$i" -lt 30 ]; do
	printf '{"op":"msg.send","conversation":"0","text":"secret-ratchet-pong","id":"phase8-pong-%s"}\n' "$i" >&3
	sleep 1
	if grep -a -q 'secret-ratchet-pong' "$fb"; then
		sent2=1
		break
	fi
	i=$((i + 1))
done
[ "$sent2" -eq 1 ] || { echo "phase8: second ratchet message missing" >&2; exit 1; }
offline_id=$(grep -a '"event":"message"' "$fb" | grep -a '"dir":"in"' |
	grep -a 'secret-ratchet-pong' | tail -1 | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
[ -n "$offline_id" ] || { echo "phase8: offline receipt id missing" >&2; exit 1; }

# Reading while the author is offline must survive a recipient-helper restart.
exec 3>&-
kill "$pa" 2>/dev/null || true
wait "$pa" 2>/dev/null || true
pa=""
printf '{"op":"conversation.read","conversation":"0"}\n' >&4
i=0
while [ "$i" -lt 30 ]; do
	if grep -a -q "d:$friend_key_b[[:space:]]$offline_id" "$sb/read-receipts.tsv" 2>/dev/null; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 30 ] || { echo "phase8: offline read was not persisted" >&2; exit 1; }
# Simulate the legacy numeric receipt namespace; the durable friend binding must migrate it.
sed -i "s/^d:$friend_key_b[[:space:]]/0\t/" "$sb/read-receipts.tsv"
awk -F '\t' -v OFS='\t' -v stable="d:$friend_key_b" '$1 == "0" { $1 = stable; print }' \
	"$sb/read-receipts.tsv" >>"$sb/read-receipts.tsv.duplicate"
cat "$sb/read-receipts.tsv.duplicate" >>"$sb/read-receipts.tsv"
rm -f "$sb/read-receipts.tsv.duplicate"
grep -a -q "0[[:space:]]$offline_id" "$sb/read-receipts.tsv" &&
grep -a -q "d:$friend_key_b[[:space:]]$offline_id" "$sb/read-receipts.tsv" || {
	echo "phase8: legacy receipt migration fixture failed" >&2
	exit 1
}
exec 4>&-
kill "$pb" 2>/dev/null || true
wait "$pb" 2>/dev/null || true
pb=""
rm -f "$holda" "$holdb"
mkfifo "$holda" "$holdb"
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >>"$fb" 2>>"$fb.err" <"$holdb" &
pb=$!
exec 4>"$holdb"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >>"$fa" 2>>"$fa.err" <"$holda" &
pa=$!
exec 3>"$holda"
i=0
while [ "$i" -lt 90 ]; do
	if [ ! -s "$sb/read-receipts.tsv" ] &&
	   grep -a -q '"id":"'"$offline_id"'".*"receipt":"read"' "$history_file"; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$i" -lt 90 ] || { echo "phase8: persisted read receipt was not acknowledged" >&2; tail -40 "$fa" >&2; tail -40 "$fb" >&2; exit 1; }

if ! grep -a '"message"' "$fa" "$fb" | grep -q '"dir":"out"'; then
	echo "phase8: no confirmed outgoing message event" >&2
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
