#!/bin/sh
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
mkdir -m 700 "$tmp/bin" "$tmp/staging"
cat >"$tmp/bin/wl-paste" <<'SH'
#!/bin/sh
[ "$1" = "--type" ] || exit 2
cat -- "$OMAQ_TEST_CLIPBOARD"
SH
chmod 755 "$tmp/bin/wl-paste"
printf '\211PNG\r\n\032\nfixture' >"$tmp/source"
: >"$tmp/staging/image"
chmod 600 "$tmp/staging/image"
PATH="$tmp/bin:$PATH" OMAQ_TEST_CLIPBOARD="$tmp/source" \
  "$root/scripts/paste-image.sh" image/png "$tmp/staging/image"
cmp "$tmp/source" "$tmp/staging/image"

if PATH="$tmp/bin:$PATH" OMAQ_TEST_CLIPBOARD="$tmp/source" \
    "$root/scripts/paste-image.sh" image/gif "$tmp/staging/image" >/dev/null 2>&1; then
  echo "paste-image: unsupported MIME accepted" >&2
  exit 1
fi
ln -s "$tmp/source" "$tmp/staging/link"
if PATH="$tmp/bin:$PATH" OMAQ_TEST_CLIPBOARD="$tmp/source" \
    "$root/scripts/paste-image.sh" image/png "$tmp/staging/link" >/dev/null 2>&1; then
  echo "paste-image: symlink destination accepted" >&2
  exit 1
fi

dd if=/dev/zero of="$tmp/large" bs=1048576 count=9 status=none
: >"$tmp/staging/large"
chmod 600 "$tmp/staging/large"
if PATH="$tmp/bin:$PATH" OMAQ_TEST_CLIPBOARD="$tmp/large" \
    "$root/scripts/paste-image.sh" image/webp "$tmp/staging/large" >/dev/null 2>&1; then
  echo "paste-image: oversized clipboard accepted" >&2
  exit 1
fi
size=$(stat -Lc '%s' -- "$tmp/staging/large")
[ "$size" -le 8388609 ] || {
  echo "paste-image: oversized clipboard write was not bounded" >&2
  exit 1
}

echo "paste-image: ok"
