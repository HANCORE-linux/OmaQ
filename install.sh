#!/bin/sh
set -eu

fail() {
  printf 'install.sh: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: ./install.sh [--section left|center|right] [--expect-commit COMMIT] --yes

Install OmaQ's package dependencies and build its helper. When this script is
run from the live Omarchy plugin checkout, it restarts the shell and verifies
the running helper. An external checkout uses the verified shell-off installer.
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
  fail "refusing to install packages or restart the shell without --yes"
[ "$(/usr/bin/id -u)" -ne 0 ] || fail "refusing to install OmaQ as root"
umask 077

root=$(CDPATH='' cd -- "$(/usr/bin/dirname -- "$0")" && pwd -P)
home=$(CDPATH='' cd -- "$HOME" && pwd -P)
live_root="$home/.config/omarchy/plugins/hancore.omaq"
installer="$root/scripts/install-omaq.sh"
helper_runtime="$root/scripts/helper-runtime.py"

install_packages() {
  PATH=/usr/bin:/bin /usr/bin/omarchy pkg add \
    toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp \
    ttf-material-symbols-variable qrencode
}

if [ "$root" = "$live_root" ]; then
  [ -f "$helper_runtime" ] && [ -x "$helper_runtime" ] && [ ! -L "$helper_runtime" ] ||
    fail "helper runtime verifier is unavailable: $helper_runtime"
  PATH=/usr/bin:/bin /usr/bin/omarchy plugin validate "$root"
  plugin_json=$(PATH=/usr/bin:/bin /usr/bin/omarchy plugin list --json) ||
    fail "could not verify the live plugin state"
  plugin_enabled=$(printf '%s\n' "$plugin_json" | /usr/bin/jq -er \
    --arg id hancore.omaq '
      [.[] | select(.id == $id)] |
      if length == 1 and .[0].firstParty == false and
          (. [0].kinds | index("bar-widget") != null) and
          (. [0].enabled | type) == "boolean"
      then (if .[0].enabled then "enabled" else "disabled" end)
      else error("unexpected OmaQ plugin state") end
    ') || fail "live plugin state is invalid or ambiguous"
  [ "$plugin_enabled" = disabled ] ||
    fail "OmaQ became enabled before its helper was built; disable it and retry"

  head=$(GIT_OPTIONAL_LOCKS=0 PATH=/usr/bin:/bin \
    /usr/bin/git -C "$root" rev-parse --verify HEAD) ||
    fail "live plugin is not a complete Git checkout"
  [ "${#head}" -eq 40 ] || fail "live plugin has an invalid Git commit"
  case "$head" in
    *[!0-9a-f]*) fail "live plugin has an invalid Git commit" ;;
  esac
  if [ -n "$expected_commit" ] && [ "$head" != "$expected_commit" ]; then
    fail "live plugin is $head, not the expected commit $expected_commit"
  fi

  install_packages
  PATH=/usr/bin:/bin /usr/bin/make --no-print-directory -C "$root" helper

  enable_tmp=$(/usr/bin/mktemp -d "/tmp/omaq-enable-$(/usr/bin/id -u).XXXXXXXX") ||
    fail "could not create private enable-result directory"
  cleanup_enable_tmp() {
    /usr/bin/rm -rf -- "$enable_tmp"
  }
  trap cleanup_enable_tmp EXIT HUP INT TERM
  set +e
  if [ -n "$section" ]; then
    PATH=/usr/bin:/bin /usr/bin/omarchy plugin enable hancore.omaq \
      --section "$section" >"$enable_tmp/stdout" 2>"$enable_tmp/stderr"
  else
    PATH=/usr/bin:/bin /usr/bin/omarchy plugin enable hancore.omaq \
      >"$enable_tmp/stdout" 2>"$enable_tmp/stderr"
  fi
  enable_status=$?
  set -e
  enable_size=$(( $(/usr/bin/wc -c <"$enable_tmp/stdout") + \
    $(/usr/bin/wc -c <"$enable_tmp/stderr") ))
  [ "$enable_size" -le 65536 ] || fail "plugin enable produced oversized output"
  if [ "$enable_status" -eq 0 ]; then
    /usr/bin/cat "$enable_tmp/stdout"
    /usr/bin/cat "$enable_tmp/stderr" >&2
  elif [ "$enable_status" -eq 1 ] && [ ! -s "$enable_tmp/stdout" ] &&
      printf 'omarchy-shell is not responding\n' |
        /usr/bin/cmp -s - "$enable_tmp/stderr"; then
    printf '%s\n' "plugin enable response was interrupted; verifying after restart"
  else
    /usr/bin/cat "$enable_tmp/stdout"
    /usr/bin/cat "$enable_tmp/stderr" >&2
    fail "plugin enable command failed ($enable_status)"
  fi
  cleanup_enable_tmp
  trap - EXIT HUP INT TERM

  PATH=/usr/bin:/bin /usr/bin/omarchy restart shell

  ready=0
  attempt=0
  while [ "$attempt" -lt 200 ]; do
    status_json=$(PATH=/usr/bin:/bin /usr/bin/python3 -IB \
      "$helper_runtime" status --root "$root" --json 2>/dev/null || true)
    if printf '%s\n' "$status_json" | /usr/bin/python3 -I -c '
import json, sys
value = json.load(sys.stdin)
available = value.get("available_sha256")
running = value.get("running_sha256")
valid_hash = (isinstance(available, str) and len(available) == 64 and
              all(char in "0123456789abcdef" for char in available))
raise SystemExit(0 if value.get("state") == "current" and valid_hash and
                 running == available else 1)
' 2>/dev/null; then
      ready=1
      break
    fi
    attempt=$((attempt + 1))
    /usr/bin/sleep 0.1
  done
  [ "$ready" -eq 1 ] || {
    PATH=/usr/bin:/bin /usr/bin/python3 -IB \
      "$helper_runtime" status --root "$root" --json || true
    fail "shell restarted, but the OmaQ helper did not become ready"
  }
  printf 'source: installed (%s)\n' "$head"
  printf 'helper: current\n'
  exit 0
fi

[ -f "$installer" ] && [ -x "$installer" ] && [ ! -L "$installer" ] ||
  fail "verified source installer is unavailable: $installer"

set -- --yes
if [ -n "$section" ]; then
  set -- --section "$section" "$@"
fi
if [ -n "$expected_commit" ]; then
  set -- --expect-commit "$expected_commit" "$@"
fi

"$installer" --preflight-only "$@"
install_packages
exec "$installer" "$@"
