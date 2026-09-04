#!/bin/sh
set -eu

fail() {
  printf 'install.sh: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: ./install.sh [--section left|center|right] [--expect-commit COMMIT] --yes

Install OmaQ's package dependencies, then run the verified shell-off source
installer from this checkout. The manifest places OmaQ on the right when
--section is omitted.
EOF
}

section=""
section_seen=0
expected_commit=""
expected_seen=0
confirmed=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --section)
      [ "$section_seen" -eq 0 ] || fail "--section was specified more than once"
      [ "$#" -ge 2 ] || fail "--section requires left, center, or right"
      section=$2
      section_seen=1
      shift 2
      ;;
    --expect-commit)
      [ "$expected_seen" -eq 0 ] || fail "--expect-commit was specified more than once"
      [ "$#" -ge 2 ] || fail "--expect-commit requires a commit"
      expected_commit=$2
      expected_seen=1
      shift 2
      ;;
    --yes)
      [ "$confirmed" -eq 0 ] || fail "--yes was specified more than once"
      confirmed=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      [ "$#" -eq 0 ] || fail "positional arguments are not supported"
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

if [ "$section_seen" -eq 1 ] && [ -z "$section" ]; then
  fail "--section requires left, center, or right"
fi
case "$section" in
  ""|left|center|right) ;;
  *) fail "--section must be left, center, or right" ;;
esac

if [ "$expected_seen" -eq 1 ] && [ -z "$expected_commit" ]; then
  fail "--expect-commit requires a commit"
fi
if [ -n "$expected_commit" ]; then
  [ "${#expected_commit}" -eq 40 ] ||
    fail "--expect-commit requires a lowercase 40-hex commit"
  case "$expected_commit" in
    *[!0-9a-f]*) fail "--expect-commit requires a lowercase 40-hex commit" ;;
  esac
fi

[ "$confirmed" -eq 1 ] ||
  fail "refusing to install packages or stop the shell without --yes"

root=$(CDPATH='' cd -- "$(/usr/bin/dirname -- "$0")" && pwd -P)
installer="$root/scripts/install-omaq.sh"
[ -f "$installer" ] && [ -x "$installer" ] && [ ! -L "$installer" ] ||
  fail "verified source installer is unavailable: $installer"

if [ -n "$expected_commit" ] && [ -n "$section" ]; then
  "$installer" --preflight-only --expect-commit "$expected_commit" \
    --section "$section" --yes
elif [ -n "$expected_commit" ]; then
  "$installer" --preflight-only --expect-commit "$expected_commit" --yes
elif [ -n "$section" ]; then
  "$installer" --preflight-only --section "$section" --yes
else
  "$installer" --preflight-only --yes
fi

PATH=/usr/bin:/bin /usr/bin/omarchy pkg add \
  toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
  ttf-material-symbols-variable qrencode

if [ -n "$expected_commit" ] && [ -n "$section" ]; then
  exec "$installer" --expect-commit "$expected_commit" \
    --section "$section" --yes
elif [ -n "$expected_commit" ]; then
  exec "$installer" --expect-commit "$expected_commit" --yes
elif [ -n "$section" ]; then
  exec "$installer" --section "$section" --yes
else
  exec "$installer" --yes
fi
