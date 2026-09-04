#!/bin/sh
# Verify the documented lifecycle command surface and root installer ordering.
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
# shellcheck disable=SC2088
printf '%s\n' \
  'omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --yes &&' \
  '~/.config/omarchy/plugins/hancore.omaq/install.sh --yes' \
  >"$tmp/install.expected"
cmp -s "$tmp/readme-install.sh" "$tmp/install.expected" || {
  echo "update-order: primary install is not the normal two-step Omarchy command" >&2
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

mkdir -p "$tmp/bin" "$tmp/home/.config/omarchy/plugins"
cat >"$tmp/bin/omarchy" <<'EOF'
#!/bin/sh
set -eu
[ "$*" = "plugin add https://github.com/HANCORE-linux/OmaQ.git --yes" ] || {
  printf 'unexpected omarchy command: %s\n' "$*" >&2
  exit 99
}
printf 'plugin-add\n' >>"$OMAQ_INSTALL_TEST_LOG"
[ "${OMAQ_PLUGIN_STATUS:-0}" -eq 0 ] || exit "$OMAQ_PLUGIN_STATUS"
target="$HOME/.config/omarchy/plugins/hancore.omaq"
mkdir -p "$target"
cat >"$target/install.sh" <<'INSTALL'
#!/bin/sh
[ "$*" = "--yes" ] || exit 97
printf 'install\n' >>"$OMAQ_INSTALL_TEST_LOG"
exit "${OMAQ_INSTALL_STATUS:-0}"
INSTALL
chmod 755 "$target/install.sh"
EOF
chmod 755 "$tmp/bin/omarchy"
test_path="$tmp/bin:/usr/bin:/bin"

run_documented_install() {
  shell_name=$1
  shell_path=$2
  name=$3
  plugin_status=$4
  install_status=$5
  expected_status=$6
  expected_log=$7
  log="$tmp/documented-$shell_name-$name.log"
  rm -rf "$tmp/home/.config/omarchy/plugins/hancore.omaq"
  : >"$log"
  if [ "$shell_name" = fish ]; then
    if HOME="$tmp/home" PATH="$test_path" OMAQ_INSTALL_TEST_LOG="$log" \
        OMAQ_PLUGIN_STATUS="$plugin_status" OMAQ_INSTALL_STATUS="$install_status" \
        "$shell_path" --no-config "$tmp/readme-install.sh"; then
      actual=0
    else
      actual=$?
    fi
  elif HOME="$tmp/home" PATH="$test_path" OMAQ_INSTALL_TEST_LOG="$log" \
      OMAQ_PLUGIN_STATUS="$plugin_status" OMAQ_INSTALL_STATUS="$install_status" \
      "$shell_path" "$tmp/readme-install.sh"; then
    actual=0
  else
    actual=$?
  fi
  [ "$actual" -eq "$expected_status" ] || {
    echo "update-order: documented $name returned $actual" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/documented.expected"
  cmp -s "$log" "$tmp/documented.expected" || {
    echo "update-order: documented $name ordering changed" >&2
    exit 1
  }
}

run_documented_suite() {
  shell_name=$1
  shell_path=$2
  run_documented_install "$shell_name" "$shell_path" success 0 0 0 \
    'plugin-add\ninstall\n'
  run_documented_install "$shell_name" "$shell_path" plugin-failure 21 0 21 \
    'plugin-add\n'
  run_documented_install "$shell_name" "$shell_path" install-failure 0 22 22 \
    'plugin-add\ninstall\n'
}
run_documented_suite bash /usr/bin/bash
if [ -x /usr/bin/fish ]; then
  run_documented_suite fish /usr/bin/fish
fi

live_home="$tmp/live-home"
live="$live_home/.config/omarchy/plugins/hancore.omaq"
mkdir -p "$live/scripts" "$tmp/live-bin"
sed -e "s#/usr/bin/omarchy#$tmp/live-bin/omarchy#g" \
  -e "s#/usr/bin/git#$tmp/live-bin/git#g" \
  -e "s#/usr/bin/make#$tmp/live-bin/make#g" \
  -e "s#/usr/bin/sleep#$tmp/live-bin/sleep#g" \
  "$root/install.sh" >"$live/install.sh"
cat >"$live/scripts/helper-runtime.py" <<'EOF'
#!/usr/bin/python3
import json, os
with open(os.environ["OMAQ_INSTALL_TEST_LOG"], "a", encoding="ascii") as output:
    output.write("verify\n")
print(json.dumps({"state":"current", "available_sha256":"a" * 64,
                  "running_sha256":"a" * 64}))
EOF
cat >"$tmp/live-bin/omarchy" <<'EOF'
#!/bin/sh
case "$*" in
  plugin\ validate\ *)
    printf 'validate\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_VALIDATE_STATUS:-0}"
    ;;
  "plugin list --json")
    printf 'plugin-state\n' >>"$OMAQ_INSTALL_TEST_LOG"
    printf '[{"id":"hancore.omaq","enabled":%s,"firstParty":false,"kinds":["bar-widget"]}]\n' \
      "${OMAQ_PLUGIN_ENABLED:-false}"
    ;;
  "pkg add toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode")
    printf 'packages\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_PACKAGE_STATUS:-0}"
    ;;
  "plugin enable hancore.omaq"|"plugin enable hancore.omaq --section left")
    printf 'enable\n' >>"$OMAQ_INSTALL_TEST_LOG"
    case "${OMAQ_ENABLE_RESULT:-0}" in
      transient)
        printf 'omarchy-shell is not responding\n' >&2
        exit 1
        ;;
      no-newline)
        printf 'omarchy-shell is not responding' >&2
        exit 1
        ;;
      extra-stdout)
        printf 'unexpected\n'
        printf 'omarchy-shell is not responding\n' >&2
        exit 1
        ;;
      *) exit "${OMAQ_ENABLE_RESULT:-0}" ;;
    esac
    ;;
  "restart shell")
    printf 'restart\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_RESTART_STATUS:-0}"
    ;;
  *) exit 95 ;;
