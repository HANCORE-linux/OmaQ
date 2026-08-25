#!/bin/sh
# Phase 5: import refuses without replace; replace on temp home; search hits.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
bin="$root/helper/omaq"
[ -x "$bin" ] || { echo "phase5: no helper" >&2; exit 1; }

real_home="${HOME}/.local/share/omaq"
ha=$(mktemp -d /tmp/omaq-p5a-XXXXXX)
sa=$(mktemp -d /tmp/omaq-p5as-XXXXXX)
hb=$(mktemp -d /tmp/omaq-p5b-XXXXXX)
sb=$(mktemp -d /tmp/omaq-p5bs-XXXXXX)
loader_home=$(mktemp -d /tmp/omaq-p5lh-XXXXXX)
loader_state=$(mktemp -d /tmp/omaq-p5ls-XXXXXX)
loader_out=$(mktemp /tmp/omaq-p5lo-XXXXXX)
loader_hold=$(mktemp -u /tmp/omaq-p5lf-XXXXXX)
exp=$(mktemp /tmp/omaq-p5-XXXXXX.save)
bad=$(mktemp /tmp/omaq-p5bad-XXXXXX.save)
malformed=$(mktemp /tmp/omaq-p5malformed-XXXXXX.save)
binding_at=$(mktemp /tmp/omaq-p5binding-at-XXXXXX.save)
binding_over=$(mktemp /tmp/omaq-p5binding-over-XXXXXX.save)
binding_version=$(mktemp /tmp/omaq-p5binding-version-XXXXXX.save)
binding_unknown=$(mktemp /tmp/omaq-p5binding-unknown-XXXXXX.save)
binding_duplicate=$(mktemp /tmp/omaq-p5binding-duplicate-XXXXXX.save)
out=$(mktemp /tmp/omaq-p5o-XXXXXX)
hold=$(mktemp -u /tmp/omaq-p5f-XXXXXX)
pid=""
loader_pid=""
cleanup() {
	exec 3>&- 7>&- 2>/dev/null || true
	[ -n "${pid:-}" ] && kill "$pid" 2>/dev/null || true
	[ -n "${loader_pid:-}" ] && kill "$loader_pid" 2>/dev/null || true
	rm -rf "$ha" "$sa" "$hb" "$sb" "$loader_home" "$loader_state" \
		"$loader_out" "$loader_hold" "$loader_out.err" "$exp" "$bad" "$malformed" \
		"$binding_at" "$binding_over" "$binding_version" "$binding_unknown" \
		"$binding_duplicate" "$out" "$hold" "$out.err"
}
trap cleanup EXIT

