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
# The standard command is intentionally a short clone plus root installer.
# The bounded exact-commit acquisition remains available below it.
# shellcheck disable=SC2016
grep -Fq 'mkdir -m 700 -- "$HOME/.omaq-source-install"' \
  "$tmp/readme-install.sh"
grep -Eq '^git clone --no-hardlinks --branch main --single-branch -- \\$' \
  "$tmp/readme-install.sh"
if grep -Fq '/usr/bin/git clone' "$tmp/readme-install.sh"; then
  echo "update-order: convenience install no longer uses user Git" >&2
  exit 1
fi
grep -Fq 'https://github.com/HANCORE-linux/OmaQ.git' \
  "$tmp/readme-install.sh"
# shellcheck disable=SC2016
grep -Fq '"$HOME/.omaq-source-install/install.sh" --section right --yes' \
  "$tmp/readme-install.sh"
if grep -Fq 'omarchy pkg add' "$tmp/readme-install.sh" ||
    grep -Fq 'scripts/install-omaq.sh' "$tmp/readme-install.sh" ||
    grep -Fq '.omaq-source-install.network-home' "$tmp/readme-install.sh"; then
  echo "update-order: primary install bypasses the root installer" >&2
  exit 1
fi
cp "$tmp/readme-install.sh" "$tmp/readme-install-test.sh"

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
cat >"$tmp/bin/git" <<'EOF'
#!/bin/sh
set -eu
expected='clone --no-hardlinks --branch main --single-branch -- https://github.com/HANCORE-linux/OmaQ.git'
case "$*" in
  "$expected "*) ;;
  *)
    printf 'unexpected git command: %s\n' "$*" >&2
    exit 98
    ;;
esac
source_tree=
for argument do
  source_tree=$argument
done
install_home=$(dirname -- "$source_tree")
[ "$source_tree" = "$install_home/.omaq-source-install" ] || {
  echo "git clone did not use the private source path" >&2
  exit 96
}
[ -d "$source_tree" ] && [ ! -L "$source_tree" ] && \
  [ "$(stat -c '%a' -- "$source_tree")" = 700 ] || {
  echo "git clone staging path is not a private real directory" >&2
  exit 95
}
printf 'clone\n' >>"$OMAQ_INSTALL_TEST_LOG"
[ "${OMAQ_CLONE_STATUS:-0}" -eq 0 ] || exit "$OMAQ_CLONE_STATUS"
mkdir -p "$source_tree/scripts"
sed "s#/usr/bin/omarchy#$OMAQ_TEST_OMARCHY#g" \
  "$OMAQ_REAL_INSTALL_SCRIPT" >"$source_tree/install.sh"
cat >"$source_tree/scripts/install-omaq.sh" <<'INSTALLER'
#!/bin/sh
set -eu
case "$*" in
  "--preflight-only --section right --yes")
    printf 'preflight\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_PREFLIGHT_STATUS:-0}"
    ;;
  "--section right --yes")
    printf 'install\n' >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_INSTALLER_STATUS:-0}"
    ;;
  *) exit 97 ;;
esac
INSTALLER
chmod 755 "$source_tree/install.sh" "$source_tree/scripts/install-omaq.sh"
EOF
cat >"$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh" <<'EOF'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$OMAQ_UPDATE_TEST_LOG"
exit "${OMAQ_UPDATE_STATUS:-0}"
EOF
chmod 755 "$tmp/bin/omarchy" "$tmp/bin/git" \
  "$tmp/home/.config/omarchy/plugins/hancore.omaq/scripts/update-omaq.sh"
test_path="$tmp/bin:$PATH"

