#!/bin/sh
# toxencryptsave: protect tox.save, restart locked, unlock, wrong pass fails.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "encryptsave: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-enc-XXXXXX)
sa=$(mktemp -d /tmp/omaq-encs-XXXXXX)
out=$(mktemp /tmp/omaq-enco-XXXXXX)
hold=$(mktemp -u /tmp/omaq-encf-XXXXXX)
pid=""
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$out" "$hold" "$out.err" "$out2" "$hold2"
}
out2=""
hold2=""
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "encryptsave: refused real home" >&2; exit 1 ;;
esac

mkfifo "$hold"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
sleep 0.4

echo '{"op":"status"}' >&3
sleep 0.2
grep -a -q '"addr"' "$out" || { echo "encryptsave: no addr" >&2; exit 1; }

before=$(wc -l <"$out")
echo '{"op":"identity.protect","passphrase":"short7","id":"identity-protect-short"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"forbidden"' |
	grep -a -q '"request":"identity-protect-short"' || {
	echo "encryptsave: short passphrase was not rejected" >&2
	exit 1
}
if tail -n +"$((before + 1))" "$out" | grep -a -q '"op":"protect"'; then
	echo "encryptsave: short passphrase protected identity" >&2
	exit 1
fi

echo '{"op":"identity.protect","passphrase":"test-pass-1","id":"identity-protect-1"}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"protect"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "encryptsave: protect failed" >&2; cat "$out" >&2; exit 1; }
grep -a '"op":"protect"' "$out" | tail -1 |
	grep -a -q '"request":"identity-protect-1"' || {
	echo "encryptsave: protect request correlation missing" >&2
	exit 1
}

kill "$pid" 2>/dev/null || true
pid=""
exec 3>&-
sleep 0.2

hold2=$(mktemp -u /tmp/omaq-encf2-XXXXXX)
out2=$(mktemp /tmp/omaq-enco2-XXXXXX)
mkfifo "$hold2"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out2" 2>"$out2.err" <"$hold2" &
pid=$!
exec 3>"$hold2"
sleep 0.4

echo '{"op":"status"}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"locked":true' "$out2"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "encryptsave: expected locked" >&2; cat "$out2" >&2; exit 1; }
grep -a '"event":"invite"' "$out2" | tail -1 |
	grep -a -q '"url":"","expires":0,"op":"status"' || {
	echo "encryptsave: locked status did not clear stale invite state" >&2
	exit 1
}

before=$(wc -l <"$out2")
printf '%s\n' '{"op":"invite.create","kind":"direct","request":"invite-locked-create"}' >&3
printf '%s\n' '{"op":"invite.revoke","request":"invite-locked-revoke"}' >&3
printf '%s\n' '{"op":"invite.redeem","payload":"locked-gate","id":"invite-locked-redeem"}' >&3
sleep 0.2
for request in invite-locked-create invite-locked-revoke invite-locked-redeem; do
	tail -n +"$((before + 1))" "$out2" | grep -a '"code":"locked"' |
		grep -a -q '"request":"'"$request"'"' || {
		echo "encryptsave: locked direct invite correlation missing for $request" >&2
		exit 1
	}
done

before=$(wc -l <"$out2")
printf '%s\n' '{"op":"invite.create","kind":"group","group":"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","id":"0","key":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","request":"gi-locked-gate"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$out2" | grep -a '"event":"group.invite.failed"' |
	grep -a '"code":"locked"' | grep -a -q '"request":"gi-locked-gate"' || {
	echo "encryptsave: locked group invite correlation missing" >&2
	exit 1
}

before=$(wc -l <"$out2")
echo '{"op":"msg.send","conversation":"0","text":"same-while-locked","id":"locked-message-1"}' >&3
echo '{"op":"msg.send","conversation":"0","text":"same-while-locked","id":"locked-message-2"}' >&3
sleep 0.2
for request in locked-message-1 locked-message-2; do
	tail -n +"$((before + 1))" "$out2" | grep -a '"event":"message.failed"' |
		grep -a '"code":"locked","delivered":false' |
		grep -a -q '"request":"'"$request"'"' || {
		echo "encryptsave: locked message correlation missing for $request" >&2
		exit 1
	}
done

before=$(wc -l <"$out2")
echo '{"op":"identity.inspect","path":"/tmp/locked-identity.save","id":"identity-inspect-locked-1"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$out2" | grep -a '"code":"locked"' |
	grep -a -q '"request":"identity-inspect-locked-1"' || {
	echo "encryptsave: locked identity request correlation missing" >&2
	exit 1
}

before=$(wc -l <"$out2")
echo '{"op":"identity.unlock","passphrase":"wrong-pass","id":"identity-unlock-wrong-1"}' >&3
sleep 0.3
tail -n +"$((before + 1))" "$out2" | grep -a '"code":"locked"' |
	grep -a -q '"request":"identity-unlock-wrong-1"' || {
	echo "encryptsave: wrong pass should error locked" >&2
	exit 1
}
if tail -n +"$((before + 1))" "$out2" | grep -a -q '"op":"unlock"'; then
	echo "encryptsave: wrong pass must not unlock" >&2
	exit 1
fi

echo '{"op":"identity.unlock","passphrase":"test-pass-1","id":"identity-unlock-1"}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"unlock"' "$out2"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "encryptsave: unlock failed" >&2; cat "$out2" >&2; exit 1; }
grep -a '"op":"unlock"' "$out2" | tail -1 |
	grep -a -q '"request":"identity-unlock-1"' || {
	echo "encryptsave: unlock request correlation missing" >&2
	exit 1
}

echo '{"op":"status"}' >&3
sleep 0.2
grep -a -q '"addr"' "$out2" || { echo "encryptsave: no addr after unlock" >&2; exit 1; }
grep -a -q '"protected":true' "$out2" || { echo "encryptsave: not protected" >&2; exit 1; }

echo '{"op":"identity.unprotect","passphrase":"test-pass-1","id":"identity-unprotect-1"}' >&3
sleep 0.3
grep -a '"op":"unprotect"' "$out2" | tail -1 |
	grep -a '"protected":false' | grep -a -q '"request":"identity-unprotect-1"' || {
	echo "encryptsave: unprotect" >&2
	cat "$out2" >&2
	exit 1
}

echo "encryptsave: ok"
exit 0
