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
hc=$(mktemp -d /tmp/omaq-p3c-XXXXXX)
sc=$(mktemp -d /tmp/omaq-p3cs-XXXXXX)
fc=$(mktemp /tmp/omaq-p3oc-XXXXXX)
hd=$(mktemp -d /tmp/omaq-p3d-XXXXXX)
sd=$(mktemp -d /tmp/omaq-p3ds-XXXXXX)
fd=$(mktemp /tmp/omaq-p3od-XXXXXX)
he=$(mktemp -d /tmp/omaq-p3e-XXXXXX)
se=$(mktemp -d /tmp/omaq-p3es-XXXXXX)
fe=$(mktemp /tmp/omaq-p3oe-XXXXXX)
busy_export=$(mktemp -u /tmp/omaq-p3-busy-export-XXXXXX.save)
holda=$(mktemp -u /tmp/omaq-p3fa-XXXXXX)
holdb=$(mktemp -u /tmp/omaq-p3fb-XXXXXX)
holdc=$(mktemp -u /tmp/omaq-p3fc-XXXXXX)
holdd=$(mktemp -u /tmp/omaq-p3fd-XXXXXX)
holde=$(mktemp -u /tmp/omaq-p3fe-XXXXXX)
pa=""
pb=""
pc=""
pd=""
pe=""
cleanup() {
	exec 3>&- 4>&- 5>&- 6>&- 7>&- 2>/dev/null || true
	[ -n "${pa:-}" ] && kill "$pa" 2>/dev/null || true
	[ -n "${pb:-}" ] && kill "$pb" 2>/dev/null || true
	[ -n "${pc:-}" ] && kill "$pc" 2>/dev/null || true
	[ -n "${pd:-}" ] && kill "$pd" 2>/dev/null || true
	[ -n "${pe:-}" ] && kill "$pe" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$hc" "$sc" "$hd" "$sd" "$he" "$se" \
		"$fa" "$fb" "$fc" "$fd" "$fe" "$busy_export" \
		"$holda" "$holdb" "$holdc" "$holdd" "$holde" \
		"$fa.err" "$fb.err" "$fc.err" "$fd.err" "$fe.err"
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
i=0
while [ "$i" -lt 90 ]; do
	if grep -a -q '"online":true' "$fa" && grep -a -q '"online":true' "$fb"; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
[ "$i" -lt 90 ] || { echo "phase3: direct contacts did not come online" >&2; exit 1; }
friend_key=$(grep -a '"event":"friend.info"' "$fa" | grep -a '"id":"0"' | tail -1 |
	sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
[ "${#friend_key}" -eq 64 ] || { echo "phase3: stable friend key missing" >&2; exit 1; }

# Group numbers are process-local. Give B a pre-existing g0 so A's room must
# still authorize correctly when B allocates a different local number.
echo '{"op":"group.create","title":"preexisting"}' >&4
i=0
while [ "$i" -lt 40 ]; do
	if grep -a '"event":"group.info"' "$fb" | grep -a -q '"title":"preexisting"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase3: recipient pre-existing group missing" >&2; exit 1; }
pre_gid=$(grep -a '"event":"group.info"' "$fb" | grep -a '"title":"preexisting"' |
	tail -1 | sed -n 's/.*"group":"\([^"]*\)".*/\1/p')
[ -n "$pre_gid" ] || { echo "phase3: pre-existing stable id missing" >&2; exit 1; }

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
group_url_before=$(grep -a '"event":"invite"' "$fa" | grep -a -c 'k=group' || true)
group_url_before=${group_url_before:-0}
stale_invites_before=$(grep -a '"event":"group.invite.failed"' "$fa" |
	grep -a -c '"code":"forbidden"' || true)
stale_invites_before=${stale_invites_before:-0}
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"0000000000000000000000000000000000000000000000000000000000000000","request":"gi-phase3-stale-1"}\n' "$gid" >&3
i=0
while [ "$i" -lt 30 ]; do
	stale_invites_after=$(grep -a '"event":"group.invite.failed"' "$fa" |
		grep -a -c '"code":"forbidden"' || true)
	stale_invites_after=${stale_invites_after:-0}
	[ "$stale_invites_after" -gt "$stale_invites_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: stale invite friend key was not rejected" >&2; exit 1; }
busy_invites_before=$(grep -a '"event":"group.invite.failed"' "$fa" |
	grep -a -c '"code":"busy"' || true)
busy_invites_before=${busy_invites_before:-0}
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-first-1"}\n' "$gid" "$friend_key" >&3
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-second-1"}\n' "$gid" "$friend_key" >&3
i=0
while [ "$i" -lt 30 ]; do
	busy_invites_after=$(grep -a '"event":"group.invite.failed"' "$fa" |
		grep -a -c '"code":"busy"' || true)
	busy_invites_after=${busy_invites_after:-0}
	if [ "$busy_invites_after" -gt "$busy_invites_before" ] &&
	   grep -a '"event":"group.invite.failed"' "$fa" | grep -a '"code":"busy"' |
	   tail -1 | grep -a -q '"group":"'"$gid"'","friend":"0","request":"gi-phase3-second-1"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: parallel group invite busy result missing" >&2; exit 1; }
# The helper bootstraps the direct Ratchet when necessary, sends the token only
# inside that encrypted session, then releases the native invite for approval.
ok=0
i=0
while [ "$i" -lt 150 ]; do
	greq_now=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
	greq_now=${greq_now:-0}
	if [ "$greq_now" -gt "$greq_before" ]; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
group_url_after=$(grep -a '"event":"invite"' "$fa" | grep -a -c 'k=group' || true)
group_url_after=${group_url_after:-0}
[ "$group_url_after" -eq "$group_url_before" ] || {
	echo "phase3: targeted group token leaked over IPC" >&2
	exit 1
}
if grep -a -q 'OQGI1\|k=group' "$fb" ||
   grep -R -a -q 'OQGI1\|k=group' "$hb/history" 2>/dev/null; then
	echo "phase3: encrypted group control leaked into direct chat" >&2
	exit 1
fi
if [ "$ok" -ne 1 ]; then
	echo "phase3: no group invite" >&2
	echo "--- A ---" >&2
	tail -30 "$fa" >&2
	echo "--- B ---" >&2
	tail -30 "$fb" >&2
	exit 1
fi
before=$(wc -l <"$fa")
printf '{"op":"identity.export","path":"%s","id":"phase3-export-binding-debt"}\n' \
	"$busy_export" >&3
i=0
while [ "$i" -lt 30 ]; do
	if tail -n +"$((before + 1))" "$fa" | grep -a '"code":"busy"' |
	   grep -a -q '"request":"phase3-export-binding-debt"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] && [ ! -e "$busy_export" ] || {
	echo "phase3: identity export ignored a group-binding debt" >&2
	exit 1
}
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-preaccept-1"}\n' "$gid" "$friend_key" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a '"event":"group.invite.failed"' "$fa" | grep -a '"code":"busy"' |
	   grep -a -q '"request":"gi-phase3-preaccept-1"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: pre-accept duplicate invite was not blocked" >&2; exit 1; }
