#!/bin/sh
set -eu
binary=${1:-helper/omaq}

readelf -h "$binary" | grep -Eq 'Type:[[:space:]]+DYN'
readelf -l "$binary" | grep -q 'GNU_RELRO'
readelf -d "$binary" | grep -Eq '\(FLAGS\).*BIND_NOW|\(FLAGS_1\).*NOW'
if command -v nm >/dev/null 2>&1; then
  nm -D "$binary" | grep -q '__stack_chk_fail'
fi
echo "helper-hardening: ok"
