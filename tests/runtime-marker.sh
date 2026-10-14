#!/bin/sh
# Runtime marker: a failed omaq.protocol rewrite after an import is reported, repeated on status and healed;
# after a guarded restore that rolls back it is repaired without claiming that the change was saved.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "runtime-marker: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
home=$(mktemp -d /tmp/omaq-marker-XXXXXX)
state=$(mktemp -d /tmp/omaq-markers-XXXXXX)
bundle=$(mktemp -d /tmp/omaq-markerb-XXXXXX)
out1=$(mktemp /tmp/omaq-markero-XXXXXX)
out2=$(mktemp /tmp/omaq-markero2-XXXXXX)
out=$out1
hold=$(mktemp -u /tmp/omaq-markerf-XXXXXX)
pid=""
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$home" "$state" "$bundle" "$out1" "$out1.err" "$out2" "$out2.err" "$hold"
}
trap cleanup EXIT

case "$home" in
"$real_home"|"$real_home"/*) echo "runtime-marker: refused real home" >&2; exit 1 ;;
esac

settled() {
	echo "{\"op\":\"status\",\"id\":\"$1-settled\"}" >&3
	wait_line "$2" '"event":"snapshot"' "\"request\":\"$1-settled\""
}

wait_line() {
	i=0
	while [ "$i" -lt 100 ]; do
		if tail -n +"$1" "$out" | grep -a "$2" | grep -a -q "$3"; then
			return 0
		fi
		i=$((i + 1))
		sleep 0.05
	done
	return 1
}

mkfifo "$hold"
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
i=0
while [ ! -f "$state/omaq.protocol" ] && [ "$i" -lt 100 ]; do
	i=$((i + 1))
	sleep 0.05
done
echo '{"op":"status","id":"marker-start"}' >&3
wait_line 1 '"event":"snapshot"' '"request":"marker-start"' || {
	echo "runtime-marker: no snapshot" >&2
	exit 1
}
printf '{"op":"identity.export","path":"%s/own.save","replace":false,"id":"marker-export"}\n' "$bundle" >&3
wait_line 1 '"op":"export"' '"request":"marker-export"' || {
	echo "runtime-marker: export failed" >&2
	exit 1
}

rm "$state/omaq.protocol"
mkdir "$state/omaq.protocol"
: >"$state/omaq.protocol/blocked"
before=$(($(wc -l <"$out") + 1))
printf '{"op":"identity.import","path":"%s/own.save","replace":true,"id":"marker-import"}\n' "$bundle" >&3
wait_line "$before" '"op":"import"' '"request":"marker-import"' || {
	echo "runtime-marker: import failed" >&2
	exit 1
}
tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"' || {
	echo "runtime-marker: a failed marker rewrite was not reported" >&2
	exit 1
}
kill -0 "$pid" 2>/dev/null || {
	echo "runtime-marker: the helper stopped after a failed marker rewrite" >&2
	exit 1
}

before=$(($(wc -l <"$out") + 1))
echo '{"op":"status","id":"marker-blocked"}' >&3
wait_line "$before" '"event":"snapshot"' '"request":"marker-blocked"' || {
	echo "runtime-marker: no snapshot while the marker is blocked" >&2
	exit 1
}
settled marker-blocked "$before" || {
	echo "runtime-marker: no answer to the status after marker-blocked" >&2
	exit 1
}
tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"' || {
	echo "runtime-marker: status did not repeat the marker failure" >&2
	exit 1
}
blocked_instance=$(grep -a '"request":"marker-blocked"' "$out" | tail -1 | sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
printf '{"op":"identity.ready","id":"%s"}\n' "$blocked_instance" >&3
printf '{"op":"identity.inspect","path":"%s/own.save","id":"marker-sync"}\n' "$bundle" >&3
wait_line 1 '"op":"inspect"' '"request":"marker-sync"' || {
	echo "runtime-marker: no inspect answer before the second import" >&2
	exit 1
}
before=$(($(wc -l <"$out") + 1))
printf '{"op":"identity.import","path":"%s/own.save","replace":true,"id":"marker-import-again"}\n' "$bundle" >&3
wait_line "$before" '"op":"import"' '"request":"marker-import-again"' || {
	echo "runtime-marker: the second import failed" >&2
	exit 1
}
tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"' || {
	echo "runtime-marker: a second import with the marker still blocked was not reported" >&2
	exit 1
}

rm -r "$state/omaq.protocol"
before=$(($(wc -l <"$out") + 1))
echo '{"op":"status","id":"marker-healed"}' >&3
wait_line "$before" '"event":"snapshot"' '"request":"marker-healed"' || {
	echo "runtime-marker: no snapshot after the marker became writable" >&2
	exit 1
}
settled marker-healed "$before" || {
	echo "runtime-marker: no answer to the status after marker-healed" >&2
	exit 1
}
if tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"'; then
	echo "runtime-marker: the failure was still reported after the marker became writable" >&2
	exit 1
fi
instance=$(tail -n +"$before" "$out" | grep -a '"request":"marker-healed"' | tail -1 |
	sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
if ! { [ -n "$instance" ] && [ -f "$state/omaq.protocol" ] &&
	grep -q "\"instance\":\"$instance\"" "$state/omaq.protocol"; }; then
	echo "runtime-marker: the rewritten marker does not name the current instance" >&2
	exit 1
fi

i=0
while [ "$i" -lt 100 ]; do
	[ -s "$home/tox.save" ] && [ -s "$state/identity-presence" ] &&
		[ -s "$state/identity-recovery.save" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 100 ] || { echo "runtime-marker: guarded identity was not initialized" >&2; exit 1; }
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
pid=""
exec 3>&-
rm "$home/tox.save" "$state/identity-recovery.save"
out=$out2
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
echo '{"op":"status","id":"marker-missing"}' >&3
wait_line 1 '"code":"identity_missing"' '"code"' || {
	echo "runtime-marker: the helper did not report the missing identity" >&2
	exit 1
}
wait_line 1 '"event":"snapshot"' '"request":"marker-missing"' || {
	echo "runtime-marker: no snapshot for the missing identity" >&2
	exit 1
}
missing_instance=$(grep -a '"request":"marker-missing"' "$out" | tail -1 | sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')

rm "$state/omaq.protocol"
mkdir "$state/omaq.protocol" "$home/tox.save.tmp"
: >"$state/omaq.protocol/blocked"
: >"$home/tox.save.tmp/blocked"
before=$(($(wc -l <"$out") + 1))
printf '{"op":"identity.import","path":"%s/own.save","replace":true,"id":"marker-guard-import"}\n' "$bundle" >&3
wait_line "$before" '"code":"identity_import_failed"' '"request":"marker-guard-import"' || {
	echo "runtime-marker: the guarded restore did not roll back" >&2
	exit 1
}
echo '{"op":"status","id":"marker-rolled-back"}' >&3
wait_line "$before" '"event":"snapshot"' '"request":"marker-rolled-back"' || {
	echo "runtime-marker: no snapshot after the rollback" >&2
	exit 1
}
settled marker-rolled-back "$before" || {
	echo "runtime-marker: no answer to the status after marker-rolled-back" >&2
	exit 1
}
rolled_instance=$(grep -a '"request":"marker-rolled-back"' "$out" | tail -1 | sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
[ -n "$rolled_instance" ] && [ "$rolled_instance" != "$missing_instance" ] || {
	echo "runtime-marker: the guarded restore did not renew the instance before it rolled back" >&2
	exit 1
}
if tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"'; then
	echo "runtime-marker: a rolled-back restore was reported as a saved identity change" >&2
	exit 1
fi

rm -r "$state/omaq.protocol" "$home/tox.save.tmp"
before=$(($(wc -l <"$out") + 1))
echo '{"op":"status","id":"marker-repaired"}' >&3
wait_line "$before" '"event":"snapshot"' '"request":"marker-repaired"' || {
	echo "runtime-marker: no snapshot after the rollback marker became writable" >&2
	exit 1
}
settled marker-repaired "$before" || {
	echo "runtime-marker: no answer to the status after marker-repaired" >&2
	exit 1
}
if tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"'; then
	echo "runtime-marker: a rolled-back restore was reported after the marker became writable" >&2
	exit 1
fi
if ! { [ -f "$state/omaq.protocol" ] && grep -q "\"instance\":\"$rolled_instance\"" "$state/omaq.protocol"; }; then
	echo "runtime-marker: the marker was not repaired after the rolled-back restore" >&2
	exit 1
fi

rm "$state/omaq.protocol"
mkdir "$state/omaq.protocol"
: >"$state/omaq.protocol/blocked"
before=$(($(wc -l <"$out") + 1))
printf '{"op":"identity.import","path":"%s/own.save","replace":true,"id":"marker-guard-restore"}\n' "$bundle" >&3
wait_line "$before" '"op":"import"' '"request":"marker-guard-restore"' || {
	echo "runtime-marker: the guarded restore did not succeed" >&2
	exit 1
}
tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"' || {
	echo "runtime-marker: a saved guarded restore did not report the failed marker" >&2
	exit 1
}
rm -r "$state/omaq.protocol"
before=$(($(wc -l <"$out") + 1))
echo '{"op":"status","id":"marker-restored"}' >&3
wait_line "$before" '"event":"snapshot"' '"request":"marker-restored"' || {
	echo "runtime-marker: no snapshot after the guarded restore" >&2
	exit 1
}
settled marker-restored "$before" || {
	echo "runtime-marker: no answer to the status after marker-restored" >&2
	exit 1
}
restored_instance=$(tail -n +"$before" "$out" | grep -a '"request":"marker-restored"' | tail -1 |
	sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
if tail -n +"$before" "$out" | grep -a -q '"code":"runtime_marker_failed"'; then
	echo "runtime-marker: the failure was still reported after the restored marker became writable" >&2
	exit 1
fi
if ! { [ -n "$restored_instance" ] && [ -f "$state/omaq.protocol" ] &&
	grep -q "\"instance\":\"$restored_instance\"" "$state/omaq.protocol"; }; then
	echo "runtime-marker: the marker does not name the restored instance" >&2
	exit 1
fi

echo "runtime-marker: ok"