esac
EOF
cat >"$tmp/live-bin/omarchy-shell" <<'EOF'
#!/bin/sh
[ "$*" = "hancore.omaq status" ] || exit 96
[ "${OMARCHY_SHELL_IPC_TIMEOUT:-}" = "0.2s" ] || exit 98
count=0
[ ! -f "$OMAQ_IPC_COUNTER" ] || count=$(cat "$OMAQ_IPC_COUNTER")
count=$((count + 1))
printf '%s\n' "$count" >"$OMAQ_IPC_COUNTER"
case "${OMAQ_IPC_MODE:-success}" in
  failure) exit 1 ;;
  delayed) [ "$count" -gt 3 ] || exit 1 ;;
  success) ;;
  *) exit 97 ;;
esac
printf 'ipc-ready\n' >>"$OMAQ_INSTALL_TEST_LOG"
printf 'ready\n'
EOF
cat >"$tmp/live-bin/sleep" <<'EOF'
#!/bin/sh
[ "$*" = "0.1" ] || exit 98
count=0
[ ! -f "$OMAQ_SLEEP_COUNTER" ] || count=$(cat "$OMAQ_SLEEP_COUNTER")
printf '%s\n' "$((count + 1))" >"$OMAQ_SLEEP_COUNTER"
EOF
cat >"$tmp/live-bin/git" <<'EOF'
#!/bin/sh
[ "$3 $4 $5" = "rev-parse --verify HEAD" ] || exit 94
printf '%040d\n' 0
EOF
cat >"$tmp/live-bin/make" <<'EOF'
#!/bin/sh
printf 'build\n' >>"$OMAQ_INSTALL_TEST_LOG"
exit "${OMAQ_BUILD_STATUS:-0}"
EOF
chmod 755 "$live/install.sh" "$live/scripts/helper-runtime.py" \
  "$tmp/live-bin/omarchy" "$tmp/live-bin/omarchy-shell" \
  "$tmp/live-bin/git" "$tmp/live-bin/make" "$tmp/live-bin/sleep"
mkdir "$tmp/python-shadow"
cat >"$tmp/python-shadow/json.py" <<'EOF'
raise RuntimeError("readiness parser imported json.py from the working directory")
EOF

