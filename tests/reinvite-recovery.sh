#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "reinvite-recovery: no helper" >&2; exit 1; }

base=$(mktemp -d /tmp/omaq-reinvite-recovery-XXXXXX)
home="$base/home"
state="$base/state"
fifo="$base/input"
pid=""
foreign_pid=""
fd_open=0
foreign_fd_open=0
cleanup() {
	if [ "$fd_open" -eq 1 ]; then
		exec 3>&- 2>/dev/null || true
	fi
	if [ "$foreign_fd_open" -eq 1 ]; then
		exec 4>&- 2>/dev/null || true
	fi
	[ -n "$pid" ] && kill "$pid" 2>/dev/null || true
	[ -n "$pid" ] && wait "$pid" 2>/dev/null || true
	[ -n "$foreign_pid" ] && kill "$foreign_pid" 2>/dev/null || true
	[ -n "$foreign_pid" ] && wait "$foreign_pid" 2>/dev/null || true
	rm -rf "$base"
}
trap cleanup EXIT HUP INT TERM
mkdir -m 700 "$home" "$state"
mkfifo "$fifo"
exec 3<>"$fifo"
fd_open=1

start_helper() {
	out=$1
	OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$fifo" &
	pid=$!
	i=0
	while [ "$i" -lt 100 ]; do
		[ -S "$state/omaq.sock" ] && return 0
		i=$((i + 1))
		sleep 0.05
	done
	echo "reinvite-recovery: helper did not start" >&2
	return 1
}

stop_helper() {
	kill "$pid"
	wait "$pid" || true
	pid=""
}

start_helper "$base/first.out"
i=0
while [ "$i" -lt 100 ]; do
	[ -s "$home/tox.save" ] && [ -s "$state/identity-presence" ] &&
		[ -s "$state/identity-recovery.save" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: guarded identity was not initialized" >&2; exit 1; }
printf '%s\n' '{"op":"status","id":"identity-before-guard-test"}' >&3
printf '{"op":"identity.export","path":"%s","id":"identity-guard-export"}\n' \
	"$base/identity.bundle" >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"export"' "$base/first.out" |
		grep -a -q '"request":"identity-guard-export"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ -s "$base/identity.bundle" ] || {
	echo "reinvite-recovery: matching recovery bundle was not exported" >&2
	exit 1
}
identity_before=$(grep -a '"event":"snapshot"' "$base/first.out" |
	grep -a '"request":"identity-before-guard-test"' | tail -1 |
	sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ -n "$identity_before" ] || { echo "reinvite-recovery: original identity missing" >&2; exit 1; }
stop_helper

mkdir -m 700 "$base/foreign-home" "$base/foreign-state"
mkfifo "$base/foreign-input"
exec 4<>"$base/foreign-input"
foreign_fd_open=1
OMAQ_HOME="$base/foreign-home" OMAQ_STATE="$base/foreign-state" "$bin" \
	>"$base/foreign.out" 2>"$base/foreign.err" <"$base/foreign-input" &
foreign_pid=$!
i=0
while [ "$i" -lt 100 ]; do
	[ -S "$base/foreign-state/omaq.sock" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: foreign helper did not start" >&2; exit 1; }
printf '{"op":"identity.export","path":"%s","id":"identity-foreign-export"}\n' \
	"$base/foreign.bundle" >&4
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"export"' "$base/foreign.out" |
		grep -a -q '"request":"identity-foreign-export"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ -s "$base/foreign.bundle" ] || {
	echo "reinvite-recovery: foreign identity bundle was not exported" >&2
	exit 1
}
kill "$foreign_pid"
wait "$foreign_pid" || true
foreign_pid=""
exec 4>&-
foreign_fd_open=0

printf 'reinvite required\n' >"$home/direct-state-reinvite.required"
chmod 600 "$home/direct-state-reinvite.required"
start_helper "$base/recovery.out"
printf '%s\n' '{"op":"status","id":"reinvite-status"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"event":"direct.reinvite","required":true' "$base/recovery.out" 2>/dev/null &&
		grep -a -q '"code":"direct_state_reinvite_required"' "$base/recovery.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: structured recovery state was not projected" >&2; exit 1; }
grep -a '"event":"direct.reinvite","required":true' "$base/recovery.out" |
	grep -a -q '"identityRetained":true,"contactsRetained":true' || {
	echo "reinvite-recovery: retained identity/contact facts were not explicit" >&2
	exit 1
}

printf '%s\n' '{"op":"direct.reinvite.clear"}' >&3
sleep 0.1
[ -f "$home/direct-state-reinvite.required" ] || {
	echo "reinvite-recovery: uncorrelated clear removed the recovery marker" >&2
	exit 1
}
printf '%s\n' '{"op":"direct.reinvite.clear","id":"reinvite-clear-test"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"direct.reinvite","required":false' "$base/recovery.out" |
		grep -a -q '"request":"reinvite-clear-test"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: clear was not request-correlated" >&2; exit 1; }
