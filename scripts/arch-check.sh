#!/bin/sh
# Mechanical architecture law. Fail if a layer leak appears.
# Missing files are not failures (phase 0 has no helper sources yet).

set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
fail=0

warn() { printf 'arch: %s\n' "$*" >&2; }
die() { warn "FAIL: $*"; fail=1; }

# Only tox_adapt.c may include tox headers.
if [ -d helper ]; then
	hits=$(grep -RIn --include='*.c' --include='*.h' \
		-e '<tox/tox.h>' -e '<tox/toxav.h>' -e '<tox/toxencryptsave.h>' \
		-e '"tox/tox.h"' -e '"tox/toxav.h"' -e '"tox/toxencryptsave.h"' \
		helper 2>/dev/null | grep -v '/tox_adapt\.[ch]:' || true)
	if [ -n "$hits" ]; then
		die "tox include outside tox_adapt.c"
		printf '%s\n' "$hits" >&2
	fi
	hits=$(grep -RIn --include='*.c' --include='*.h' \
		-e '<signal/' -e '"signal/' \
		helper 2>/dev/null | grep -v '/ratchet_adapt\.c:' || true)
	if [ -n "$hits" ]; then
		die "signal include outside ratchet_adapt.c"
		printf '%s\n' "$hits" >&2
	fi
fi

# Only store.c may mention the history path.
if [ -d helper ]; then
	hits=$(grep -RIn --include='*.c' --include='*.h' \
		-e 'OMAQ_HOME/history' -e 'history/' helper 2>/dev/null \
		| grep -v '/store\.[ch]:' || true)
	if [ -n "$hits" ]; then
		die "history path outside store.c"
		printf '%s\n' "$hits" >&2
	fi
fi

# Pure policy: no IO, no tox.
for f in helper/roles.c helper/invite.c helper/conversation.c helper/rate.c helper/safety.c; do
	[ -f "$f" ] || continue
	if grep -En '([^[:alnum:]_](open|fopen|socket)\()' "$f" >/dev/null; then
		die "$f contains IO"
		grep -En '([^[:alnum:]_](open|fopen|socket)\()' "$f" >&2 || true
	fi
	if grep -En '#include[[:space:]]*[<"]tox/|tox_friend|tox_new|tox_iterate|tox_group' "$f" >/dev/null; then
		die "$f talks to toxcore"
		grep -En '#include[[:space:]]*[<"]tox/|tox_friend|tox_new|tox_iterate|tox_group' "$f" >&2 || true
	fi
done

if [ -f Model.js ]; then
	if grep -En 'Qt|Quickshell|XMLHttpRequest' Model.js >/dev/null; then
		die "Model.js contains Qt/Quickshell/XMLHttpRequest"
		grep -En 'Qt|Quickshell|XMLHttpRequest' Model.js >&2 || true
	fi
fi

if find . -name '*.qml' | grep -q .; then
	hits=$(grep -RIn --include='*.qml' -ie 'tox' . || true)
	if [ -n "$hits" ]; then
		die "QML mentions tox"
		printf '%s\n' "$hits" >&2
	fi
fi

for runtime_script in install.sh scripts/float-omaq.sh scripts/uninstall-omaq.sh \
	scripts/paste-image.sh scripts/update-helper.sh scripts/helper-runtime.py \
	scripts/install-omaq.sh scripts/install-omaq.py scripts/update-omaq.sh \
	scripts/update-omaq.py; do
	if [ ! -f "$runtime_script" ] || [ ! -x "$runtime_script" ]; then
		die "missing executable runtime script: $runtime_script"
	fi
done

if [ "$fail" -ne 0 ]; then
	warn "make arch failed"
	exit 1
fi
printf 'arch: ok\n'
exit 0