run_live_case() {
  name=$1
  validate=$2
  package=$3
  build=$4
  enable=$5
  restart=$6
  expected_status=$7
  expected_log=$8
  plugin_enabled=${9:-false}
  ipc_mode=${10:-success}
  expected_ipc_calls=${11:-}
  expected_sleep_calls=${12:-}
  log="$tmp/live-$name.log"
  ipc_counter="$tmp/live-$name.ipc-counter"
  sleep_counter="$tmp/live-$name.sleep-counter"
  : >"$log"
  rm -f "$ipc_counter" "$sleep_counter"
  if (
    cd "$tmp/python-shadow"
    HOME="$live_home" PYTHONPATH="$tmp/python-shadow" \
      OMAQ_INSTALL_TEST_LOG="$log" OMAQ_VALIDATE_STATUS="$validate" \
      OMAQ_PACKAGE_STATUS="$package" OMAQ_BUILD_STATUS="$build" \
      OMAQ_ENABLE_RESULT="$enable" OMAQ_RESTART_STATUS="$restart" \
      OMAQ_PLUGIN_ENABLED="$plugin_enabled" OMAQ_IPC_MODE="$ipc_mode" \
      OMAQ_IPC_COUNTER="$ipc_counter" OMAQ_SLEEP_COUNTER="$sleep_counter" \
      "$live/install.sh" --yes
  ) >"$tmp/live-$name.out" 2>"$tmp/live-$name.err"; then
    actual=0
  else
    actual=$?
  fi
  [ "$actual" -eq "$expected_status" ] || {
    echo "update-order: live $name returned $actual, expected $expected_status" >&2
    cat "$tmp/live-$name.err" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/live.expected"
  cmp -s "$log" "$tmp/live.expected" || {
    echo "update-order: live $name ordering changed" >&2
    cat "$log" >&2
    exit 1
  }
  if [ -n "$expected_ipc_calls" ]; then
    [ -f "$ipc_counter" ] &&
      [ "$(cat "$ipc_counter")" -eq "$expected_ipc_calls" ] || {
      echo "update-order: live $name IPC readiness count changed" >&2
      exit 1
    }
  fi
  if [ -n "$expected_sleep_calls" ]; then
    actual_sleep_calls=0
    [ ! -f "$sleep_counter" ] || actual_sleep_calls=$(cat "$sleep_counter")
    [ "$actual_sleep_calls" -eq "$expected_sleep_calls" ] || {
      echo "update-order: live $name readiness delay count changed" >&2
      exit 1
    }
  fi
}
run_live_case success 0 0 0 0 26 0 \
  'validate\nplugin-state\npackages\nbuild\nenable\nipc-ready\nverify\n' \
  false success 1 0
run_live_case transient-enable 0 0 0 transient 26 0 \
  'validate\nplugin-state\npackages\nbuild\nenable\nipc-ready\nverify\n' \
  false success 1 0
grep -Fxq 'plugin enable response was interrupted; verifying reactive activation' \
  "$tmp/live-transient-enable.out" || {
  echo "update-order: transient enable message is stale" >&2
  exit 1
}
! grep -Fq 'verifying after restart' "$tmp/live-transient-enable.out" || {
  echo "update-order: transient enable still promises a restart" >&2
  exit 1
}
run_live_case delayed-ipc 0 0 0 0 26 0 \
  'validate\nplugin-state\npackages\nbuild\nenable\nipc-ready\nverify\n' \
  false delayed 4 3
run_live_case ipc-timeout 0 0 0 0 0 1 \
  'validate\nplugin-state\npackages\nbuild\nenable\nverify\n' \
  false failure 200 200
run_live_case validation-failure 23 0 0 0 0 23 'validate\n'
run_live_case enabled-before-build 0 0 0 0 0 1 \
  'validate\nplugin-state\n' true
run_live_case package-failure 0 24 0 0 0 24 \
  'validate\nplugin-state\npackages\n'
run_live_case build-failure 0 0 25 0 0 25 \
  'validate\nplugin-state\npackages\nbuild\n'
run_live_case enable-failure 0 0 0 27 0 1 \
  'validate\nplugin-state\npackages\nbuild\nenable\n'
run_live_case enable-no-newline 0 0 0 no-newline 0 1 \
  'validate\nplugin-state\npackages\nbuild\nenable\n'
run_live_case enable-extra-stdout 0 0 0 extra-stdout 0 1 \
  'validate\nplugin-state\npackages\nbuild\nenable\n'

wrapper="$tmp/wrapper"
mkdir -p "$wrapper/scripts" "$wrapper/bin"
sed "s#/usr/bin/omarchy#$wrapper/bin/omarchy#g" \
  "$root/install.sh" >"$wrapper/install.sh"
cat >"$wrapper/scripts/install-omaq.sh" <<'EOF'
#!/bin/sh
case "${1:-}" in
  --preflight-only)
    printf 'preflight=%s\n' "$*" >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_PREFLIGHT_STATUS:-0}"
    ;;
  *)
    printf 'install=%s\n' "$*" >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_INSTALLER_STATUS:-0}"
    ;;
