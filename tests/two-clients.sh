#!/bin/sh
# Two Unix-socket clients, one helper. No network required.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "two-clients: missing helper/omaq" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
home=$(mktemp -d /tmp/omaq-2c-XXXXXX)
state=$(mktemp -d /tmp/omaq-2cs-XXXXXX)
hold=$(mktemp -u /tmp/omaq-2cf-XXXXXX)
out=$(mktemp /tmp/omaq-2co-XXXXXX)
pid=""
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	exec 3>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	rm -rf "$home" "$state" "$hold" "$out" "$out.err"
}
trap cleanup EXIT

case "$home" in
"$real_home"|"$real_home"/*)
	echo "two-clients: refused real home" >&2
	exit 1
	;;
esac

mkfifo "$hold"
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"

ok=0
i=0
while [ "$i" -lt 40 ]; do
	if [ -S "$state/omaq.sock" ]; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
if [ "$ok" -ne 1 ]; then
	echo "two-clients: no sock" >&2
	exit 1
fi

python3 - "$state/omaq.sock" <<'PY'
import socket
import sys

path = sys.argv[1]

def client():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(2)
    s.connect(path)
    return s

def readline(s):
    buf = b""
    while b"\n" not in buf:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
    return buf.decode("utf-8", "replace")

a = client()
b = client()
a.sendall(b'{"op":"status"}\n')
ra = readline(a)
rb = readline(b)
if '"event"' not in ra:
    sys.stderr.write("two-clients: client a no event: %r\n" % ra)
    sys.exit(1)
if '"event"' not in rb:
    sys.stderr.write("two-clients: client b no event: %r\n" % rb)
    sys.exit(1)
PY

if ! kill -0 "$pid" 2>/dev/null; then
	echo "two-clients: helper died" >&2
	exit 1
fi
set +e
OMAQ_HOME="$home" OMAQ_STATE="$state" "$bin" --hold
rc=$?
set -e
if [ "$rc" -ne 2 ]; then
	echo "two-clients: expected exit 2, got $rc" >&2
	exit 1
fi
echo "two-clients: ok"
exit 0
