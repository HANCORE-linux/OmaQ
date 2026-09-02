#!/bin/sh
# This mock verifies documented install and update order plus exit status only.
# It cannot exercise the Quickshell plugin-loader lifecycle or prove that
# monitored writes are safe.
set -eu

root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d /tmp/omaq-update-order-XXXXXX)
cleanup() {
  rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

extract_update_block() {
  heading=$1
  document=$2
  awk -v heading="$heading" '
    $0 == heading { in_section = 1; next }
    in_section && $0 == "```bash" { in_block = 1; next }
    in_block && $0 == "```" { exit }
    in_block { print }
  ' "$document"
}

extract_update_block "## Install" "$root/README.md" >"$tmp/readme-install.sh"
extract_update_block "## Install OmaQ" "$root/docs/INSTALLATION.md" >"$tmp/installation-install.sh"
[ -s "$tmp/readme-install.sh" ] || {
  echo "update-order: README install command is missing" >&2
  exit 1
}
cmp -s "$tmp/readme-install.sh" "$tmp/installation-install.sh" || {
  echo "update-order: documented install commands differ" >&2
  exit 1
}

extract_update_block "## Update" "$root/README.md" >"$tmp/readme-update.sh"
extract_update_block "## Update OmaQ" "$root/docs/INSTALLATION.md" >"$tmp/installation-update.sh"
[ -s "$tmp/readme-update.sh" ] || {
  echo "update-order: README update command is missing" >&2
  exit 1
}
cmp -s "$tmp/readme-update.sh" "$tmp/installation-update.sh" || {
  echo "update-order: documented update commands differ" >&2
  exit 1
}
extract_update_block "### Recover from a degraded activation" \
  "$root/docs/INSTALLATION.md" >"$tmp/rollback.sh"
[ -s "$tmp/rollback.sh" ] || {
  echo "update-order: rollback command is missing" >&2
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
  "plugin add https://github.com/HANCORE-linux/OmaQ.git --yes")
    printf 'add-disabled\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_ADD_STATUS:-0}"
    ;;
  "plugin enable hancore.omaq")
    printf 'enable\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_ENABLE_STATUS:-0}"
    ;;
  "plugin update hancore.omaq --yes")
    printf 'source\n' >>"$OMAQ_UPDATE_TEST_LOG"
    exit "${OMAQ_SOURCE_STATUS:-0}"
    ;;
  "restart shell")
    printf 'restart\n' >>"$OMAQ_UPDATE_TEST_LOG"
    exit "${OMAQ_RESTART_STATUS:-0}"
    ;;
  *)
    printf 'unexpected omarchy command: %s\n' "$*" >&2
    exit 99
    ;;
esac
EOF
cat >"$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh" <<'EOF'
#!/bin/sh
set -eu
case "$*" in
  --activate)
    printf 'helper\n' >>"$OMAQ_UPDATE_TEST_LOG"
    exit "${OMAQ_HELPER_STATUS:-0}"
    ;;
  --rollback)
    printf 'rollback\n' >>"$OMAQ_UPDATE_TEST_LOG"
    exit "${OMAQ_ROLLBACK_STATUS:-0}"
    ;;
  --status)
    printf 'status\n' >>"$OMAQ_UPDATE_TEST_LOG"
    exit "${OMAQ_STATUS_STATUS:-0}"
    ;;
  *)
    printf 'unexpected helper command: %s\n' "$*" >&2
    exit 98
    ;;
esac
EOF
cat >"$tmp/bin/make" <<'EOF'
#!/bin/sh
set -eu
[ "$*" = "-C $HOME/.config/omarchy/plugins/hancore.omaq helper" ] || {
  printf 'unexpected make command: %s\n' "$*" >&2
  exit 97
}
printf 'helper-build\n' >>"$OMAQ_INSTALL_TEST_LOG"
exit "${OMAQ_BUILD_STATUS:-0}"
EOF
chmod 755 "$tmp/bin/omarchy" "$tmp/bin/make" \
  "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-helper.sh"

run_install_case() {
  name=$1
  package_status=$2
  add_status=$3
  build_status=$4
  enable_status=$5
  expected_status=$6
  expected_log=$7
  log="$tmp/install-$name.log"
  : >"$log"

  if HOME="$tmp/home" PATH="$tmp/bin:$PATH" \
      OMAQ_INSTALL_TEST_LOG="$log" \
      OMAQ_PACKAGE_STATUS="$package_status" \
      OMAQ_ADD_STATUS="$add_status" \
      OMAQ_BUILD_STATUS="$build_status" \
      OMAQ_ENABLE_STATUS="$enable_status" \
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
    printf 'update-order: install %s order mismatch\nexpected:\n' "$name" >&2
    cat "$tmp/install-$name.expected" >&2
    printf 'actual:\n' >&2
    cat "$log" >&2
    exit 1
  }
}

