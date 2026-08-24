#!/bin/sh
# toxencryptsave: protect tox.save, restart locked, unlock, wrong pass fails.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "encryptsave: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-enc-XXXXXX)
sa=$(mktemp -d /tmp/omaq-encs-XXXXXX)
out=$(mktemp /tmp/omaq-enco-XXXXXX)
hold=$(mktemp -u /tmp/omaq-encf-XXXXXX)
pid=""
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

echo '{"op":"identity.protect","passphrase":"test-pass-1"}' >&3
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
echo '{"op":"identity.unlock","passphrase":"wrong-pass"}' >&3
sleep 0.3
tail -n +"$((before + 1))" "$out2" | grep -a -q '"code":"locked"' || {
	echo "encryptsave: wrong pass should error locked" >&2
	exit 1
}
if tail -n +"$((before + 1))" "$out2" | grep -a -q '"op":"unlock"'; then
	echo "encryptsave: wrong pass must not unlock" >&2
	exit 1
fi

echo '{"op":"identity.unlock","passphrase":"test-pass-1"}' >&3
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

echo '{"op":"status"}' >&3
sleep 0.2
grep -a -q '"addr"' "$out2" || { echo "encryptsave: no addr after unlock" >&2; exit 1; }
grep -a -q '"protected":true' "$out2" || { echo "encryptsave: not protected" >&2; exit 1; }

echo '{"op":"identity.unprotect","passphrase":"test-pass-1"}' >&3
sleep 0.3
grep -a -q '"protected":false' "$out2" || { echo "encryptsave: unprotect" >&2; cat "$out2" >&2; exit 1; }

echo "encryptsave: ok"
exit 0