run_install_case() {
  shell_name=$1
  shell_path=$2
  name=$3
  clone_status=$4
  preflight_status=$5
  package_status=$6
  installer_status=$7
  expected_status=$8
  expected_log=$9
  source_tree="$tmp/home/.omaq-source-install"
  log="$tmp/home/.install-test.log"
  error_log="$tmp/install-$shell_name-$name.stderr"
  plugins_dir="$tmp/home/.config/omarchy/plugins"
  : >"$log"
  : >"$error_log"
  rm -rf "$source_tree" "$plugins_dir/OmaQ"
  case "$name" in
    existing-directory)
      mkdir -m 700 -- "$source_tree"
      ;;
    existing-symlink)
      ln -s "$plugins_dir" "$source_tree"
      ;;
  esac

  if (
    cd "$plugins_dir"
    HOME="$tmp/home"
    PATH=$test_path
    OMAQ_INSTALL_TEST_LOG="$log"
    OMAQ_CLONE_STATUS="$clone_status"
    OMAQ_PREFLIGHT_STATUS="$preflight_status"
    OMAQ_PACKAGE_STATUS="$package_status"
    OMAQ_INSTALLER_STATUS="$installer_status"
    OMAQ_REAL_INSTALL_SCRIPT="$root/install.sh"
    OMAQ_TEST_OMARCHY="$tmp/bin/omarchy"
    export HOME PATH OMAQ_INSTALL_TEST_LOG OMAQ_CLONE_STATUS \
      OMAQ_PREFLIGHT_STATUS OMAQ_PACKAGE_STATUS OMAQ_INSTALLER_STATUS \
      OMAQ_REAL_INSTALL_SCRIPT OMAQ_TEST_OMARCHY
    if [ "$shell_name" = fish ]; then
      "$shell_path" --no-config "$tmp/readme-install-test.sh"
    else
      "$shell_path" "$tmp/readme-install-test.sh"
    fi
  ) 2>"$error_log"; then
    actual_status=0
  else
    actual_status=$?
  fi

  [ "$actual_status" -eq "$expected_status" ] || {
    printf 'update-order: install %s returned %s, expected %s\n' \
      "$name" "$actual_status" "$expected_status" >&2
    cat "$error_log" >&2
    exit 1
  }
  printf '%b' "$expected_log" >"$tmp/install-$shell_name-$name.expected"
  cmp -s "$log" "$tmp/install-$shell_name-$name.expected" || {
    printf 'update-order: install %s order mismatch\n' "$name" >&2
    cat "$error_log" >&2
    exit 1
  }
  [ ! -e "$plugins_dir/OmaQ" ] || {
    echo "update-order: source clone entered the monitored plugin tree" >&2
    exit 1
  }
}

run_install_suite() {
  shell_name=$1
  shell_path=$2
  run_install_case "$shell_name" "$shell_path" \
    success 0 0 0 0 0 'clone\npreflight\npackages\ninstall\n'
  run_install_case "$shell_name" "$shell_path" \
    clone-failure 22 0 0 0 22 'clone\n'
  run_install_case "$shell_name" "$shell_path" \
    preflight-failure 0 24 0 0 24 'clone\npreflight\n'
  run_install_case "$shell_name" "$shell_path" \
    package-failure 0 0 21 0 21 'clone\npreflight\npackages\n'
  run_install_case "$shell_name" "$shell_path" \
    installer-failure 0 0 0 23 23 \
    'clone\npreflight\npackages\ninstall\n'
  run_install_case "$shell_name" "$shell_path" \
    existing-directory 0 0 0 0 1 ''
  run_install_case "$shell_name" "$shell_path" \
    existing-symlink 0 0 0 0 1 ''
}

run_install_suite bash /usr/bin/bash
if [ -x /usr/bin/fish ]; then
  run_install_suite fish /usr/bin/fish
fi

wrapper="$tmp/wrapper"
mkdir -p "$wrapper/scripts"
sed "s#/usr/bin/omarchy#$tmp/bin/omarchy#g" \
  "$root/install.sh" >"$wrapper/install.sh"
cat >"$wrapper/scripts/install-omaq.sh" <<'EOF'
#!/bin/sh
set -eu
case "${1:-}" in
  --preflight-only)
    printf 'preflight=%s\n' "$*" >>"$OMAQ_INSTALL_TEST_LOG"
    exit "${OMAQ_PREFLIGHT_STATUS:-0}"
    ;;
  *) printf 'args=%s\n' "$*" >>"$OMAQ_INSTALL_TEST_LOG" ;;
