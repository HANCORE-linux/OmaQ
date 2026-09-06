#!/bin/sh
# Setting an identity passphrase must also seal chat history at rest, and
# removing it must return the history to plaintext. No network required.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin=${1:-$root/helper/omaq}
[ -x "$bin" ] || { echo "history-seal: missing helper binary" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
home=$(mktemp -d /tmp/omaq-hs-XXXXXX)
state=$(mktemp -d /tmp/omaq-hss-XXXXXX)
fifo=$(mktemp -u /tmp/omaq-hsf-XXXXXX)
out=$(mktemp /tmp/omaq-hso-XXXXXX)
pid=""
fd_open=0
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	[ "$fd_open" -eq 1 ] && exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$home" "$state" "$fifo" "$out" "$out.err"
}
trap cleanup EXIT HUP INT TERM

case "$home" in
"$real_home"|"$real_home"/*)
	echo "history-seal: refused real home" >&2
	exit 1
	;;
esac

mkfifo "$fifo"
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$fifo" &
pid=$!
exec 3>"$fifo"
fd_open=1

i=0
while [ "$i" -lt 100 ]; do
	[ -S "$state/omaq.sock" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "history-seal: helper did not start" >&2; exit 1; }

# Plant a conversation transcript the way the store does.
mkdir -p "$home/history/7"
printf '{"id":"m1","from":"peer","text":"attack at dawn"}\n' >"$home/history/7/messages.jsonl"
chmod 600 "$home/history/7/messages.jsonl"

wait_for() {
	i=0
	while [ "$i" -lt 200 ]; do
		grep -a -q "$1" "$out" && return 0
		i=$((i + 1))
		sleep 0.05
	done
	echo "history-seal: timed out waiting for $1" >&2
	tail -5 "$out.err" >&2 || true
	exit 1
}

printf '%s\n' '{"op":"identity.protect","passphrase":"a strong passphrase","id":"hs-protect"}' >&3
wait_for '"request":"hs-protect"'

[ -f "$home/seal.key" ] || { echo "history-seal: no seal key after protect" >&2; exit 1; }
mode=$(stat -c %a -- "$home/seal.key")
[ "$mode" = "600" ] || { echo "history-seal: seal key mode $mode" >&2; exit 1; }
if grep -a -q "attack at dawn" "$home/history/7/messages.jsonl"; then
	echo "history-seal: history still plaintext after protect" >&2
	exit 1
fi
grep -a -q '^#1:' "$home/history/7/messages.jsonl" || {
	echo "history-seal: history not sealed after protect" >&2
	exit 1
}
# The plaintext must not survive anywhere under the home.
if grep -a -r -q "attack at dawn" "$home" 2>/dev/null; then
	echo "history-seal: plaintext transcript left on disk" >&2
	exit 1
fi

printf '%s\n' '{"op":"identity.unprotect","passphrase":"a strong passphrase","id":"hs-unprotect"}' >&3
wait_for '"request":"hs-unprotect"'

[ -f "$home/seal.key" ] && { echo "history-seal: seal key survived unprotect" >&2; exit 1; }
grep -a -q "attack at dawn" "$home/history/7/messages.jsonl" || {
	echo "history-seal: history not restored after unprotect" >&2
	exit 1
}

if ! kill -0 "$pid" 2>/dev/null; then
	echo "history-seal: helper died" >&2
	exit 1
fi
echo "history-seal: ok"
exit 0
