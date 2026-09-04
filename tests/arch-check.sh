#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-arch-check-XXXXXX)
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT HUP INT TERM

mkdir -p "$tmp/helper" "$tmp/scripts"
cp "$root/scripts/arch-check.sh" "$tmp/scripts/arch-check.sh"
for path in install.sh scripts/float-omaq.sh scripts/uninstall-omaq.sh \
  scripts/paste-image.sh scripts/update-helper.sh scripts/helper-runtime.py \
  scripts/install-omaq.sh scripts/install-omaq.py scripts/update-omaq.sh \
  scripts/update-omaq.py; do
  mkdir -p -- "$(dirname "$tmp/$path")"
  printf '#!/bin/sh\nexit 0\n' >"$tmp/$path"
  chmod 0755 "$tmp/$path"
done

printf '%s\n' 'void policy_without_io(void) {}' >"$tmp/helper/roles.c"
sh "$tmp/scripts/arch-check.sh" >/dev/null

cat >"$tmp/helper/roles.c" <<'EOF'
#include <stdio.h>
void audit_forbidden_io(void) {
  FILE *file = fopen ("/tmp/unused", "r");
  if (file)
    fclose(file);
}
EOF
set +e
output=$(sh "$tmp/scripts/arch-check.sh" 2>&1)
status=$?
set -e
if [ "$status" -ne 1 ] ||
   ! printf '%s\n' "$output" | grep -Fq 'helper/roles.c contains direct IO'; then
  printf '%s\n' "$output" >&2
  echo "arch-check: policy IO mutation passed" >&2
  exit 1
fi

echo "arch-check: ok"