sleep 0.2
greq_after_duplicate=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
[ "$greq_after_duplicate" -eq "$greq_now" ] || {
	echo "phase3: pre-accept duplicate produced a native invite" >&2
	exit 1
}
echo '{"op":"contact.decide","accept":true}' >&4
i=0
while [ "$i" -lt 90 ]; do
	if grep -a '"event":"group.changed"' "$fa" |
	   grep -a -q '"action":"member.join"'; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
if [ "$i" -ge 90 ]; then
	echo "phase3: accepted member did not join" >&2
	tail -40 "$fa" >&2
	tail -40 "$fb" >&2
	tail -n 20 -- "$fa.err" "$fb.err" >&2
	exit 1
fi
join_notice_count=$(grep -a '"event":"message"' "$fa" | grep -a '"dir":"sys"' |
	grep -a -c 'joined the group\.' || true)
[ "$join_notice_count" -eq 1 ] || {
	echo "phase3: initial member join did not emit exactly one live notice" >&2
	exit 1
}
persisted_join_notice_count=$(grep -a '"dir":"sys"' "$ha/history/$gid/messages.jsonl" |
	grep -a -c 'joined the group\.' || true)
[ "$persisted_join_notice_count" -eq 1 ] || {
	echo "phase3: initial member join did not persist exactly one notice" >&2
	exit 1
}
grep -a '"event":"group.invite.sent"' "$fa" |
	grep -a -q '"request":"gi-phase3-first-1"' || {
	echo "phase3: first invite success was not request-correlated" >&2
	exit 1
}
if grep -a '"event":"group.invite.sent"' "$fa" |
   grep -a -q '"request":"gi-phase3-second-1"'; then
	echo "phase3: busy second invite was reported sent" >&2
	exit 1
