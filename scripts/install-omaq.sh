#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(/usr/bin/dirname -- "$0")/.." && pwd -P)
exec /usr/bin/python3 -IB "$root/scripts/install-omaq.py" "$@"