esac
EOF
cat >"$wrapper/bin/omarchy" <<'EOF'
#!/bin/sh
[ "$*" = "pkg add toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode" ] || exit 96
printf 'packages\n' >>"$OMAQ_INSTALL_TEST_LOG"
exit "${OMAQ_PACKAGE_STATUS:-0}"
EOF
chmod 755 "$wrapper/install.sh" "$wrapper/scripts/install-omaq.sh" \
  "$wrapper/bin/omarchy"
wrapper_log="$tmp/wrapper.log"
: >"$wrapper_log"
OMAQ_INSTALL_TEST_LOG="$wrapper_log" OMAQ_PACKAGE_STATUS=0 \
  "$wrapper/install.sh" --expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
  --section left --yes
printf '%s\n' \
  'preflight=--preflight-only --expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa --section left --yes' \
  'packages' \
  'install=--expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa --section left --yes' \
  >"$tmp/wrapper.expected"
cmp -s "$wrapper_log" "$tmp/wrapper.expected" || {
  echo "update-order: external installer did not forward validated arguments" >&2
  exit 1
}

reject_wrapper_arguments() {
  : >"$wrapper_log"
  if OMAQ_INSTALL_TEST_LOG="$wrapper_log" "$wrapper/install.sh" "$@" \
      >"$tmp/wrapper.stdout" 2>"$tmp/wrapper.stderr"; then
    echo "update-order: root installer accepted invalid arguments: $*" >&2
    exit 1
  fi
  [ ! -s "$wrapper_log" ] || {
    echo "update-order: root installer changed state before rejecting arguments" >&2
    exit 1
  }
}
reject_wrapper_arguments --section middle --yes
reject_wrapper_arguments --section "" --yes
reject_wrapper_arguments --yes --yes
reject_wrapper_arguments --expect-commit ABC --yes
reject_wrapper_arguments --expect-commit "" --yes
reject_wrapper_arguments
: >"$wrapper_log"
OMAQ_INSTALL_TEST_LOG="$wrapper_log" "$wrapper/install.sh" --help \
  >"$tmp/wrapper-help.txt"
if [ -s "$wrapper_log" ] ||
    ! grep -Fq 'Usage: ./install.sh' "$tmp/wrapper-help.txt"; then
  echo "update-order: root installer help changed state" >&2
  exit 1
fi

mkdir -p "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts"
cat >"$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$OMAQ_UPDATE_TEST_LOG"
exit "${OMAQ_UPDATE_STATUS:-0}"
EOF
chmod 755 "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh"
run_update_case() {
  name=$1
  status=$2
  expected=$3
  log="$tmp/update-$name.log"
  : >"$log"
  if HOME="$tmp/home" PATH="$test_path" OMAQ_UPDATE_TEST_LOG="$log" \
      OMAQ_UPDATE_STATUS="$status" bash "$tmp/readme-update.sh"; then
    actual=0
  else
    actual=$?
  fi
  [ "$actual" -eq "$expected" ] && [ "$(cat "$log")" = "--yes" ] || {
    echo "update-order: update $name changed behavior" >&2
    exit 1
  }
}
run_update_case success 0 0
run_update_case failure 31 31
rollback_log="$tmp/rollback.log"
: >"$rollback_log"
HOME="$tmp/home" PATH="$test_path" OMAQ_UPDATE_TEST_LOG="$rollback_log" \
  OMAQ_UPDATE_STATUS=0 bash "$tmp/rollback.sh"
[ "$(cat "$rollback_log")" = "--rollback-helper --yes" ] || {
  echo "update-order: helper rollback does not use the shell-off updater" >&2
  exit 1
}