[ ! -e "$home/direct-state-reinvite.required" ] || {
	echo "reinvite-recovery: helper did not clear its recovery marker" >&2
	exit 1
}
stop_helper

start_helper "$base/final.out"
printf '%s\n' '{"op":"status","id":"reinvite-final"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"event":"direct.reinvite","required":false' "$base/final.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: cleared state did not survive restart" >&2; exit 1; }
! grep -a -q '"code":"direct_state_reinvite_required"' "$base/final.out" || {
	echo "reinvite-recovery: cleared warning returned after restart" >&2
	exit 1
}
stop_helper

printf 'reinvite required\n' >"$home/direct-state-reinvite.required"
chmod 600 "$home/direct-state-reinvite.required"
rm "$home/tox.save" "$state/identity-recovery.save"
printf '999\t5\n' >"$state/unread.tsv"
chmod 600 "$state/unread.tsv"
start_helper "$base/missing.out"
printf '%s\n' '{"op":"status","id":"identity-missing-test"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"code":"identity_missing"' "$base/missing.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: missing identity was not reported" >&2; exit 1; }
[ ! -e "$home/tox.save" ] || {
	echo "reinvite-recovery: missing existing identity was silently replaced" >&2
	exit 1
}
printf '%s\n' '{"op":"invite.redeem","payload":"guard-gate","id":"guard-redeem-test"}' >&3
sleep 0.1
grep -a '"code":"identity_missing"' "$base/missing.out" |
	grep -a -q '"request":"guard-redeem-test"' || {
	echo "reinvite-recovery: identity guard did not correlate invite redemption failure" >&2
	exit 1
}
printf '%s\n' '{"op":"direct.reinvite.clear","id":"guard-reinvite-clear-test"}' >&3
sleep 0.1
grep -a '"code":"identity_missing"' "$base/missing.out" |
	grep -a -q '"request":"guard-reinvite-clear-test"' || {
	echo "reinvite-recovery: identity guard did not correlate recovery-clear failure" >&2
	exit 1
}
[ -f "$home/direct-state-reinvite.required" ] || {
	echo "reinvite-recovery: blocked recovery clear removed the marker" >&2
	exit 1
}
printf '{"op":"identity.import","path":"%s","replace":true,"id":"identity-foreign-import"}\n' \
	"$base/foreign.bundle" >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"code":"identity_mismatch"' "$base/missing.out" |
		grep -a -q '"request":"identity-foreign-import"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ ! -e "$home/tox.save" ] || {
	echo "reinvite-recovery: foreign identity bypassed the protected fingerprint" >&2
	exit 1
}
printf '{"op":"identity.import","path":"%s","replace":true,"id":"identity-guard-import"}\n' \
	"$base/identity.bundle" >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"import"' "$base/missing.out" |
		grep -a -q '"request":"identity-guard-import"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: matching bundle did not repair missing identity" >&2; exit 1; }
