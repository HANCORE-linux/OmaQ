#!/bin/bash
# Copy one allowlisted Wayland clipboard image into a helper-created staging file.
set -euo pipefail

usage() {
  echo "Usage: paste-image.sh <image/png|image/jpeg|image/webp> <staging-path>" >&2
  exit 2
}

(( $# == 2 )) || usage
mime=$1
destination=$2
case "$mime" in
  image/png | image/jpeg | image/webp) ;;
  *) usage ;;
esac
[[ $destination == /* ]] || usage
command -v wl-paste >/dev/null 2>&1 || {
  echo "paste-image: wl-paste is unavailable" >&2
  exit 3
}

python3 - "$destination" <<'PY'
import os, stat, sys
path = sys.argv[1]
parent = os.path.dirname(path)
parent_info = os.lstat(parent)
file_info = os.lstat(path)
uid = os.geteuid()
if (not stat.S_ISDIR(parent_info.st_mode) or stat.S_ISLNK(parent_info.st_mode) or
        parent_info.st_uid != uid or parent_info.st_mode & 0o077):
    raise SystemExit("paste-image: unsafe staging directory")
if (not stat.S_ISREG(file_info.st_mode) or stat.S_ISLNK(file_info.st_mode) or
        file_info.st_uid != uid or file_info.st_nlink != 1 or
        file_info.st_mode & 0o077 or file_info.st_size != 0):
    raise SystemExit("paste-image: unsafe staging file")
PY

# The helper independently validates, decodes, bounds, and canonicalizes this
# file before exposing it to QML or allowing it to be sent.
timeout 15s wl-paste --type "$mime" | head -c 8388609 >"$destination"
size=$(stat -Lc '%s' -- "$destination")
if (( size == 0 || size > 8388608 )); then
  echo "paste-image: clipboard image exceeds the 8 MiB transfer limit" >&2
  exit 4
fi