case "$ha" in
"$real_home"|"$real_home"/*) echo "phase5: refused real home" >&2; exit 1 ;;
esac

{
	printf 'OMAQGF1\n'
	i=1
	while [ "$i" -le 10 ]; do
		printf 'g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\t%064d\t%064d\n' \
			"$i" "$((i + 100))"
		i=$((i + 1))
	done
} >"$loader_home/group-friends.tsv"
mkfifo "$loader_hold"
OMAQ_HOME="$loader_home" OMAQ_STATE="$loader_state" "$bin" \
	>"$loader_out" 2>"$loader_out.err" <"$loader_hold" &
loader_pid=$!
exec 7>"$loader_hold"
printf '{"op":"status","id":"phase5-loader-limit"}\n' >&7
i=0
while [ "$i" -lt 60 ]; do
	grep -a -q '"request":"phase5-loader-limit"' "$loader_out" && break
	i=$((i + 1))
	sleep 0.1
done
if [ "$i" -ge 60 ] || grep -a -q '"addr"' "$loader_out" ||
   [ "$(wc -l <"$loader_home/group-friends.tsv")" -ne 11 ]; then
	echo "phase5: unknown over-limit group bindings were accepted" >&2
	tail -n 20 -- "$loader_out" "$loader_out.err" >&2 || true
	exit 1
fi
exec 7>&-
kill "$loader_pid" 2>/dev/null || true
wait "$loader_pid" 2>/dev/null || true
loader_pid=""

orphan_stage="$sa/identity-import-stage-999999"
mkdir -p "$orphan_stage"
printf 'staged' >"$orphan_stage/tox.save"
printf 'registry' >"$orphan_stage/groups.tsv"
printf 'bindings' >"$orphan_stage/group-friends.tsv"
printf 'bundle' >"$orphan_stage/identity.bundle"
printf 'temp' >"$orphan_stage/groups.tsv.tmp.999999"
printf 'temp' >"$orphan_stage/group-friends.tsv.tmp.999999"
printf 'temp' >"$orphan_stage/identity.bundle.tmp.999999"
random_orphan_stage="$sa/identity-import-stage-Ab12z9"
mkdir "$random_orphan_stage"
printf 'staged' >"$random_orphan_stage/tox.save"
printf 'registry' >"$random_orphan_stage/groups.tsv"
printf 'bindings' >"$random_orphan_stage/group-friends.tsv"
printf 'bundle' >"$random_orphan_stage/identity.bundle"
mkfifo "$hold"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out" 2>"$out.err" <"$hold" &
pid=$!
exec 3>"$hold"
sleep 0.4
[ ! -e "$orphan_stage" ] && [ ! -e "$random_orphan_stage" ] || {
	echo "phase5: orphan import stage not cleaned" >&2
	exit 1
}

echo '{"op":"status"}' >&3
sleep 0.2
addr=$(grep -a '"addr"' "$out" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ -n "$addr" ] || { echo "phase5: no addr" >&2; exit 1; }
stage_sentinel="$sa/stage-sentinel"
mkdir "$stage_sentinel"
printf 'keep-save' >"$stage_sentinel/tox.save"
printf 'keep-groups' >"$stage_sentinel/groups.tsv"
printf 'keep-bindings' >"$stage_sentinel/group-friends.tsv"
ln -s "$stage_sentinel" "$sa/identity-import-stage-$pid"

before=$(wc -l <"$out")
printf '%s\n' '{"op":"nickname.set","nickname":"1234567890123456789","id":"phase5-nickname-invalid"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"nickname_invalid"' |
	grep -a -q '"request":"phase5-nickname-invalid"' || {
	echo "phase5: 19-character nickname was accepted" >&2
	exit 1
}
before=$(wc -l <"$out")
printf '%s\n' '{"op":"nickname.set","nickname":"123456789012345678","id":"phase5-nickname-valid"}' >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" |
	grep -a '"event":"nickname","value":"123456789012345678"' |
	grep -a -q '"request":"phase5-nickname-valid"' || {
	echo "phase5: 18-character nickname was rejected" >&2
	exit 1
}

printf '{"op":"identity.export","path":"%s","id":"phase5-export-a"}\n' "$exp" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"export"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] && [ -f "$exp" ] || { echo "phase5: export failed" >&2; exit 1; }
grep -a '"op":"export"' "$out" | tail -1 |
	grep -a -q '"request":"phase5-export-a"' || {
	echo "phase5: export request correlation missing" >&2
	exit 1
}
printf '{"op":"identity.inspect","path":"%s","id":"phase5-inspect-a"}\n' "$exp" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a '"op":"inspect"' "$out" | grep -a -q '"request":"phase5-inspect-a"'; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
if [ "$ok" -ne 1 ]; then
	echo "phase5: identity inspection failed" >&2
	tail -n 25 -- "$out" "$out.err" >&2 || true
	exit 1
fi
[ "$(cat "$stage_sentinel/tox.save")" = keep-save ] &&
[ "$(cat "$stage_sentinel/groups.tsv")" = keep-groups ] &&
[ "$(cat "$stage_sentinel/group-friends.tsv")" = keep-bindings ] &&
[ -L "$sa/identity-import-stage-$pid" ] || {
	echo "phase5: identity staging followed an attacker-controlled symlink" >&2
	exit 1
}
printf '{"op":"identity.export","path":"%s","id":"phase5-export-malformed"}\n' "$malformed" >&3
i=0
while [ "$i" -lt 40 ]; do
	if grep -a '"op":"export"' "$out" |
	   grep -a -q '"request":"phase5-export-malformed"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 40 ] || { echo "phase5: malformed registry bundle export failed" >&2; exit 1; }
python3 - "$malformed" <<'PY'
import struct, sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
if data[:8] != b"OMAQID2\n" or len(data) < 20:
    raise SystemExit("phase5: malformed fixture is not a v2 bundle")
tox_len, registry_len, bindings_len = struct.unpack(">III", data[8:20])
if 20 + tox_len + registry_len + bindings_len != len(data):
    raise SystemExit("phase5: malformed fixture length mismatch")
payload = b"malformed-binding\n"
base_end = 20 + tox_len + registry_len
data = data[:base_end]
data[16:20] = struct.pack(">I", len(payload))
data.extend(payload)
if base_end + len(payload) != len(data):
    raise SystemExit("phase5: malformed sidecar fixture length mismatch")
open(path, "wb").write(data)
PY
before=$(wc -l <"$out")
printf '{"op":"identity.inspect","path":"%s","id":"phase5-inspect-malformed"}\n' "$malformed" >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_import_failed"' |
	grep -a -q '"request":"phase5-inspect-malformed"' || {
	echo "phase5: malformed identity registry passed inspection" >&2
	exit 1
}
printf 'not-an-identity' >"$bad"
before=$(wc -l <"$out")
printf '{"op":"identity.inspect","path":"%s","id":"phase5-inspect-bad"}\n' "$bad" >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_import_failed"' |
	grep -a -q '"request":"phase5-inspect-bad"' || {
	echo "phase5: invalid identity inspection did not fail closed" >&2
	exit 1
}
if tail -n +"$((before + 1))" "$out" | grep -a -q '"op":"inspect"'; then
	echo "phase5: invalid identity inspection succeeded" >&2
	exit 1
fi

printf '{"op":"identity.import","path":"%s","id":"phase5-import-refuse"}\n' "$exp" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q 'identity_exists' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase5: expected identity_exists" >&2; exit 1; }
grep -a '"code":"identity_exists"' "$out" | tail -1 |
	grep -a -q '"request":"phase5-import-refuse"' || {
	echo "phase5: import refusal request correlation missing" >&2
	exit 1
}

# Second home creates a different identity, then we replace A's save with B's via import.
holdb=$(mktemp -u /tmp/omaq-p5fb-XXXXXX)
outb=$(mktemp /tmp/omaq-p5ob-XXXXXX)
mkfifo "$holdb"
OMAQ_HOME="$hb" OMAQ_STATE="$sb" "$bin" >"$outb" 2>"$outb.err" <"$holdb" &
pidb=$!
exec 4>"$holdb"
sleep 0.4
echo '{"op":"status"}' >&4
sleep 0.2
addrb=$(grep -a '"addr"' "$outb" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ -n "$addrb" ] || { echo "phase5: no addr b" >&2; exit 1; }
printf '{"op":"group.create","title":"Exported group"}\n' >&4
i=0
exported_gid=""
while [ "$i" -lt 40 ]; do
	exported_gid=$(grep -a '"event":"group.changed"' "$outb" | grep -a '"action":"create"' |
		tail -1 | sed -n 's/.*"group":"\([^"]*\)".*/\1/p')
	[ -n "$exported_gid" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ -n "$exported_gid" ] || { echo "phase5: no exported group" >&2; exit 1; }