run_install_case success 0 0 0 0 0 \
  'packages\nadd-disabled\nhelper-build\nenable\n'
run_install_case package-failure 21 0 0 0 21 'packages\n'
run_install_case add-failure 0 22 0 0 22 'packages\nadd-disabled\n'
run_install_case build-failure 0 0 23 0 23 \
  'packages\nadd-disabled\nhelper-build\n'
run_install_case enable-failure 0 0 0 24 24 \
  'packages\nadd-disabled\nhelper-build\nenable\n'

run_case() {
  name=$1
  source_status=$2
  helper_status=$3
  restart_status=$4
  expected_status=$5
  expected_log=$6
  log="$tmp/$name.log"
  : >"$log"

  if HOME="$tmp/home" PATH="$tmp/bin:$PATH" \
      OMAQ_UPDATE_TEST_LOG="$log" \
      OMAQ_SOURCE_STATUS="$source_status" \
      OMAQ_HELPER_STATUS="$helper_status" \
      OMAQ_RESTART_STATUS="$restart_status" \
      bash "$tmp/readme-update.sh"; then
    actual_status=0
  else
    actual_status=$?
  fi

  [ "$actual_status" -eq "$expected_status" ] || {
    printf 'update-order: %s returned %s, expected %s\n' \
      "$name" "$actual_status" "$expected_status" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/$name.expected"
  cmp -s "$log" "$tmp/$name.expected" || {
    printf 'update-order: %s order mismatch\nexpected:\n' "$name" >&2
    cat "$tmp/$name.expected" >&2
    printf 'actual:\n' >&2
    cat "$log" >&2
    exit 1
  }
}

run_case success 0 0 0 0 'source\nhelper\nrestart\n'
run_case source-failure 23 0 0 23 'source\nrestart\n'
run_case helper-failure 0 24 0 24 'source\nhelper\nrestart\n'
run_case restart-failure 0 0 25 25 'source\nhelper\nrestart\n'
run_case operation-and-restart-failure 0 26 27 26 'source\nhelper\nrestart\n'
run_case source-and-restart-failure 28 0 29 28 'source\nrestart\n'

run_rollback_case() {
  name=$1
  rollback_status=$2
  restart_status=$3
  expected_status=$4
  expected_log=$5
  log="$tmp/$name.log"
  : >"$log"

  if HOME="$tmp/home" PATH="$tmp/bin:$PATH" \
      OMAQ_UPDATE_TEST_LOG="$log" \
      OMAQ_ROLLBACK_STATUS="$rollback_status" \
      OMAQ_RESTART_STATUS="$restart_status" \
      OMAQ_STATUS_STATUS="${status_status:-0}" \
      bash "$tmp/rollback.sh"; then
    actual_status=0
  else
    actual_status=$?
  fi

  [ "$actual_status" -eq "$expected_status" ] || {
    printf 'update-order: %s returned %s, expected %s\n' \
      "$name" "$actual_status" "$expected_status" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/$name.expected"
  cmp -s "$log" "$tmp/$name.expected" || {
    printf 'update-order: %s order mismatch\nexpected:\n' "$name" >&2
    cat "$tmp/$name.expected" >&2
    printf 'actual:\n' >&2
    cat "$log" >&2
    exit 1
  }
}

run_rollback_case rollback-success 0 0 0 'rollback\nrestart\nstatus\n'
run_rollback_case rollback-failure 31 0 31 'rollback\nrestart\nstatus\n'
run_rollback_case rollback-restart-failure 0 32 32 'rollback\nrestart\nstatus\n'
run_rollback_case rollback-and-restart-failure 33 34 33 'rollback\nrestart\nstatus\n'
status_status=35
run_rollback_case rollback-status-failure 0 0 35 'rollback\nrestart\nstatus\n'
unset status_status

grep -Fq "must then attempt exactly one complete \`omarchy restart shell\`" \
  "$root/docs/PLAN.md" || {
  echo "update-order: architecture contract does not require the final restart" >&2
  exit 1
}
grep -Fq 'also attempts that restart on failed update paths' "$root/docs/USER-GUIDE.md" || {
  echo "update-order: user guide omits failed update paths" >&2
  exit 1
}
for document in "$root/README.md" "$root/docs/INSTALLATION.md"; do
  grep -Fq 'Known host-reload risk' "$document" || {
    echo "update-order: documented command omits the known host-reload risk" >&2
    exit 1
  }
  grep -Fiq 'final restart guard does not prevent' "$document" || {
    echo "update-order: documented warning overstates the restart guard" >&2
    exit 1
  }
done

printf 'update-order: ok\n'
