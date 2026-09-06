#!/usr/bin/env python3
"""Keep every TCP-only Tox bootstrap path capable of restoring relays."""

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "helper/tox_adapt.c").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    match = re.search(signature + r"\s*\{", SOURCE)
    if not match:
        raise SystemExit(f"tcp-relay-retry-source: missing function: {signature}")
    opening = SOURCE.find("{", match.start())
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening + 1:index]
    raise SystemExit(f"tcp-relay-retry-source: unterminated function: {signature}")


bootstrap = function_body(r"static void bootstrap_tox\(struct omaq_tox \*t\)")
open_body = function_body(
    r"struct omaq_tox \*omaq_tox_open\(const char \*home, const char \*pass, int \*err_out\)"
)
iterate = function_body(r"void omaq_tox_iterate\(struct omaq_tox \*t\)")

if bootstrap.count("tox_bootstrap(") != 1:
    raise SystemExit("tcp-relay-retry-source: bootstrap node registration changed")
if bootstrap.count("tox_add_tcp_relay(") != 1:
    raise SystemExit("tcp-relay-retry-source: TCP relay registration is not unconditional")
if "add_relays" in SOURCE:
    raise SystemExit("tcp-relay-retry-source: optional relay bypass returned")
if open_body.count("bootstrap_tox(t);") != 1:
    raise SystemExit("tcp-relay-retry-source: startup does not use the shared bootstrap path")
if iterate.count("bootstrap_tox(t);") != 1:
    raise SystemExit("tcp-relay-retry-source: periodic retry does not restore relays")
if "tox_options_set_udp_enabled(opt, false);" not in open_body:
    raise SystemExit("tcp-relay-retry-source: TCP-only privacy mode changed")

# Every packet crosses the relay set, so it must stay wide and key-pinned.
table = re.search(
    r"static const struct bootstrap_node bootstrap_nodes\[\]\s*=\s*\{(.*?)\n\};",
    SOURCE,
    re.S,
)
if not table:
    raise SystemExit("tcp-relay-retry-source: bootstrap node table missing")
entries = re.findall(r'\{\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*"([0-9A-F]{64})"',
                     table.group(1))
if len(entries) < 8:
    raise SystemExit(
        "tcp-relay-retry-source: relay set narrowed to %d nodes" % len(entries)
    )
hosts = [entry[0] for entry in entries]
keys = [entry[3] for entry in entries]
if len(set(hosts)) != len(hosts) or len(set(keys)) != len(keys):
    raise SystemExit("tcp-relay-retry-source: duplicate relay entry")
if sum(1 for host in hosts if not re.fullmatch(r"[0-9.]+", host)) < 3:
    raise SystemExit("tcp-relay-retry-source: too few relays reached by hostname")

# A configured proxy must be applied, and an unusable one must fail closed
# rather than silently connecting directly.
# User-supplied relays must extend or replace the pinned set through the same
# single registration site, and an unusable relay file must fail closed.
if "omaq_relays_load(home, &relays)" not in open_body:
    raise SystemExit("tcp-relay-retry-source: relays.conf is not consulted")
if "OMAQ_TOX_RELAYS_INVALID" not in open_body:
    raise SystemExit("tcp-relay-retry-source: relay config failure is not fail-closed")
if "t->relays" not in bootstrap:
    raise SystemExit("tcp-relay-retry-source: user relays are not registered")
if "t->relays.exclusive" not in bootstrap:
    raise SystemExit("tcp-relay-retry-source: exclusive relay mode is not honoured")

for needle in (
    "omaq_proxy_load(home, &proxy)",
    "tox_options_set_proxy_type(",
    "tox_options_set_proxy_host(opt, proxy.host)",
    "tox_options_set_proxy_port(",
    "OMAQ_TOX_PROXY_INVALID",
):
    if needle not in open_body:
        raise SystemExit("tcp-relay-retry-source: missing proxy support: %s" % needle)
if open_body.index("omaq_proxy_load(home, &proxy)") > open_body.index("tox_new(opt"):
    raise SystemExit("tcp-relay-retry-source: proxy applied after tox_new")

print(
    "tcp-relay-retry-source: ok startup=relays periodic=relays udp=false "
    "relays=%d proxy=optional user-relays=optional" % len(entries)
)