sleep 1
expb=$(mktemp /tmp/omaq-p5b-XXXXXX.save)
printf '{"op":"identity.export","path":"%s","id":"phase5-export-b"}\n' "$expb" >&4
sleep 0.3
[ -f "$expb" ] || { echo "phase5: b export" >&2; exit 1; }
kill "$pidb" 2>/dev/null || true
pidb=""
exec 4>&-

python3 - "$expb" "$binding_at" "$binding_over" "$binding_version" \
	"$binding_unknown" "$binding_duplicate" "$exported_gid" <<'PY'
import struct, sys
source, at_path, over_path, version_path, unknown_path, duplicate_path, gid = sys.argv[1:]
data = bytearray(open(source, "rb").read())
if data[:8] != b"OMAQID2\n" or len(data) < 20:
    raise SystemExit("phase5: binding fixture is not a v2 bundle")
tox_len, registry_len, bindings_len = struct.unpack(">III", data[8:20])
base_end = 20 + tox_len + registry_len
if base_end + bindings_len != len(data):
    raise SystemExit("phase5: binding fixture length mismatch")

def write_payload(path, lines):
    payload = ("\n".join(lines) + "\n").encode()
    result = bytearray(data[:base_end])
    result[16:20] = struct.pack(">I", len(payload))
    result.extend(payload)
    open(path, "wb").write(result)

