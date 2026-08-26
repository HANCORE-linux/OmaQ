#!/bin/sh
set -eu
root=$(unset CDPATH; cd -- "$(dirname "$0")/.." && pwd)
panel="$root/Panel.qml"
chat="$root/ChatSurface.qml"

grep -q 'mask: Region { item: card }' "$panel"
python3 - "$panel" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
start = text.index("  PanelWindow {\n    id: popup")
end = text.index("    Item {\n      id: connectedSurface", start)
header = text[start:end]
if "MouseArea {" in header or "passThroughBar" in header:
    raise SystemExit("input-mask: popup regained a desktop-sized pointer catcher")
if "onBackingWindowActiveChanged:" not in header or "root.close()" not in header:
    raise SystemExit("input-mask: popup lost click-away focus dismissal")
PY
grep -q 'Keys.onEscapePressed: root.close()' "$panel"
grep -q 'mask: Region { item: cardColumn }' "$chat"
echo "input-mask: ok"
