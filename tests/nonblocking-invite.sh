#!/bin/sh
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
body=$(awk '
  /int omaq_tox_group_invite_friend\(/ { capture=1 }
  capture { print }
  capture && /^}/ { exit }
' "$root/helper/tox_adapt.c")
printf '%s\n' "$body" | grep -q 'return 1;'
if printf '%s\n' "$body" | grep -Eq 'tox_iterate|usleep|sleep\('; then
  echo "nonblocking-invite: invite adapter still blocks or re-enters tox" >&2
  exit 1
fi
grep -q 'retry_pending_native_group_invite();' "$root/helper/omaq.c"
grep -q 'g_group_invite_cleanup_pending = 1;' "$root/helper/omaq.c"
grep -q 'if (group_binding_forget_expect' "$root/helper/omaq.c"
echo "nonblocking-invite: ok"
