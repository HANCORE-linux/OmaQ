#!/bin/sh
# This mock verifies the documented command surface and install ordering.
# Shell lifecycle, supervisor races, and atomic exchange use source-update.py.
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-update-order-XXXXXX)
cleanup() {
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

extract_block() {
  heading=$1
  document=$2
  awk -v heading="$heading" '
    $0 == heading { in_section = 1; next }
    in_section && $0 == "```bash" { in_block = 1; next }
    in_block && $0 == "```" { exit }
    in_block { print }
  ' "$document"
}

extract_block "## Install" "$root/README.md" >"$tmp/readme-install.sh"
extract_block "## Install OmaQ" "$root/docs/INSTALLATION.md" >"$tmp/installation-install.sh"
[ -s "$tmp/readme-install.sh" ] || {
  echo "update-order: README install command is missing" >&2
  exit 1
}
cmp -s "$tmp/readme-install.sh" "$tmp/installation-install.sh" || {
  echo "update-order: documented install commands differ" >&2
  exit 1
}

extract_block "## Update" "$root/README.md" >"$tmp/readme-update.sh"
extract_block "## Update OmaQ" "$root/docs/INSTALLATION.md" >"$tmp/installation-update.sh"
[ -s "$tmp/readme-update.sh" ] || {
  echo "update-order: README update command is missing" >&2
  exit 1
}
cmp -s "$tmp/readme-update.sh" "$tmp/installation-update.sh" || {
  echo "update-order: documented primary update commands differ" >&2
  exit 1
}

extract_block "### Recover from an update failure" \
  "$root/docs/INSTALLATION.md" >"$tmp/rollback.sh"
[ -s "$tmp/rollback.sh" ] || {
  echo "update-order: helper rollback command is missing" >&2
  exit 1
}

mkdir -p "$tmp/bin" "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts"
cat >"$tmp/bin/omarchy" <<'EOF'
#!/bin/sh
set -eu
case "$*" in
  "pkg add toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode")
    printf 'packages\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_PACKAGE_STATUS:-0}"
    ;;
  *)
    printf 'unexpected omarchy command: %s\n' "$*" >&2
    exit 99
    ;;
esac
EOF
cat >"$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$OMAQ_UPDATE_TEST_LOG"
exit "${OMAQ_UPDATE_STATUS:-0}"
EOF
chmod 755 "$tmp/bin/omarchy" \
  "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh"

run_install_case() {
  name=$1
  package_status=$2
  expected_status=$3
  expected_log=$4
  log="$tmp/install-$name.log"
  : >"$log"

  if HOME="$tmp/home" PATH="$tmp/bin:$PATH" \
      OMAQ_INSTALL_TEST_LOG="$log" \
      OMAQ_PACKAGE_STATUS="$package_status" \
      bash "$tmp/readme-install.sh"; then
    actual_status=0
  else
    actual_status=$?
  fi

  [ "$actual_status" -eq "$expected_status" ] || {
    printf 'update-order: install %s returned %s, expected %s\n' \
      "$name" "$actual_status" "$expected_status" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/install-$name.expected"
  cmp -s "$log" "$tmp/install-$name.expected" || {
    printf 'update-order: install %s order mismatch\n' "$name" >&2
    exit 1
  }
}

run_install_case success 0 0 'packages\n'
run_install_case package-failure 21 21 'packages\n'

run_update_case() {
  name=$1
  status=$2
  expected=$3
  log="$tmp/update-$name.log"
  : >"$log"
  if HOME="$tmp/home" PATH="$tmp/bin:$PATH" \
      OMAQ_UPDATE_TEST_LOG="$log" OMAQ_UPDATE_STATUS="$status" \
      bash "$tmp/readme-update.sh"; then
    actual=0
  else
    actual=$?
  fi
  [ "$actual" -eq "$expected" ] || {
    printf 'update-order: update %s returned %s, expected %s\n' \
      "$name" "$actual" "$expected" >&2
    exit 1
  }
  [ "$(cat "$log")" = "--yes" ] || {
    echo "update-order: primary updater arguments changed" >&2
    exit 1
  }
}

run_update_case success 0 0
run_update_case failure 31 31

rollback_log="$tmp/rollback.log"
: >"$rollback_log"
HOME="$tmp/home" PATH="$tmp/bin:$PATH" OMAQ_UPDATE_TEST_LOG="$rollback_log" \
  OMAQ_UPDATE_STATUS=0 bash "$tmp/rollback.sh"
[ "$(cat "$rollback_log")" = "--rollback-helper --yes" ] || {
  echo "update-order: helper rollback does not use the shell-off updater" >&2
  exit 1
}

for document in "$root/README.md" "$root/docs/INSTALLATION.md"; do
  if grep -Fq 'Known host-reload risk' "$document"; then
    echo "update-order: obsolete host-reload warning remains" >&2
    exit 1
  fi
  if grep -Fq 'omarchy plugin update hancore.omaq' "$document"; then
    echo "update-order: documented update still writes through the active checkout" >&2
    exit 1
  fi
  if grep -Fq "omarchy plugin add \\" "$document"; then
    echo "update-order: documented install still exposes the tree before build" >&2
    exit 1
  fi
done

grep -Fq 'mv -T --exchange --no-copy' "$root/docs/PLAN.md" || {
  echo "update-order: architecture omits the atomic no-copy exchange" >&2
  exit 1
}
grep -Fq 'omarchy-launch-shell' "$root/docs/PLAN.md" || {
  echo "update-order: architecture omits the shell supervisor" >&2
  exit 1
}
grep -Fq 'update-pending: old helper, new tree' "$root/docs/PLAN.md" || {
  echo "update-order: architecture omits the mixed helper state" >&2
  exit 1
}
grep -Fq 'shell-off source updates' "$root/docs/USER-GUIDE.md" || {
  echo "update-order: user guide omits the shell-off workflow" >&2
  exit 1
}
grep -Fq 'cooperative same-user boundary' "$root/docs/SECURITY.md" || {
  echo "update-order: security guide omits the concurrent-restart boundary" >&2
  exit 1
}
grep -Fq 'Bootstrap an older installation' "$root/docs/INSTALLATION.md" || {
  echo "update-order: installation guide omits the external bootstrap" >&2
  exit 1
}
[ -x "$root/scripts/update-omaq.sh" ] && [ -x "$root/scripts/update-omaq.py" ] || {
  echo "update-order: source updater is not executable" >&2
  exit 1
}
[ -x "$root/scripts/install-omaq.sh" ] && [ -x "$root/scripts/install-omaq.py" ] || {
  echo "update-order: source installer is not executable" >&2
  exit 1
}
if ! grep -Fq 'mode=install' "$root/docs/INSTALLATION.md" ||
    ! grep -Fq 'mode=update' "$root/docs/INSTALLATION.md"; then
  echo "update-order: shared source bootstrap modes are missing" >&2
  exit 1
fi

printf 'update-order: ok\n'
