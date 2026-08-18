#!/bin/sh
# Two helpers, one home: exactly one keeps the lock. No network, no toxcore.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "lock-elect: missing helper/omaq" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
home=$(mktemp -d /tmp/omaq-lock-XXXXXX)
state=$(mktemp -d /tmp/omaq-state-XXXXXX)
cleanup() { kill "$pid" 2>/dev/null || true; rm -rf "$home" "$state"; }
trap cleanup EXIT

case "$home" in
"$real_home"|"$real_home"/*)
	echo "lock-elect: refused real home path" >&2
	exit 1
	;;
esac

export OMAQ_HOME="$home" OMAQ_STATE="$state"
"$bin" --hold &
pid=$!
sleep 0.15
set +e
"$bin" --hold
rc=$?
set -e
if [ "$rc" -ne 2 ]; then
	echo "lock-elect: expected exit 2, got $rc" >&2
	exit 1
fi
if ! kill -0 "$pid" 2>/dev/null; then
	echo "lock-elect: owner died" >&2
	exit 1
fi
echo "lock-elect: ok"
exit 0