esac
EOF
chmod 755 "$wrapper/install.sh" "$wrapper/scripts/install-omaq.sh"
wrapper_log="$tmp/wrapper.log"
: >"$wrapper_log"
OMAQ_INSTALL_TEST_LOG="$wrapper_log" OMAQ_PACKAGE_STATUS=0 \
  "$wrapper/install.sh" --expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
  --section left --yes
printf '%s\n' \
  'preflight=--preflight-only --expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa --section left --yes' \
  'packages' \
  'args=--expect-commit aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa --section left --yes' \
  >"$tmp/wrapper.expected"
cmp -s "$wrapper_log" "$tmp/wrapper.expected" || {
  echo "update-order: root installer did not forward validated arguments" >&2
  exit 1
}
reject_wrapper_arguments() {
  : >"$wrapper_log"
  if OMAQ_INSTALL_TEST_LOG="$wrapper_log" OMAQ_PACKAGE_STATUS=0 \
      "$wrapper/install.sh" "$@" \
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
  echo "update-order: root installer help changed packages" >&2
  exit 1
fi

run_update_case() {
  name=$1
  status=$2
  expected=$3
  log="$tmp/update-$name.log"
  : >"$log"
  if HOME="$tmp/home" PATH="$test_path" \
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
HOME="$tmp/home" PATH="$test_path" OMAQ_UPDATE_TEST_LOG="$rollback_log" \
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
  if ! grep -Fq 'does not install an OmaQ package through Pacman' "$document"; then
    echo "update-order: source-only AUR status is missing" >&2
    exit 1
  fi
done

grep -Fq '<summary>Pin a reviewed commit and limit acquisition</summary>' \
  "$root/docs/INSTALLATION.md" || {
  echo "update-order: bounded bootstrap is not an optional detail" >&2
  exit 1
}
grep -Fq '"/usr/bin/git", "-c", "core.hooksPath=/dev/null"' \
  "$root/docs/INSTALLATION.md" || {
  echo "update-order: bounded bootstrap does not bind system Git" >&2
  exit 1
}

if grep -Eq '<sub>|<br[ >]' "$root/README.md"; then
  echo "update-order: README introduction changes the subtitle size or line" >&2
  exit 1
fi
[ "$(grep -Fc '<td width="25%" align="center"><a href="docs/images/guide/' \
  "$root/README.md")" -eq 4 ] || {
  echo "update-order: README showcase does not contain four equal cells" >&2
  exit 1
}
for image in 15-direct-chat-overview.png 20-direct-image-preview.png \
    22-group-chat-overview.png 35-demo-window.png; do
  grep -Fq "docs/images/guide/$image" "$root/README.md" || {
    echo "update-order: README showcase image is missing: $image" >&2
    exit 1
  }
done
grep -Fq 'the updater builds outside the monitored plugin tree' \
  "$root/README.md" || {
  echo "update-order: concise README update safety summary is missing" >&2
  exit 1
}
if grep -Fq 'The updater returns without staging or stopping the shell' \
    "$root/README.md"; then
  echo "update-order: verbose README update explanation remains" >&2
  exit 1
fi
grep -Fq 'This installs OmaQ and its dependencies in the right bar section.' \
  "$root/README.md" || {
  echo "update-order: concise README install summary is missing" >&2
  exit 1
}
if grep -Fq 'builds the omitted Signal-enabled' "$root/README.md"; then
  echo "update-order: verbose README install explanation remains" >&2
  exit 1
fi

grep -Fq '## 16. Git (public)' "$root/docs/PLAN.md" || {
  echo "update-order: architecture still describes a private repository" >&2
  exit 1
}
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
[ -x "$root/install.sh" ] && [ -x "$root/scripts/install-omaq.sh" ] &&
    [ -x "$root/scripts/install-omaq.py" ] || {
  echo "update-order: source installer is not executable" >&2
  exit 1
}
if ! grep -Fq 'mode=install' "$root/docs/INSTALLATION.md" ||
    ! grep -Fq 'mode=update' "$root/docs/INSTALLATION.md"; then
  echo "update-order: shared source bootstrap modes are missing" >&2
  exit 1
fi

printf 'update-order: ok\n'
