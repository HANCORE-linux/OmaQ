#!/bin/sh
# Phase 5: import refuses without replace; replace on temp home; search hits.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase5: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p5a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p5as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p5b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p5bs-XXXXXX)
exp=$(mktemp /tmp/omaq-p5-XXXXXX.save)
out=$(mktemp /tmp/omaq-p5o-XXXXXX)
hold=$(mktemp -u /tmp/omaq-p5f-XXXXXX)
pid=""
cleanup() {
	exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$exp" "$out" "$hold" "$out.err"
}
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase5: refused real home" >&2; exit 1 ;;
esac

mkfifo "$hold"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
sleep 0.4

echo '{"op":"status"}' >&3
sleep 0.2
addr=$(grep -a '"addr"' "$out" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ -n "$addr" ] || { echo "phase5: no addr" >&2; exit 1; }

printf '{"op":"identity.export","path":"%s"}\n' "$exp" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"export"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] && [ -f "$exp" ] || { echo "phase5: export failed" >&2; exit 1; }

printf '{"op":"identity.import","path":"%s"}\n' "$exp" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q 'identity_exists' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase5: expected identity_exists" >&2; exit 1; }

# Second home creates a different identity, then we replace A's save with B's via import.
holdb=$(mktemp -u /tmp/omaq-p5fb-XXXXXX)
outb=$(mktemp /tmp/omaq-p5ob-XXXXXX)
mkfifo "$holdb"
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >"$outb" 2>"$outb.err" <"$holdb" &
pidb=$!
exec 4>"$holdb"
sleep 0.4
echo '{"op":"status"}' >&4
sleep 0.2
addrb=$(grep -a '"addr"' "$outb" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ -n "$addrb" ] || { echo "phase5: no addr b" >&2; exit 1; }
expb=$(mktemp /tmp/omaq-p5b-XXXXXX.save)
printf '{"op":"identity.export","path":"%s"}\n' "$expb" >&4
sleep 0.3
[ -f "$expb" ] || { echo "phase5: b export" >&2; exit 1; }
kill "$pidb" 2>/dev/null || true
pidb=""
exec 4>&-

printf '{"op":"identity.import","path":"%s","replace":true}\n' "$expb" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"import"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase5: replace import failed" >&2; exit 1; }

# Search fixture on disk (no need for a live message).
python3 - "$ha" <<'PY'
import os, sys
home = sys.argv[1]
d = os.path.join(home, "history", "c1")
os.makedirs(d, 0o700)
p = os.path.join(d, "messages.jsonl")
open(p, "w").write('{"text":"needle-unique-xyz"}\n{"text":"other"}\n')
os.chmod(p, 0o600)
PY
echo '{"op":"search","conversation":"c1","text":"needle-unique"}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q 'needle-unique-xyz' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase5: search miss" >&2; cat "$out" >&2; exit 1; }

kill "$pid" 2>/dev/null || true
pid=""
exec 3>&-
# Reload A with replaced save — addr must match B.
hold2=$(mktemp -u /tmp/omaq-p5f2-XXXXXX)
out2=$(mktemp /tmp/omaq-p5o2-XXXXXX)
mkfifo "$hold2"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out2" 2>"$out2.err" <"$hold2" &
pid=$!
exec 3>"$hold2"
sleep 0.5
echo '{"op":"status"}' >&3
sleep 0.3
addr2=$(grep -a '"addr"' "$out2" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ "$addr2" = "$addrb" ] || { echo "phase5: replace did not change identity" >&2; exit 1; }
[ "$addr2" != "$addr" ] || { echo "phase5: addrs should differ" >&2; exit 1; }

echo "phase5: ok"
exit 0
