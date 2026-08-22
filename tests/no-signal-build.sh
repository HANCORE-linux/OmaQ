#!/bin/sh
# A helper without Signal must not be buildable: direct text must never fall back to plaintext.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
out=$(mktemp /tmp/omaq-no-signal-XXXXXX)
trap 'rm -f "$out"' EXIT
if make -C "$root" -B helper PKG_CONFIG=/bin/false >"$out" 2>&1; then
	echo "no-signal-build: helper unexpectedly built" >&2
	cat "$out" >&2
	exit 1
fi
grep -q 'libsignal-protocol-c is required' "$out"
echo "no-signal-build: ok"
