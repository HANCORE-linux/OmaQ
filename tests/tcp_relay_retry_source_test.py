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

print("tcp-relay-retry-source: ok startup=relays periodic=relays udp=false")
