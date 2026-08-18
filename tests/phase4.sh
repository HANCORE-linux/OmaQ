#!/bin/sh
# Phase 4: surfaces.json read/write, schema files present, one helper.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase4: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
home=$(mktemp -d /tmp/omaq-p4-XXXXXX)
state=$(mktemp -d /tmp/omaq-p4s-XXXXXX)
out=$(mktemp /tmp/omaq-p4o-XXXXXX)
hold=$(mktemp -u /tmp/omaq-p4f-XXXXXX)
pid=""
cleanup() {
	exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$home" "$state" "$out" "$hold" "$out.err"
}
trap cleanup EXIT

case "$home" in
"$real_home"|"$real_home"/*) echo "phase4: refused real home" >&2; exit 1 ;;
esac

mkfifo "$hold"
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
sleep 0.3

echo '{"op":"surface.set","conversation":"0","monitor":"DP-1","x":10,"y":20,"pinned":false}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"event":"surface"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase4: no surface event" >&2; exit 1; }

echo '{"op":"surface.get","conversation":"0"}' >&3
i=0
got=""
while [ "$i" -lt 40 ]; do
	got=$(grep -a '"event":"surface"' "$out" | tail -1)
	if echo "$got" | grep -a -q '"x":10'; then
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
echo "$got" | grep -a -q '"monitor":"DP-1"' || { echo "phase4: get mismatch" >&2; exit 1; }
echo "$got" | grep -a -q '"pinned":false' || { echo "phase4: pinned mismatch" >&2; exit 1; }

[ -f "$state/surfaces.jsonl" ] || { echo "phase4: missing surfaces.jsonl" >&2; exit 1; }
grep -q 'DP-1' "$state/surfaces.jsonl" || { echo "phase4: file content" >&2; exit 1; }

echo '{"op":"surface.set","conversation":"0","monitor":"HDMI-1","x":1,"y":2,"pinned":true}' >&3
sleep 0.2
echo '{"op":"surface.get","conversation":"0"}' >&3
i=0
while [ "$i" -lt 40 ]; do
	got=$(grep -a '"event":"surface"' "$out" | tail -1)
	if echo "$got" | grep -a -q '"pinned":true'; then
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
echo "$got" | grep -a -q '"monitor":"HDMI-1"' || { echo "phase4: update mismatch" >&2; exit 1; }

set +e
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" --hold
rc=$?
set -e
[ "$rc" -eq 2 ] || { echo "phase4: expected lock 2, got $rc" >&2; exit 1; }
if ! kill -0 "$pid" 2>/dev/null; then
	echo "phase4: helper died" >&2
	exit 1
fi

echo "phase4: ok"
exit 0
