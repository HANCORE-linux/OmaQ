#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root/ChatSurface.qml" <<'PY'
from pathlib import Path
import sys

surface = Path(sys.argv[1]).read_text(encoding="utf-8")
for marker in (
    'id: chatDragHandle',
    'anchors.fill: parent',
    'pinWin.startSystemMove()',
    'onReleased: pinWin.requestCurrentGeometry(false)',
    'text: "Pop up: " + (pinPage.autoOpenEnabled ? "On" : "Off")',
    '? "Pop up for new messages: on" : "Pop up for new messages: off"',
    'onClicked: pinPage.autoOpenToggled()',
):
    if marker not in surface:
        raise SystemExit(f"floating-window-presentation: missing contract {marker!r}")
if 'text: "drag_indicator"' in surface:
    raise SystemExit("floating-window-presentation: visible drag indicator remains")

start = surface.index("          Item {\n            id: chatDragHandle")
end = surface.index("          SurfaceBtn {", start)
drag = surface[start:end]
for marker in ("MouseArea {", "anchors.fill: parent", "pinWin.startSystemMove()",
               "pinWin.requestCurrentGeometry(false)"):
    if marker not in drag:
        raise SystemExit(f"floating-window-presentation: drag area lost {marker!r}")

button = surface[end:surface.index("          SurfaceBtn {", end + 1)]
if "pinPage.autoOpenEnabled" not in button or "pinPage.autoOpenToggled()" not in button:
    raise SystemExit("floating-window-presentation: Pop up control lost per-chat state")
PY

sh "$root/tests/chat-surface-geometry.sh"
echo "floating-window-presentation: ok"
