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
if "HyprlandFocusGrab {" not in header or "windows: [popup]" not in header or \
        "onCleared:" not in header or "root.close()" not in header:
    raise SystemExit("input-mask: popup lost compositor click-away dismissal")
if "!root.avatarRestorePending" not in header or \
        'root.identityPickerMode === ""' not in header or \
        "!avatarPick.running && !identityPick.running" not in header:
    raise SystemExit("input-mask: pending or active external pickers can be dismissed by the panel focus grab")
if "onBackingWindowActiveChanged:" not in header:
    raise SystemExit("input-mask: popup lost non-grab focus dismissal")
button_start = text.index("  BarIconButton {\n    id: button")
button_end = text.index("  Rectangle {\n    id: unreadBadge", button_start)
button = text[button_start:button_end]
if "root.toggle()" not in button:
    raise SystemExit("input-mask: OmaQ bar action no longer toggles the panel")
if "panelCloseButton" in text or 'tooltipText: "Close panel"' in text:
    raise SystemExit("input-mask: redundant header Close action returned")
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
grep -q 'mask: Region { item: cardColumn }' "$chat"
echo "input-mask: ok"
