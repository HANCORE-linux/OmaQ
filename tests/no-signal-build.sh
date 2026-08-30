#!/bin/sh
# A helper without Signal must not be buildable: direct text must never fall back to plaintext.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
out=$(mktemp /tmp/omaq-no-signal-XXXXXX)
trap 'rm -f "$out"' EXIT

if make -C "$root" -B helper PKG_CONFIG=/bin/false >"$out" 2>&1; then
	echo "no-signal-build: helper unexpectedly built" >&2
	cat "$out" >&2
	exit 1
fi
grep -q 'libsignal-protocol-c is required' "$out"

: >"$out"
if "${CC:-cc}" -E -DHAVE_TOX -I"$root/helper" "$root/helper/omaq.c" \
	>"$out" 2>&1; then
	echo "no-signal-build: direct HAVE_TOX compile bypassed the source guard" >&2
	cat "$out" >&2
	exit 1
fi
grep -q 'OmaQ Tox transport requires Signal Ratchet support' "$out"

: >"$out"
if make -C "$root" -B tests/omaq_group_admin_test_helper \
	PKG_CONFIG=/bin/false >"$out" 2>&1; then
	echo "no-signal-build: group admin helper bypassed check-signal" >&2
	cat "$out" >&2
	exit 1
fi
grep -q 'libsignal-protocol-c is required' "$out"

echo "no-signal-build: ok"