fi
i=0
while [ "$i" -lt 50 ]; do
	if grep -a '"event":"group.member"' "$fa" |
	   grep -a -q '"friendKey":"'"$friend_key"'"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 50 ] || { echo "phase3: stable friend/group member binding missing" >&2; exit 1; }
member_key=$(grep -a '"event":"group.member"' "$fa" |
	grep -a '"friendKey":"'"$friend_key"'"' | tail -1 |
	sed -n 's/.*"key":"\([0-9a-f]*\)".*/\1/p')
[ ${#member_key} -eq 64 ] || { echo "phase3: bound member key missing" >&2; exit 1; }
if grep -E -q "^E[[:space:]]${gid}[[:space:]][0-9a-f]{16}[[:space:]]${friend_key}[[:space:]]${member_key}[[:space:]]" \
   "$sa/group-bind.pending"; then
	echo "phase3: completed inviter binding debt was not retired" >&2
	exit 1
fi
i=0
while [ "$i" -lt 50 ]; do
	if [ -f "$sb/group-bind.pending" ] &&
	   ! grep -q '^P[[:space:]]' "$sb/group-bind.pending"; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 50 ] || { echo "phase3: binding acknowledgement missing" >&2; exit 1; }

cp -a "$hb/." "$hd/"
printf 'A\t%s\t1111111111111111\t%s\t-\t0\t0\t0\n' "$gid" \
	"0000000000000000000000000000000000000000000000000000000000000000" \
	>"$sd/group-bind.pending"
grep -E -q "${friend_key}[[:space:]][0-9a-f]{64}" "$ha/group-friends.tsv" || {
	echo "phase3: stable friend/group member binding was not persisted" >&2
	exit 1
}
awk -F '\t' 'NF != 3 { exit 1 }' "$ha/groups.tsv" || {
	echo "phase3: legacy-compatible group registry schema changed" >&2
	exit 1
}
cp -a "$ha/." "$he/"
awk -F '\t' -v chat="${gid#g:}" '$1 != chat' "$he/groups.tsv" >"$he/groups.tsv.next"
mv "$he/groups.tsv.next" "$he/groups.tsv"
printf 'R\t%s\n' "$gid" >"$he/group-registry.pending"
chmod 0600 "$he/groups.tsv" "$he/group-registry.pending"
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-duplicate-1"}\n' "$gid" "$friend_key" >&3
i=0
while [ "$i" -lt 30 ]; do
	if grep -a '"event":"group.invite.failed"' "$fa" |
	   grep -a '"code":"already_member"' |
	   grep -a -q '"request":"gi-phase3-duplicate-1"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
if [ "$i" -ge 30 ]; then
	echo "phase3: duplicate member invite was not rejected for friend key $friend_key" >&2
	grep -a '"event":"group.member"' "$fa" | tail -4 >&2 || true
	tail -n 20 -- "$fa.err" "$fb.err" >&2 || true
	exit 1
fi
if grep -a '"event":"group.invite.sent"' "$fa" |
   grep -a -q '"request":"gi-phase3-duplicate-1"'; then
	echo "phase3: duplicate member invite was reported sent" >&2
	exit 1
fi
sent_replays_before=$(grep -a '"event":"group.invite.sent"' "$fa" |
	grep -a -c '"request":"gi-phase3-first-1"' || true)
echo '{"op":"status","id":"phase3-invite-replay"}' >&3
i=0
while [ "$i" -lt 30 ]; do
	sent_replays_after=$(grep -a '"event":"group.invite.sent"' "$fa" |
		grep -a -c '"request":"gi-phase3-first-1"' || true)
	[ "$sent_replays_after" -gt "$sent_replays_before" ] && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: terminal invite result was not replayed" >&2; exit 1; }
grep -a '"event":"group.info"' "$fa" | grep -a '"title":"room"' |
	grep -a -q '"limit":10' || { echo "phase3: group info/limit missing" >&2; exit 1; }
grep -a '"event":"group.member"' "$fa" | grep -a -q '"online":true' || {
	echo "phase3: online group member snapshot missing" >&2
	exit 1
}
member_key=$(grep -a '"event":"group.member"' "$fa" | grep -a '"self":false' |
	tail -1 | sed -n 's/.*"key":"\([^"]*\)".*/\1/p')
[ -n "$member_key" ] || { echo "phase3: stable moderation key missing" >&2; exit 1; }
printf '{"op":"group.member.setRole","group":"%s","member":"%s","role":"admin"}\n' \
	"$gid" "$member_key" >&3
i=0
while [ "$i" -lt 50 ]; do
	if grep -a '"event":"group.member"' "$fa" | grep -a '"key":"'"$member_key"'"' |
	   grep -a -q '"role":"admin"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
if [ "$i" -ge 50 ]; then
	echo "phase3: stable-key role change missing" >&2
	tail -40 "$fa" >&2
	echo "--- B ---" >&2
	tail -40 "$fb" >&2
	echo "--- ERRORS ---" >&2
	tail -n 20 -- "$fa.err" "$fb.err" >&2
	exit 1
fi

# Tox NGC private groups cannot be restored from a chat ID alone. A cold
# helper must visibly prune a registry entry missing from Tox saved state,
# never fabricate a disconnected phantom group.
cp -a "$ha/." "$hc/"
rm -f "$hc/tox.save"
printf '%s\t1\n' "$gid" >"$sc/unread.tsv"
mkdir -p "$hc/history/g7"
printf '%s\n' '{"id":"legacy","text":"archived"}' >"$hc/history/g7/messages.jsonl"
mkfifo "$holdc"
OMAQ_HOME="$hc" OMAQ_STATE="$sc" "$bin" >"$fc" 2>"$fc.err" <"$holdc" &
pc=$!
exec 5>"$holdc"
sleep 0.5
echo '{"op":"status"}' >&5
i=0
while [ "$i" -lt 50 ]; do
	if grep -a -q '"code":"group_orphaned"' "$fc" &&
	   grep -a -q '"code":"legacy_group_state_archived"' "$fc"; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
if [ "$i" -ge 50 ] ||
   grep -a '"event":"group.info"' "$fc" | grep -a -q '"group":"'"$gid"'"' ||
   grep -a '"event":"snapshot"' "$fc" | tail -1 | grep -a -vq '"unread":0' ||
   grep -a -q "^$gid" "$sc/unread.tsv" 2>/dev/null; then
	echo "phase3: orphaned private group was not pruned" >&2
	tail -30 "$fc" >&2
	tail -n 20 -- "$fc.err" >&2
	exit 1
fi
exec 5>&-
kill "$pc" 2>/dev/null || true
wait "$pc" 2>/dev/null || true
pc=""

ok=0
i=0
while [ "$i" -lt 60 ]; do
	printf '{"op":"msg.send","conversation":"%s","text":"hi","id":"phase3-group-hi-%s"}\n' "$gid" "$i" >&3
	sleep 1
	if grep -a -q '"hi"' "$fb"; then
		ok=1
		break
	fi
	i=$((i + 1))
done
[ "$ok" -eq 1 ] || { echo "phase3: no group message" >&2; exit 1; }
grep -a '"event":"message"' "$fa" | grep -a -q '"request":"phase3-group-hi-' || {
	echo "phase3: group message request correlation missing" >&2
	exit 1
}
grep -a '"event":"message"' "$fb" | grep -a '"text":"hi"' |
	grep -E -a -q '"sender":"[0-9a-f]{64}"' || {
	echo "phase3: stable group sender identity missing" >&2
	exit 1
}
message_id=$(grep -a '"event":"message"' "$fa" | grep -a '"request":"phase3-group-hi-' |
	tail -1 | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
recipient_gid=$(grep -a '"event":"message"' "$fb" | grep -a '"text":"hi"' | tail -1 |
	sed -n 's/.*"conversation":"\([^"]*\)".*/\1/p')
[ -n "$message_id" ] || { echo "phase3: group message id missing" >&2; exit 1; }
[ -n "$recipient_gid" ] || { echo "phase3: recipient local group id missing" >&2; exit 1; }
[ "$recipient_gid" = "$gid" ] || { echo "phase3: stable group id differs across peers" >&2; exit 1; }
printf '{"op":"message.react","conversation":"%s","id":"%s","text":"👍"}\n' "$recipient_gid" "$message_id" >&4
i=0
while [ "$i" -lt 40 ]; do
	if grep -a '"event":"message.reaction"' "$fa" | grep -a '"id":"'"$message_id"'"' |
	   grep -E -a -q '"actor":"[0-9a-f]{64}"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 40 ] || { echo "phase3: group reaction missing" >&2; exit 1; }
printf '{"op":"receipt.send","conversation":"%s","id":"%s","state":"read"}\n' "$recipient_gid" "$message_id" >&4
i=0
while [ "$i" -lt 40 ]; do
	if grep -a '"event":"receipt"' "$fa" | grep -a '"id":"'"$message_id"'"' |
	   grep -a -q '"state":"read"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.2
done
[ "$i" -lt 40 ] || { echo "phase3: group read receipt missing" >&2; exit 1; }
read_events_before=$(grep -a '"event":"receipt"' "$fa" | grep -a '"id":"'"$message_id"'"' |
	grep -a -c '"state":"read"' || true)
i=0
while [ "$i" -lt 35 ]; do
	printf '{"op":"receipt.send","conversation":"%s","id":"%s","state":"read"}\n' "$recipient_gid" "$message_id" >&4
	i=$((i + 1))
done
sleep 1
read_events_after=$(grep -a '"event":"receipt"' "$fa" | grep -a '"id":"'"$message_id"'"' |
	grep -a -c '"state":"read"' || true)
[ "$read_events_after" -eq "$read_events_before" ] || {
	echo "phase3: duplicate group receipt emitted repeated updates" >&2
	exit 1
}

if [ -n "$member_key" ]; then
	echo '{"op":"status","id":"phase3-before-kick"}' >&4
	i=0
	while [ "$i" -lt 30 ]; do
		if grep -a '"event":"snapshot"' "$fb" | grep -a '"request":"phase3-before-kick"' |
		   tail -1 | grep -E -a -q '"unread":[1-9]'; then
			break
		fi
		i=$((i + 1))
		sleep 0.1
	done
	[ "$i" -lt 30 ] || { echo "phase3: pre-kick unread fixture missing" >&2; exit 1; }
	printf '{"op":"group.member.setRole","group":"%s","member":"%s","role":"member"}\n' "$gid" "$member_key" >&3
	sleep 0.5
	self_leave_before=$(grep -a '"event":"group.changed"' "$fb" | grep -a '"group":"'"$gid"'"' |
		grep -a -c '"action":"leave"' || true)
	leave_notices_before=$(grep -a '"event":"message"' "$fa" | grep -a '"dir":"sys"' |
		grep -a -c 'left the group\.' || true)
	persisted_leave_notices_before=$(grep -a '"dir":"sys"' "$ha/history/$gid/messages.jsonl" |
		grep -a -c 'left the group\.' || true)
	printf '{"op":"group.member.remove","group":"%s","member":"%s"}\n' "$gid" "$member_key" >&3
	i=0
	while [ "$i" -lt 50 ]; do
		self_leave_after=$(grep -a '"event":"group.changed"' "$fb" | grep -a '"group":"'"$gid"'"' |
			grep -a -c '"action":"leave"' || true)
		if [ "$self_leave_after" -gt "$self_leave_before" ]; then
			break
		fi
		i=$((i + 1))
		sleep 0.2
	done
	[ "$i" -lt 50 ] || { echo "phase3: kicked self group was not removed" >&2; exit 1; }
	leave_notices_after=$(grep -a '"event":"message"' "$fa" | grep -a '"dir":"sys"' |
		grep -a -c 'left the group\.' || true)
	[ "$leave_notices_after" -eq "$((leave_notices_before + 1))" ] || {
		echo "phase3: group kick did not emit exactly one live leave notice" >&2
		exit 1
	}
	persisted_leave_notices_after=$(grep -a '"dir":"sys"' "$ha/history/$gid/messages.jsonl" |
		grep -a -c 'left the group\.' || true)
	[ "$persisted_leave_notices_after" -eq "$((persisted_leave_notices_before + 1))" ] || {
		echo "phase3: group kick did not persist exactly one leave notice" >&2
		exit 1
	}
	echo '{"op":"status","id":"phase3-after-kick"}' >&4
	i=0
	while [ "$i" -lt 30 ]; do
		if grep -a '"event":"snapshot"' "$fb" | grep -a '"request":"phase3-after-kick"' |
		   tail -1 | grep -a -q '"unread":0'; then
			break
		fi
		i=$((i + 1))
		sleep 0.1
	done
	[ "$i" -lt 30 ] || { echo "phase3: kicked group unread was not cleared" >&2; exit 1; }
	i=0
	while [ "$i" -lt 30 ]; do
		if grep -a '"event":"group.info"' "$fa" | grep -a '"group":"'"$gid"'"' |
			tail -1 | grep -a -q '"members":1'; then
			break
		fi
		i=$((i + 1))
		sleep 0.1
	done
	[ "$i" -lt 30 ] || { echo "phase3: kicked member remained in initiator cache" >&2; exit 1; }
	i=0
	while [ "$i" -lt 40 ]; do
		if ! grep -E -q "^E[[:space:]]${gid}[[:space:]][0-9a-f]{16}[[:space:]]${friend_key}[[:space:]]${member_key}[[:space:]]" \
		   "$sa/group-bind.pending"; then
			break
		fi
		i=$((i + 1))
		sleep 0.1
	done
	[ "$i" -lt 40 ] || { echo "phase3: removed member debt was not retried" >&2; exit 1; }

	# A kicked member must be able to receive and accept a fresh targeted invite.
	reinvite_requests_before=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
	reinvite_requests_before=${reinvite_requests_before:-0}
	rejoins_before=$(grep -a '"event":"group.changed"' "$fa" |
		grep -a -c '"action":"member.join"' || true)
	rejoins_before=${rejoins_before:-0}
	printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-reinvite-1"}\n' "$gid" "$friend_key" >&3
	i=0
	while [ "$i" -lt 150 ]; do
		reinvite_requests_after=$(grep -a -c '"kind":"group"' "$fb" 2>/dev/null || true)
		reinvite_requests_after=${reinvite_requests_after:-0}
		if [ "$reinvite_requests_after" -gt "$reinvite_requests_before" ]; then
			break
		fi
		i=$((i + 1))
		sleep 0.2
	done
	if [ "$i" -ge 150 ]; then
		echo "phase3: removed member did not receive a fresh invite" >&2
		tail -40 "$fa" >&2
		tail -40 "$fb" >&2
		exit 1
	fi
	echo '{"op":"contact.decide","accept":true}' >&4
	i=0
	while [ "$i" -lt 90 ]; do
		rejoins_after=$(grep -a '"event":"group.changed"' "$fa" |
			grep -a -c '"action":"member.join"' || true)
		rejoins_after=${rejoins_after:-0}
		if [ "$rejoins_after" -gt "$rejoins_before" ]; then
			break
		fi
		i=$((i + 1))
		sleep 1
	done
	[ "$i" -lt 90 ] || { echo "phase3: removed member did not rejoin" >&2; exit 1; }
	rejoin_notice_count=$(grep -a '"event":"message"' "$fa" |
		grep -a '"dir":"sys"' | grep -a -c 'joined the group\.' || true)
	[ "$rejoin_notice_count" -eq 2 ] || {
		echo "phase3: genuine member rejoin did not emit exactly one new notice" >&2
		exit 1
	}
	persisted_rejoin_notice_count=$(grep -a '"dir":"sys"' "$ha/history/$gid/messages.jsonl" |
		grep -a -c 'joined the group\.' || true)
	[ "$persisted_rejoin_notice_count" -eq 2 ] || {
		echo "phase3: genuine member rejoin did not persist exactly one new notice" >&2
		exit 1
	}
	i=0
	while [ "$i" -lt 40 ]; do
		if grep -a '"event":"group.info"' "$fb" | grep -a '"group":"'"$gid"'"' |
		   tail -1 | grep -a -q '"members":2'; then
			break
		fi
		i=$((i + 1))
		sleep 0.2
	done
	[ "$i" -lt 40 ] || { echo "phase3: rejoined member group projection missing" >&2; exit 1; }
	i=0
	while [ "$i" -lt 60 ]; do
		if grep -a '"event":"group.member"' "$fa" |
		   grep -a '"group":"'"$gid"'"' | grep -a '"self":false' | tail -1 |
		   grep -a -q '"friendKey":"'"$friend_key"'"'; then
			break
		fi
		i=$((i + 1))
		sleep 0.2
	done
	[ "$i" -lt 60 ] || { echo "phase3: rejoined member binding missing" >&2; exit 1; }
fi

# A failed registry pre-commit must not execute the irreversible Tox leave.
registry_errors_before=$(grep -a -c '"code":"group_registry_failed"' "$fb" || true)
chmod 0500 "$hb"
printf '{"op":"group.leave","group":"%s"}\n' "$pre_gid" >&4
i=0
while [ "$i" -lt 30 ]; do
	registry_errors_after=$(grep -a -c '"code":"group_registry_failed"' "$fb" || true)
	if [ "$registry_errors_after" -gt "$registry_errors_before" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
chmod 0700 "$hb"
[ "$i" -lt 30 ] || { echo "phase3: registry write failure not reported" >&2; exit 1; }
pre_info_before=$(grep -a '"event":"group.info"' "$fb" | grep -a -c '"group":"'"$pre_gid"'"' || true)
echo '{"op":"status"}' >&4
i=0
while [ "$i" -lt 30 ]; do
	pre_info_after=$(grep -a '"event":"group.info"' "$fb" | grep -a -c '"group":"'"$pre_gid"'"' || true)
	if [ "$pre_info_after" -gt "$pre_info_before" ]; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: failed leave mutated the group" >&2; exit 1; }

fault_groups_before=$(grep -a -c '"action":"create"' "$fa" 2>/dev/null || true)
printf '%s\n' '{"op":"group.create","title":"Persistence fault"}' >&3
i=0
fault_gid=""
while [ "$i" -lt 40 ]; do
	fault_groups_after=$(grep -a -c '"action":"create"' "$fa" 2>/dev/null || true)
	if [ "$fault_groups_after" -gt "$fault_groups_before" ]; then
		fault_gid=$(grep -a '"event":"group.changed"' "$fa" | grep -a '"action":"create"' |
			tail -1 | sed -n 's/.*"group":"\([^"]*\)".*/\1/p')
		[ -n "$fault_gid" ] && break
	fi
	i=$((i + 1))
	sleep 0.1
done
[ -n "$fault_gid" ] || { echo "phase3: persistence-fault group missing" >&2; exit 1; }
chmod 0500 "$sa"
printf '{"op":"invite.create","ttlSec":86400,"kind":"group","group":"%s","role":"member","id":"0","key":"%s","request":"gi-phase3-persist-fail"}\n' \
	"$fault_gid" "$friend_key" >&3
i=0
while [ "$i" -lt 50 ]; do
	if grep -a '"event":"group.invite.failed"' "$fa" |
	   grep -a '"code":"group_registry_failed"' |
	   grep -a -q '"request":"gi-phase3-persist-fail"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.1
done
chmod 0700 "$sa"
[ "$i" -lt 50 ] || {
	echo "phase3: group binding persistence failure was not correlated" >&2
	tail -n 30 -- "$fa" "$fb" "$fa.err" "$fb.err" >&2 || true
	exit 1
}

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

mkfifo "$holdd"
OMAQ_HOME="$hd" OMAQ_STATE="$sd" "$bin" >"$fd" 2>"$fd.err" <"$holdd" &
pd=$!
exec 6>"$holdd"
i=0
while [ "$i" -lt 40 ]; do
	[ -f "$sd/group-bind.pending" ] &&
	[ ! -s "$sd/group-bind.pending" ] && break
	if ! kill -0 "$pd" 2>/dev/null; then
		echo "phase3: pre-accept recovery helper failed" >&2
		tail -n 20 -- "$fd" "$fd.err" >&2 || true
		exit 1
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 40 ] || { echo "phase3: pre-accept journal was not recovered" >&2; exit 1; }
printf '{"op":"status","id":"phase3-preaccept-recovery"}\n' >&6
i=0
while [ "$i" -lt 30 ]; do
	grep -a -q '"request":"phase3-preaccept-recovery"' "$fd" && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: recovered helper did not answer" >&2; exit 1; }
if grep -a '"event":"group.info"' "$fd" | grep -a -q '"group":"'"$gid"'"'; then
	echo "phase3: incomplete accepted group survived journal recovery" >&2
	exit 1
fi
exec 6>&-
kill "$pd" 2>/dev/null || true
wait "$pd" 2>/dev/null || true
pd=""

mkfifo "$holde"
OMAQ_HOME="$he" OMAQ_STATE="$se" "$bin" >"$fe" 2>"$fe.err" <"$holde" &
pe=$!
exec 7>"$holde"
i=0
while [ "$i" -lt 50 ]; do
	if [ ! -e "$he/group-registry.pending" ] &&
	   ! grep -a -q "^${gid#g:}[[:space:]]" "$he/groups.tsv" 2>/dev/null &&
	   ! grep -a -q "^${gid}[[:space:]]" "$he/group-friends.tsv" 2>/dev/null; then
		break
	fi
	if ! kill -0 "$pe" 2>/dev/null; then
		echo "phase3: registry transaction recovery helper failed" >&2
		tail -n 20 -- "$fe" "$fe.err" >&2 || true
		exit 1
	fi
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 50 ] || { echo "phase3: split registry transaction was not recovered" >&2; exit 1; }
printf '{"op":"status","id":"phase3-registry-recovery"}\n' >&7
i=0
while [ "$i" -lt 30 ]; do
	grep -a -q '"request":"phase3-registry-recovery"' "$fe" && break
	i=$((i + 1))
	sleep 0.1
done
[ "$i" -lt 30 ] || { echo "phase3: registry recovery helper did not answer" >&2; exit 1; }
exec 7>&-
kill "$pe" 2>/dev/null || true
wait "$pe" 2>/dev/null || true
pe=""

set +e
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" --hold
rc=$?
set -e
[ "$rc" -eq 2 ] || { echo "phase3: expected lock exit 2, got $rc" >&2; exit 1; }

if ! kill -0 "$pa" 2>/dev/null; then
	echo "phase3: helper A died" >&2
	exit 1
fi

if grep -R -a -q 'OQX1|gmb' "$ha/history" "$hb/history" 2>/dev/null; then
	echo "phase3: group binding control leaked into history" >&2
	exit 1
fi
echo "phase3: ok"
exit 0