for document in "$root/README.md" "$root/docs/INSTALLATION.md"; do
  ! grep -Fq 'Known host-reload risk' "$document" || {
    echo "update-order: obsolete host-reload warning remains" >&2
    exit 1
  }
  if ! grep -Fq 'Do not ' "$document" ||
      ! grep -Fq 'omarchy plugin update' "$document"; then
    echo "update-order: unsafe generic updater is not clearly rejected" >&2
    exit 1
  fi
  grep -Fq 'omarchy plugin add https://github.com/HANCORE-linux/OmaQ.git --yes' \
    "$document" || {
    echo "update-order: normal plugin installation is missing" >&2
    exit 1
  }
done

if ! grep -Fq 'Do not update OmaQ with' "$root/README.md" ||
    ! grep -Fq 'including the all-plugins form.' "$root/README.md"; then
  echo "update-order: README generic-updater warning changed" >&2
  exit 1
fi
if ! grep -Fq 'include OmaQ in an all-plugins' "$root/docs/INSTALLATION.md" ||
    ! grep -Fq 'omarchy plugin update --yes' "$root/docs/INSTALLATION.md"; then
  echo "update-order: installation guide omits the all-plugins update risk" >&2
  exit 1
fi
grep -Fq 'fast-forwards the source checkout but has no OmaQ lifecycle hook to rebuild and verify the native helper' \
  "$root/docs/INSTALLATION.md" || {
  echo "update-order: installation guide omits the helper rebuild risk" >&2
  exit 1
}
if ! grep -Fq \
    "Only \`scripts/update-omaq.sh\` implements the source/helper update transaction." \
    "$root/docs/SECURITY.md"; then
  echo "update-order: security guide omits the supported updater boundary" >&2
  exit 1
fi
grep -Fq 'including its all-plugins form: it can fast-forward the live source checkout without rebuilding the native helper' \
  "$root/docs/SECURITY.md" || {
  echo "update-order: security guide omits the generic updater risk" >&2
  exit 1
}
grep -Fq 'leave the packages installed rather than forcing removal' \
  "$root/docs/INSTALLATION.md" || {
  echo "update-order: package conflict guidance is missing" >&2
  exit 1
}

grep -Fq '<summary>Pin a reviewed commit and limit acquisition</summary>' \
  "$root/docs/INSTALLATION.md" || exit 1
grep -Fq '"/usr/bin/git", "-c", "core.hooksPath=/dev/null"' \
  "$root/docs/INSTALLATION.md" || exit 1
! grep -Eq '<sub>|<br[ >]' "$root/README.md" || exit 1
[ "$(grep -Fc '<td width="25%" align="center"><a href="docs/images/guide/' \
  "$root/README.md")" -eq 4 ] || exit 1
grep -Fq 'the updater builds outside the monitored plugin tree' \
  "$root/README.md" || exit 1
grep -Fq 'Dependency packages are never removed automatically' "$root/README.md" || exit 1
grep -Fq '## 16. Git (public)' "$root/docs/PLAN.md" || exit 1
grep -Fq 'mv -T --exchange --no-copy' "$root/docs/PLAN.md" || exit 1
grep -Fq 'omarchy-launch-shell' "$root/docs/PLAN.md" || exit 1
grep -Fq 'update-pending: old helper, new tree' "$root/docs/PLAN.md" || exit 1
grep -Fq 'shell-off source updates' "$root/docs/USER-GUIDE.md" || exit 1
grep -Fq 'cooperative same-user boundary' "$root/docs/SECURITY.md" || exit 1
grep -Fq 'Bootstrap an older installation' "$root/docs/INSTALLATION.md" || exit 1
[ -x "$root/scripts/update-omaq.sh" ] && [ -x "$root/scripts/update-omaq.py" ] || exit 1
[ -x "$root/install.sh" ] && [ -x "$root/scripts/install-omaq.sh" ] &&
  [ -x "$root/scripts/install-omaq.py" ] || exit 1
grep -Fq 'mode=install' "$root/docs/INSTALLATION.md" &&
  grep -Fq 'mode=update' "$root/docs/INSTALLATION.md" || exit 1

printf 'update-order: ok\n'
