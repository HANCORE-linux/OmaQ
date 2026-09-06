#!/bin/sh
# Setting an identity passphrase must also seal chat history and Ratchet state
# at rest, and removing it must return both to plaintext. Sealed Ratchet state
# whose key is gone must be quarantined, never read as plaintext and never
# silently regenerated. No network required.
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
	rm -rf "$home" "$state" "$fifo" "$out" "$out.err" "${magic:-}" "${head:-}"
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

# The socket is published before the identity finishes loading; identity
# operations are refused until it exists.
i=0
while [ "$i" -lt 200 ]; do
	[ -s "$home/tox.save" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 200 ] || { echo "history-seal: identity never loaded" >&2; exit 1; }

# Plant a conversation transcript the way the store does.
mkdir -p "$home/history/7"
printf '{"id":"m1","from":"peer","text":"attack at dawn"}\n' >"$home/history/7/messages.jsonl"
chmod 600 "$home/history/7/messages.jsonl"

# Blob headers are binary, so compare bytes rather than shell strings.
magic=$(mktemp /tmp/omaq-hsm-XXXXXX)
head=$(mktemp /tmp/omaq-hsh-XXXXXX)
printf 'OMAQSEAL1' >"$magic"

is_sealed() {
	head -c 9 -- "$1" >"$head" 2>/dev/null || return 1
	cmp -s "$head" "$magic"
}

# Every regular file under $1 must begin with the seal magic.
assert_sealed() {
	found=0
	for f in $(find "$1" -type f 2>/dev/null); do
		found=1
		if ! is_sealed "$f"; then
			echo "history-seal: $f is not sealed" >&2
			exit 1
		fi
	done
	[ "$found" -eq 1 ] || { echo "history-seal: no files under $1" >&2; exit 1; }
}

assert_plaintext() {
	for f in $(find "$1" -type f 2>/dev/null); do
		if is_sealed "$f"; then
			echo "history-seal: $f is still sealed" >&2
			exit 1
		fi
	done
}

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
wait_for '"op":"protect","request":"hs-protect","protected":true'

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
# Ratchet state - including the Signal identity private key - is sealed too.
[ -f "$home/ratchet/identity" ] || {
	echo "history-seal: no Ratchet identity blob" >&2
	exit 1
}
assert_sealed "$home/ratchet"

printf '%s\n' '{"op":"identity.unprotect","passphrase":"a strong passphrase","id":"hs-unprotect"}' >&3
wait_for '"op":"unprotect","request":"hs-unprotect","protected":false'

[ -f "$home/seal.key" ] && { echo "history-seal: seal key survived unprotect" >&2; exit 1; }
grep -a -q "attack at dawn" "$home/history/7/messages.jsonl" || {
	echo "history-seal: history not restored after unprotect" >&2
	exit 1
}
assert_plaintext "$home/ratchet"

# Quarantine: seal again, then lose the key and restart. The sealed Ratchet
# store must be moved aside and fresh invitations required - never opened as
# if it were empty.
printf '%s\n' '{"op":"identity.protect","passphrase":"a strong passphrase","id":"hs-protect2"}' >&3
wait_for '"op":"protect","request":"hs-protect2","protected":true'
assert_sealed "$home/ratchet"
is_sealed "$home/ratchet/identity" || {
	echo "history-seal: identity blob not sealed before quarantine" >&2
	exit 1
}
exec 3>&-
fd_open=0
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
rm -f "$home/seal.key"
# The planted transcript has no owning contact; OmaQ already refuses to unlock
# while an orphan conversation directory exists, which is unrelated to
# sealing, so drop it before exercising the quarantine.
rm -rf "$home/history"
rm -f "$fifo"
mkfifo "$fifo"
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$fifo" &
pid=$!
exec 3>"$fifo"
fd_open=1
i=0
while [ "$i" -lt 200 ]; do
	[ -S "$state/omaq.sock" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 200 ] || { echo "history-seal: helper did not restart" >&2; exit 1; }
# The identity is passphrase-protected again, so unlocking is what reaches the
# Ratchet store.
printf '%s\n' '{"op":"identity.unlock","passphrase":"a strong passphrase","id":"hs-unlock"}' >&3
i=0
while [ "$i" -lt 200 ]; do
	set -- "$home"/ratchet.locked-*
	[ -d "$1" ] && break
	i=$((i + 1))
	sleep 0.05
done
set -- "$home"/ratchet.locked-*
[ -d "$1" ] || {
	echo "history-seal: sealed Ratchet state was not quarantined" >&2
	tail -5 "$out.err" >&2 || true
	exit 1
}
assert_sealed "$1"
[ -f "$home/direct-state-reinvite.required" ] || {
	echo "history-seal: quarantine did not require fresh invitations" >&2
	exit 1
}

if ! kill -0 "$pid" 2>/dev/null; then
	echo "history-seal: helper died" >&2
	exit 1
fi
echo "history-seal: ok"
exit 0