def write_fixture(path, count):
    lines = ["OMAQGF1"]
    for index in range(count):
        friend_key = f"{index + 1:064x}"
        member_key = f"{index + 101:064x}"
        lines.append(f"{gid}\t{friend_key}\t{member_key}")
    write_payload(path, lines)

write_fixture(at_path, 9)
write_fixture(over_path, 10)
write_payload(version_path, ["OMAQGF2"])
write_payload(unknown_path, ["OMAQGF1", f"g:{'b' * 64}\t{'1' * 64}\t{'2' * 64}"])
write_payload(duplicate_path, ["OMAQGF1",
    f"{gid}\t{'3' * 64}\t{'4' * 64}",
    f"{gid}\t{'3' * 64}\t{'5' * 64}"])
PY
before=$(wc -l <"$out")
printf '{"op":"identity.inspect","path":"%s","id":"phase5-bindings-at"}\n' "$binding_at" >&3
i=0
while [ "$i" -lt 40 ]; do
	if tail -n +"$((before + 1))" "$out" | grep -a '"op":"inspect"' |
	   grep -a -q '"request":"phase5-bindings-at"'; then
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 40 ] || { echo "phase5: at-limit group bindings were rejected" >&2; exit 1; }
before=$(wc -l <"$out")
printf '{"op":"identity.inspect","path":"%s","id":"phase5-bindings-over"}\n' "$binding_over" >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_import_failed"' |
	grep -a -q '"request":"phase5-bindings-over"' || {
	echo "phase5: over-limit group bindings passed inspection" >&2
	exit 1
}
for fixture in \
	"$binding_version:phase5-bindings-version" \
	"$binding_unknown:phase5-bindings-unknown" \
	"$binding_duplicate:phase5-bindings-duplicate"; do
	fixture_path=${fixture%%:*}
	fixture_request=${fixture#*:}
	before=$(wc -l <"$out")
	printf '{"op":"identity.inspect","path":"%s","id":"%s"}\n' \
		"$fixture_path" "$fixture_request" >&3
	i=0
	while [ "$i" -lt 40 ]; do
		if tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_import_failed"' |
		   grep -a -q '"request":"'"$fixture_request"'"'; then
			break
		fi
		i=$((i + 1))
		sleep 0.05
	done
	[ "$i" -lt 40 ] || {
		echo "phase5: invalid binding fixture passed: $fixture_request" >&2
		exit 1
	}
done

printf '{"op":"identity.import","path":"%s","replace":true,"id":"phase5-replace"}\n' "$expb" >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"op":"import"' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
if [ "$ok" -ne 1 ]; then
	echo "phase5: replace import failed" >&2
	tail -n 30 -- "$out" "$out.err" >&2 || true
	exit 1
fi
grep -a '"op":"import"' "$out" | tail -1 |
	grep -a -q '"request":"phase5-replace"' || {
	echo "phase5: replace request correlation missing" >&2
	exit 1
}
before=$(wc -l <"$out")
printf '{"op":"invite.create","kind":"group","group":"%s","id":"0","key":"%s","request":"gi-identity-gate"}\n' \
	"$exported_gid" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"event":"group.invite.failed"' |
	grep -a '"code":"identity_changed"' |
	grep -a -q '"request":"gi-identity-gate"' || {
	echo "phase5: identity gate lost group invite correlation" >&2
	exit 1
}
before=$(wc -l <"$out")
printf '%s\n' '{"op":"invite.create","kind":"direct","request":"invite-identity-gate-create"}' >&3
printf '%s\n' '{"op":"invite.revoke","request":"invite-identity-gate-revoke"}' >&3
sleep 0.2
for request in invite-identity-gate-create invite-identity-gate-revoke; do
	tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_changed"' |
		grep -a -q '"request":"'"$request"'"' || {
		echo "phase5: identity gate lost direct invite correlation for $request" >&2
		exit 1
	}
done
before=$(wc -l <"$out")
printf '{"op":"identity.inspect","path":"%s","id":"phase5-stale-identity"}\n' "$exp" >&3
sleep 0.2
tail -n +"$((before + 1))" "$out" | grep -a '"code":"identity_changed"' |
	grep -a -q '"request":"phase5-stale-identity"' || {
	echo "phase5: stale identity request correlation missing" >&2
	exit 1
}
echo '{"op":"status","id":"phase5-identity"}' >&3
i=0
instance=""
while [ "$i" -lt 40 ]; do
	instance=$(grep -a '"request":"phase5-identity"' "$out" | tail -1 |
		sed -n 's/.*"instance":"\([^"]*\)".*/\1/p')
	[ -n "$instance" ] && break
	i=$((i + 1))
	sleep 0.05
done
[ -n "$instance" ] || { echo "phase5: identity handshake missing" >&2; exit 1; }
printf '{"op":"identity.ready","id":"%s"}\n' "$instance" >&3
i=0
while [ "$i" -lt 40 ]; do
	if grep -a -q '"code":"group_orphaned"' "$out"; then
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$i" -lt 40 ] || { echo "phase5: missing private group was not reported" >&2; exit 1; }
if grep -a '"event":"group.info"' "$out" | grep -a -q '"group":"'"$exported_gid"'"'; then
	echo "phase5: private group was fabricated from exported chat ID" >&2
	exit 1
fi

# Search fixture on disk (no need for a live message).
python3 - "$ha" <<'PY'
import os, sys
home = sys.argv[1]
d = os.path.join(home, "history", "c1")
os.makedirs(d, 0o700)
p = os.path.join(d, "messages.jsonl")
open(p, "w").write('{"text":"needle-unique-xyz"}\n{"text":"other"}\n')
os.chmod(p, 0o600)
PY
echo '{"op":"search","conversation":"c1","text":"needle-unique","id":"phase5-search"}' >&3
i=0
ok=0
while [ "$i" -lt 40 ]; do
	if grep -a -q 'needle-unique-xyz' "$out"; then
		ok=1
		break
	fi
	i=$((i + 1))
	sleep 0.05
done
[ "$ok" -eq 1 ] || { echo "phase5: search miss" >&2; cat "$out" >&2; exit 1; }
grep -a 'needle-unique-xyz' "$out" | tail -1 |
	grep -a -q '"request":"phase5-search"' || {
	echo "phase5: search request correlation missing" >&2
	exit 1
}

kill "$pid" 2>/dev/null || true
pid=""
exec 3>&-
# Reload A with replaced save — addr must match B.
hold2=$(mktemp -u /tmp/omaq-p5f2-XXXXXX)
out2=$(mktemp /tmp/omaq-p5o2-XXXXXX)
mkfifo "$hold2"
OMAQ_HOME="$ha" OMAQ_STATE="$sa" "$bin" >"$out2" 2>"$out2.err" <"$hold2" &
pid=$!
exec 3>"$hold2"
sleep 0.5
echo '{"op":"status"}' >&3
sleep 0.3
addr2=$(grep -a '"addr"' "$out2" | tail -1 | sed -n 's/.*"addr":"\([^"]*\)".*/\1/p')
[ "$addr2" = "$addrb" ] || { echo "phase5: replace did not change identity" >&2; exit 1; }
[ "$addr2" != "$addr" ] || { echo "phase5: addrs should differ" >&2; exit 1; }

echo "phase5: ok"
exit 0
