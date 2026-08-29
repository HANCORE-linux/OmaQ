#!/bin/sh
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
chat="$root/ChatSurface.qml"
coordinator="$root/SurfaceCoordinator.qml"

grep -q 'readonly property bool isSurfaceOwner: OmaQ.SurfaceCoordinator.owner === root' "$chat"
grep -q 'root.ownershipTeardown = true' "$chat"
grep -q 'if (root.isSurfaceOwner && !root.ownershipTeardown' "$chat"
grep -q 'model: root.isSurfaceOwner && root.floatRulesReady ? root.openCards : null' "$chat"
python3 - "$chat" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
start = text.index("    onExited: function(code) {", text.index("id: installFloatRules"))
end = text.index("  Timer {", start)
handler = text[start:end]
failure = handler.index("if (code !== 0)")
ready = handler.index("root.floatRulesReady = true")
if failure >= ready or "installFloatRulesRetry.restart()" not in handler[failure:ready]:
    raise SystemExit("surface-owner: failed rule install can release a first map")
if "floatRuleReloadBlocked" not in text[text.index("id: installFloatRulesRetry"):]:
    raise SystemExit("surface-owner: reload recovery cannot retry")
if "OmaQ.SurfaceCoordinator.queueDemo()" not in text or \
        "OmaQ.SurfaceCoordinator.deliverPendingDemo()" not in handler:
    raise SystemExit("surface-owner: demo requests bypass reload recovery")
PY
grep -q 'function unregisterHost(host)' "$coordinator"
grep -q 'coordinator.selectOwner()' "$coordinator"
grep -q 'property bool pendingDemoOpen: false' "$coordinator"
grep -q 'property string pendingKey: ""' "$coordinator"
grep -q 'host.acceptOpenRequest(conversation, expectedKey, name, monitor)' "$coordinator"
echo "surface-owner: ok"
