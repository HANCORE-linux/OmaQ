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
python3 - "$root/pages/ChatPage.qml" <<'PY'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
start = text.index("  component OmaqTooltip: Controls.ToolTip {")
end = text.index("  component ChatBtn: Button {", start)
tooltip = text[start:end]
if "radius: Style.cornerRadius" not in tooltip:
    raise SystemExit("input-mask: chat tooltip lost the reactive theme radius")
PY
grep -q 'Keys.onEscapePressed: root.close()' "$panel"
grep -q 'id: panelCloseButton' "$panel"
grep -q 'tooltipText: "Close panel"' "$panel"
grep -q 'onClicked: root.close()' "$panel"
grep -q 'mask: Region { item: cardColumn }' "$chat"
echo "input-mask: ok"