printf '%s\n' '{"op":"status","id":"identity-after-guard-test"}' >&3
sleep 0.2
identity_after=$(grep -a '"event":"snapshot"' "$base/missing.out" |
	grep -a '"request":"identity-after-guard-test"' | tail -1 |
	sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ "${identity_before%????????????}" = "${identity_after%????????????}" ] || {
	echo "reinvite-recovery: guarded import activated a different identity" >&2
	exit 1
}
[ -s "$home/tox.save" ] && [ -s "$state/identity-recovery.save" ] || {
	echo "reinvite-recovery: guarded import did not restore primary and recovery copies" >&2
	exit 1
}
[ ! -s "$state/unread.tsv" ] || {
	echo "reinvite-recovery: guarded import skipped stale unread reconciliation" >&2
	exit 1
}
restored_instance=$(grep -a '"event":"snapshot"' "$base/missing.out" |
	grep -a '"request":"identity-after-guard-test"' | tail -1 |
	sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
[ -n "$restored_instance" ] || { echo "reinvite-recovery: restored instance missing" >&2; exit 1; }
printf '{"op":"identity.ready","id":"%s"}\n' "$restored_instance" >&3
printf '%s\n' '{"op":"identity.protect","passphrase":"guard-test-pass","id":"identity-before-uncertain-protect"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"protect"' "$base/missing.out" |
		grep -a -q '"request":"identity-before-uncertain-protect"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: uncertainty fixture was not protected" >&2; exit 1; }
printf 'OMAQDR1\n%s\n' 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' \
	>"$home/direct-remove.pending"
chmod 600 "$home/direct-remove.pending"
printf '999\t5\n' >"$state/unread.tsv"
chmod 600 "$state/unread.tsv"
uncertain_unread_before=$(sha256sum "$state/unread.tsv" | cut -d' ' -f1)
stop_helper
printf 'primary durability uncertain\n' >"$state/identity-primary-uncertain"
chmod 600 "$state/identity-primary-uncertain"
start_helper "$base/uncertain-primary.out"
printf '%s\n' '{"op":"status","id":"identity-primary-status"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"event":"identity.primary","uncertain":true' "$base/uncertain-primary.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: primary uncertainty did not survive restart" >&2; exit 1; }
printf '%s\n' '{"op":"identity.unlock","passphrase":"guard-test-pass","id":"identity-uncertain-unlock"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"unlock"' "$base/uncertain-primary.out" |
		grep -a -q '"request":"identity-uncertain-unlock"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ -f "$home/direct-remove.pending" ] &&
	[ "$uncertain_unread_before" = "$(sha256sum "$state/unread.tsv" | cut -d' ' -f1)" ] || {
	echo "reinvite-recovery: unlock mutated pending contact or unread state before verification" >&2
	exit 1
}
groups_before=$(sha256sum "$home/groups.tsv" | cut -d' ' -f1)
printf '%s\n' '{"op":"group.leave","group":"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}' >&3
sleep 0.1
grep -a -q '"code":"identity_primary_uncertain"' "$base/uncertain-primary.out" || {
	echo "reinvite-recovery: mutation was not blocked during primary verification" >&2
	exit 1
}
groups_after=$(sha256sum "$home/groups.tsv" | cut -d' ' -f1)
[ "$groups_before" = "$groups_after" ] || {
	echo "reinvite-recovery: blocked group mutation changed the registry" >&2
	exit 1
}
stop_helper
printf 'primary acknowledgement pending\n' >"$state/identity-primary-ack.txn"
chmod 600 "$state/identity-primary-ack.txn"
rm "$state/identity-primary-uncertain"
start_helper "$base/uncertain-ack-recovery.out"
printf '%s\n' '{"op":"status","id":"identity-ack-recovery-status"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"event":"identity.primary","uncertain":true' "$base/uncertain-ack-recovery.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: interrupted acknowledgement lost its warning" >&2; exit 1; }
printf '%s\n' '{"op":"identity.unlock","passphrase":"guard-test-pass","id":"identity-ack-recovery-unlock"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"unlock"' "$base/uncertain-ack-recovery.out" |
		grep -a -q '"request":"identity-ack-recovery-unlock"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ -f "$home/direct-remove.pending" ] || {
	echo "reinvite-recovery: interrupted acknowledgement replayed pending removal" >&2
	exit 1
}
printf '%s\n' '{"op":"identity.primary.acknowledge","id":"identity-primary-ack"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity.primary","uncertain":false' "$base/uncertain-ack-recovery.out" |
		grep -a -q '"request":"identity-primary-ack"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ ! -e "$state/identity-primary-uncertain" ] &&
	[ ! -e "$state/identity-primary-ack.txn" ] &&
	[ ! -e "$home/direct-remove.pending" ] && [ ! -s "$state/unread.tsv" ] || {
	echo "reinvite-recovery: primary uncertainty acknowledgement was not durable" >&2
	exit 1
}
stop_helper
rm "$state/identity-recovery.save"
ln -s missing-recovery "$state/identity-recovery.save"
printf 'primary durability uncertain\n' >"$state/identity-primary-uncertain"
chmod 600 "$state/identity-primary-uncertain"
start_helper "$base/invalid-recovery.out"
printf '%s\n' '{"op":"status","id":"identity-invalid-recovery"}' >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a -q '"code":"identity_guard_invalid"' "$base/invalid-recovery.out" 2>/dev/null && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "reinvite-recovery: unsafe recovery copy did not fail closed" >&2; exit 1; }
printf '{"op":"identity.import","path":"%s","replace":true,"id":"identity-invalid-foreign"}\n' \
	"$base/foreign.bundle" >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"code":"identity_mismatch"' "$base/invalid-recovery.out" |
		grep -a -q '"request":"identity-invalid-foreign"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] && [ -L "$state/identity-recovery.save" ] || {
	echo "reinvite-recovery: foreign bundle changed combined recovery state" >&2
	exit 1
}
printf '{"op":"identity.import","path":"%s","replace":true,"id":"identity-invalid-repair"}\n' \
	"$base/identity.bundle" >&3
i=0
while [ "$i" -lt 100 ]; do
	grep -a '"event":"identity","op":"import"' "$base/invalid-recovery.out" |
		grep -a -q '"request":"identity-invalid-repair"' && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] &&
	grep -a -q '"event":"identity.primary","uncertain":false' "$base/invalid-recovery.out" &&
	[ -f "$state/identity-recovery.save" ] &&
	[ ! -L "$state/identity-recovery.save" ] &&
	[ ! -e "$state/identity-primary-uncertain" ] &&
	[ ! -L "$state/identity-primary-uncertain" ] || {
	echo "reinvite-recovery: matching bundle did not repair unsafe recovery state" >&2
	exit 1
}

echo "reinvite-recovery: ok"
