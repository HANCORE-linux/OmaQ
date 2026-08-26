#!/bin/bash
# Remove the OmaQ plugin without silently deleting private user data.
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: uninstall-omaq.sh [--yes]

Unloads and removes the OmaQ plugin. Private data, local state, received files,
optional deployment backups, dependency packages, and any Omarchy plugin backup
are retained and can be inspected or removed manually later.
EOF
}

assume_yes=0
while (( $# > 0 )); do
  case "$1" in
  --yes | -y)
    assume_yes=1
    ;;
  --help | -h)
    usage
    exit 0
    ;;
  *)
    printf 'uninstall-omaq: unknown option: %s\n' "$1" >&2
    usage >&2
    exit 2
    ;;
  esac
  shift
done

if (( ! assume_yes )); then
  if [[ ! -t 0 || ! -t 1 ]]; then
    echo "uninstall-omaq: refusing to continue without confirmation; pass --yes" >&2
    exit 1
  fi
  gum confirm "Remove the OmaQ plugin? Private and downloaded data will be retained." || exit 1
fi

set +e
remove_output=$(omarchy plugin remove hancore.omaq --yes)
remove_status=$?
set -e
printf '%s\n' "$remove_output"
plugin_backup=$(printf '%s\n' "$remove_output" |
  sed -n 's/^Removed hancore\.omaq\. Backup at: \(.*\)$/\1/p' | tail -n 1)
if ! printf '%s\n' "$remove_output" |
     grep -Eq '^(Removed|Unlinked) hancore\.omaq(\.|$)'; then
  if (( remove_status != 0 )); then
    echo "uninstall-omaq: plugin removal failed before completion" >&2
    exit "$remove_status"
  fi
  echo "uninstall-omaq: remover returned success without a completion marker" >&2
  exit 1
fi

cat <<'EOF'

OmaQ was unloaded, but your private and downloaded data was not deleted.
The following paths may remain:
  ~/.local/share/omaq/                 identity, contacts, groups, avatars, history, Ratchet state
  ~/.local/state/omaq/                 preferences, unread state, receipts, surfaces, recovery state
  ~/Downloads/omaq/                    received files
  ~/.local/state/omaq-deploy-backups/  deployment backups, when present

Keep these files if you may reinstall OmaQ or need the identity or chat history.
To permanently erase selected data later, inspect it first:
  ls -la -- "$HOME/.local/share/omaq"
  ls -la -- "$HOME/.local/state/omaq"
  ls -la -- "$HOME/Downloads/omaq"
  ls -la -- "$HOME/.local/state/omaq-deploy-backups"
Then manually run only the corresponding deletion command. Each is independent
and irreversible:
  rm -rf -- "$HOME/.local/share/omaq"
  rm -rf -- "$HOME/.local/state/omaq"
  rm -rf -- "$HOME/Downloads/omaq"
  rm -rf -- "$HOME/.local/state/omaq-deploy-backups"
EOF

if [[ -n $plugin_backup ]]; then
  printf '\nOmarchy retained the plain plugin folder at:\n  %s\n' "$plugin_backup"
  printf 'Inspect it later with:\n  ls -la -- %q\n' "$plugin_backup"
  printf 'Delete it later with:\n  rm -rf -- %q\n' "$plugin_backup"
fi

cat <<'EOF'

Dependency packages are retained because other applications may use them:
  toxcore  libsignal-protocol-c  libpulse  libpng  libjpeg-turbo  libwebp
  ttf-material-symbols-variable  qrencode
The optional verification tool zbar may also remain when it was installed for testing.
Inspect ownership and dependencies first:
  pacman -Qi toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode zbar
Only after confirming that no other application needs them, they can be removed with:
  omarchy pkg drop toxcore libsignal-protocol-c libpulse libpng libjpeg-turbo libwebp ttf-material-symbols-variable qrencode zbar
EOF

if (( remove_status != 0 )); then
  echo "uninstall-omaq: plugin removal completed, but Omarchy reported a later error" >&2
  exit "$remove_status"
fi
