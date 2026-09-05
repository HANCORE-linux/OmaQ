#!/usr/bin/env python3
"""Enforce explicit plain-text rendering at OmaQ's QML trust boundary."""

from html.parser import HTMLParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import contextlib
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
QML_POLICY_SHA256 = {
    "CallTone.qml": "12d873ec1b774ed038fb526b0b2b7fd2a1a71e97d9987aa27fef06f6f4d93ddc",
    "ChatSurface.qml": "4511c3f3fd2b3838c52c5d574f13b420c0e200da5eb363362b645ee3fa9436a9",
    "Panel.qml": "201fb007e8c5ff1960a72d930aaf86a421c6691c34b0189e707cb070a086e71b",
    "PlacementController.qml": "82f72fcee9a6aceeb6d1015095fea4961eb2cddbdc11943ef410e160060df76a",
    "SafeText.qml": "a8bfa2ea5e13cbd50bf7e9c70995bea06ceeaca9c9d61e63b243ce18a830e354",
    "Service.qml": "41b79733f49adafe804e498e5989a4a49662ffd7d74de9cee84fbd09c69e5842",
    "SurfaceCoordinator.qml": "c206242de180c0b3a02b5ac50af9ba7e2486be1b0f585ed6ab8983979b0666f0",
    "pages/ChatPage.qml": "268e5e022958c09d1ef808bf5c8efe364784745ee162203be189a67ba95becc1",
}
TEXT_KINDS = (
    "Controls.TextArea",
    "Controls.Label",
    "OmaQ.SafeText",
    "SafeText",
    "TextEdit",
    "TextArea",
    "Label",
    "Text",
)
TEXT_OBJECT = re.compile(
    r"^(?P<indent>\s*)(?:(?:contentItem|delegate)\s*:\s*)?"
    r"(?P<kind>" + "|".join(re.escape(kind) for kind in TEXT_KINDS) + r")\s*\{\s*$"
)
TEXT_OBJECT_ANYWHERE = re.compile(
    r"\b(?:" + "|".join(re.escape(kind) for kind in TEXT_KINDS) + r")\s*\{"
)
TEXT_OBJECT_MULTILINE = re.compile(
    r"\b(?:" + "|".join(re.escape(kind) for kind in TEXT_KINDS) + r")\s*\{",
    re.S,
)
TOOLTIP_OBJECT = re.compile(
    r"^(?P<indent>\s*)(?:component\s+\w+\s*:\s*)?Controls\.ToolTip\s*\{\s*$"
)
EXTERNAL_INSTANCE = re.compile(
    r"^(?P<indent>\s*)(?:(?:component\s+\w+\s*:\s*)|(?:delegate\s*:\s*))?"
    r"(?P<kind>ChatBtn|FormatBtn|EmojiPickerBtn|SurfaceBtn|ContextMenuItem|"
    r"BarIconButton|PanelSectionHeader)\s*\{\s*$"
)
INHERITED_TEXT_CONTROL = (
    r"(?:Controls\.)?(?:Button|ToolButton|RoundButton|TabButton|DelayButton|"
    r"CheckBox|RadioButton|Switch|ItemDelegate|CheckDelegate|RadioDelegate|"
    r"SwitchDelegate|MenuItem)"
)
EXTERNAL_ANYWHERE = re.compile(
    r"\b(?:" + INHERITED_TEXT_CONTROL +
    r"|ChatBtn|FormatBtn|EmojiPickerBtn|SurfaceBtn|ContextMenuItem|"
    r"BarIconButton|PanelSectionHeader)\s*\{"
)
EXTERNAL_MULTILINE = re.compile(
    r"\b(?:" + INHERITED_TEXT_CONTROL +
    r"|ChatBtn|FormatBtn|EmojiPickerBtn|SurfaceBtn|ContextMenuItem|"
    r"BarIconButton|PanelSectionHeader)\s*\{",
    re.S,
)
TOOLTIP_MULTILINE = re.compile(r"\bControls\.ToolTip\s*\{", re.S)
WRAPPER_DECLARATION = re.compile(
    r"^\s*component\s+(?P<name>ChatBtn|SurfaceBtn)\s*:\s*Button\s*\{\s*$"
)
MENU_WRAPPER_DECLARATION = re.compile(
    r"^\s*component\s+ContextMenuItem\s*:\s*Controls\.MenuItem\s*\{\s*$"
)
EMPTY_CONTEXT_MENU = re.compile(r"^\s*delegate\s*:\s*ContextMenuItem\s*\{\s*}\s*$")
FORBIDDEN_INDIRECT = re.compile(r"\b(?:Binding|PropertyChanges)\b")
RELEVANT_KIND_COMMENT = re.compile(
    r"\b(?:Text|SafeText|OmaQ\.SafeText|Label|TextEdit|TextArea|Controls\.Label|Controls\.TextArea|" +
    INHERITED_TEXT_CONTROL + r")\s*/[/*]"
)
TEXT_FORMAT_ASSIGNMENT = re.compile(
    r"(?:\.textFormat|\[\s*[\"']textFormat[\"']\s*\])\s*="
)
DYNAMIC_TEXT_PROPERTY = re.compile(
    r"\.setProperty\s*\(\s*[\"'`](?:text|tooltipText|textFormat)[\"'`]"
)
SET_PROPERTY_CALL = re.compile(
    r"\b(?P<id>[A-Za-z_][A-Za-z0-9_]*)\.setProperty\s*\("
)
MODEL_SET_PROPERTY_IDS = {"lines", "openCardModel"}
LITERAL_BRACKET_WRITE = re.compile(
    r"\[[^\]\n]*[\"'`][^\]\n]*\]\s*="
)
LITERAL_BRACKET_ACCESS = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\[\s*[\"'`]")
DYNAMIC_QML = re.compile(r"\bQt\.createQmlObject\s*\(")
TEXT_FORMAT_ALIAS = re.compile(
    r"\bproperty\s+alias\s+\w+\s*:\s*\w+\.textFormat\b"
)
CONTROL_OBJECT = re.compile(r"\bControls\.(?P<kind>[A-Za-z_][A-Za-z0-9_]*)\s*\{")
ALLOWED_CONTROL_OBJECTS = {
    "Label", "Menu", "MenuItem", "Popup", "ScrollBar", "TextArea", "ToolTip",
}
DYNAMIC_COMPONENT = re.compile(
    r"\b(?:Loader|StackView)\s*\{|\b(?:createQmlObject|createComponent)\b|\bQt\s*\["
)
ANY_TEXT_PROPERTY_WRITE = re.compile(
    r"(?:\?[ \t]*)?\.[ \t]*(?:text|tooltipText|iconText)\b[ \t]*"
    r"(?:\+=|-=|\*=|/=|\?\?=|\|\|=|&&=|=(?!=))"
)
IMPERATIVE_TEXT_ASSIGNMENT = re.compile(
    r"\b(?P<id>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\.(?P<dot>text|tooltipText|iconText)|"
    r"\s*\[\s*[\"'](?P<bracket>text|tooltipText|iconText)[\"']\s*\])"
    r"\s*(?P<operator>\+=|-=|\*=|/=|\?\?=|\|\|=|&&=|=)"
)
COMPUTED_PROPERTY_WRITE = re.compile(
    r"\b(?P<id>[A-Za-z_][A-Za-z0-9_]*)\s*\[[^\]]+\]\s*"
    r"(?:\+=|-=|\*=|/=|\?\?=|\|\|=|&&=|=(?!=))"
)
COMPUTED_WRITE_ALLOWLIST = {
    "CallTone.qml": {"next"},
    "ChatSurface.qml": {"next", "pending"},
    "Panel.qml": {"entry", "found", "parts"},
    "Service.qml": {
        "actors", "authoritativeNext", "bound", "command", "failedClearNext",
        "failedHistoryKeys", "failedHistoryPending", "failedHistoryRequests",
        "failedRequests", "failedTransferIds", "groupBuild", "groupTypingNext",
        "historyKeyNext", "historyPendingNext", "historyRequestNext", "keyNext",
        "memberBuild", "memberKeys", "next", "nextActors", "nextGroups", "pending",
        "pendingClearNext", "pendingNext", "remainingSoundRequests", "requestNext",
        "retryAfterNext", "retryNext", "soundIds", "transferIds", "typingNext",
        "unreadNext", "updatedFriend",
    },
    "pages/ChatPage.qml": {
        "breakAt", "choices", "counts", "emittedMarkers", "literalMarkerAt",
        "order", "parts", "syntheticAt",
    },
}
REVIEWED_COMPUTED_IDS = set().union(*COMPUTED_WRITE_ALLOWLIST.values())
COMPUTED_WRITE_SOURCE_SHA256 = {
    "CallTone.qml": "8d9a0af95e58b888dfd09e37c198684843aa10d306f815abc868b88c61496c86",
    "ChatSurface.qml": "76f3165f9d8bb0324f3793b9d5587571cc5af0f6f5d6d64c25f4fbbf48fb686a",
    "Panel.qml": "edfd5835cd46f89bce5ce2c0ace5b4fbfc946875acead1180828cb24b3473938",
    "Service.qml": "ac9e4248f0a9385eeb4d0db01357229005f49a10d74c5a85226366a8d92f8124",
    "pages/ChatPage.qml": "179f2865c6e3118ce2243aef366d3900c2102a58969baed5507d0382dcfc2b00",
}
FUNCTION_PARAMETERS = re.compile(
    r"\bfunction(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\((?P<params>[^)]*)\)"
)
ARROW_PARAMETERS = re.compile(
    r"(?:\((?P<list>[^)]*)\)|(?P<single>[A-Za-z_][A-Za-z0-9_]*))\s*=>"
)
ANY_TEXT_MUTATION_ACCESS = re.compile(
    r"(?:\?[ \t]*)?\.[ \t]*(?:append|clear|cut|insert|paste|redo|remove|undo)\b"
    r"(?:[ \t]*\(|[ \t]*\?\.[ \t]*\(|[ \t]*\.[ \t]*(?:apply|bind|call)\b)"
)
TEXT_MUTATION_ACCESS = re.compile(
    r"\b(?P<id>[A-Za-z_][A-Za-z0-9_]*)"
    r"\.(?P<method>append|clear|cut|insert|paste|redo|remove|undo)\b(?P<after>\s*.)"
)
SINK_ALIAS = re.compile(
    r"\b(?:(?:var|let|const)\s+|property\s+var\s+)"
    r"(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*(?:=|:)\s*\(*\s*"
    r"(?P<target>[A-Za-z_][A-Za-z0-9_]*)\b"
)
OBJECT_ALIAS = re.compile(
    r"\bproperty\s+alias\s+(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*"
    r"(?P<target>[A-Za-z_][A-Za-z0-9_]*)\b"
)
PLAIN_INPUT_SHADOW = re.compile(
    r"\b(?:var|let|const|property\s+(?:var|string|url|int|real|bool))\s+"
    r"(?P<id>chatSearchField|filePath|groupNameField|importPath|input|nicknameField|passField|unlockField)\b"
)
PLAIN_INPUT_PARAMETER = re.compile(
    r"(?:\bfunction(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*|\bcatch\s*)\([^)]*"
    r"\b(?P<id>chatSearchField|filePath|groupNameField|importPath|input|nicknameField|passField|unlockField)\b"
)
PLAIN_INPUT_ARROW_PARAMETER = re.compile(
    r"(?:\([^)]*\b|\b)(?P<id>chatSearchField|filePath|groupNameField|importPath|input|nicknameField|passField|unlockField)"
    r"\b[^=\n]*=>"
)
PLAIN_INPUT_OBJECT = re.compile(
    r"^(?P<indent>\s*)(?P<kind>TextField|TokenTextField|Controls\.TextArea)\s*\{\s*$"
)
PLAIN_INPUT_IDS = {
    "chatSearchField", "filePath", "groupNameField", "importPath", "input", "nicknameField",
    "passField", "unlockField",
}
EXPECTED_PLAIN = {
    "OmaQ.SafeText": "Text.PlainText",
    "SafeText": "Text.PlainText",
    "Text": "Text.PlainText",
    "Label": "Text.PlainText",
    "Controls.Label": "Text.PlainText",
    "TextEdit": "TextEdit.PlainText",
    "TextArea": "TextEdit.PlainText",
    "Controls.TextArea": "TextEdit.PlainText",
}
ALLOWED_EXTERNAL_LABEL_REFS = {
    "ChatBtn": {"root.groupInviteFriendId"},
    "FormatBtn": set(),
    "EmojiPickerBtn": set(),
    "SurfaceBtn": {"pinPage.autoOpenEnabled", "root.muted"},
}
HEADER_TEXT_BINDING = (
    '{ var name = root.escapeMarkup(root.demo ? "DEMO" : '
    '(root.peerName || root.conversation || "chat")); '
    'if (root.demo) return "<font color=\'" + String(root.peerNameColor) + '
    '"\'><b>" + name + "</b></font>"; var status = root.peerConnectionStatus; '
    'return "<font color=\'" + String(root.peerNameColor) + "\'><b>" + name + '
    '"</b></font> <font color=\'" + String(root.peerStatusColor) + "\'>· " + '
    'root.escapeMarkup(status) + "</font>";'
)
MESSAGE_TEXT_BINDING = (
    '!line.smileOnly && model.dir !== "sys" ? '
    'root.messageMarkup(model.text, model.reply, line.edited) : ""'
)
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
KEYWORDS = {"true", "false", "null", "undefined"}


def fail(message: str) -> None:
    print(f"qml-plaintext: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def format_qml(path: Path) -> str:
    """Use Qt's parser/formatter as the grammar authority before policy checks."""
    runtime = Path("/usr/lib/qt6/bin/qmlformat")
    if not runtime.is_file():
        fail("Qt 6 qmlformat is required for the QML trust-boundary scan")
    result = subprocess.run(
        [str(runtime), "--ignore-settings", "-w", "2", "-W", "-1", str(path)],
        check=False, capture_output=True, text=True, timeout=20,
    )
    if result.returncode != 0 or not result.stdout:
        try:
            display = str(path.relative_to(ROOT))
        except ValueError:
            display = path.name
        fail(f"qmlformat rejected {display}: {result.stderr.strip()}")
    return result.stdout


def format_qml_text(source: str) -> str:
    with tempfile.TemporaryDirectory(prefix="omaq-qmlformat-") as directory:
        path = Path(directory) / "fixture.qml"
        path.write_text(source)
        return format_qml(path)


def object_end(lines: list[str], start: int, path: Path) -> int:
    indent = re.match(r"^(\s*)", lines[start]).group(1)
    closing = re.compile(r"^" + re.escape(indent) + r"}\s*$")
    for index in range(start + 1, len(lines)):
        if closing.match(lines[index]):
            return index
    fail(f"unterminated formatted object at {path.relative_to(ROOT)}:{start + 1}")
    raise AssertionError


def direct_property(block: list[str], indent: str, name: str) -> str:
    prefix = indent + "  "
    pattern = re.compile(r"^" + re.escape(prefix) + re.escape(name) + r"\s*:\s*(.*)$")
    for index, line in enumerate(block):
        match = pattern.match(line)
        if not match:
            continue
        value = [match.group(1).strip()]
        for continuation in block[index + 1 :]:
            if continuation.startswith(prefix) and not continuation.startswith(prefix + " "):
                break
            if continuation.strip():
                value.append(continuation.strip())
        return " ".join(value)
    return ""


def object_id(block: list[str], indent: str) -> str:
    value = direct_property(block, indent, "id")
    return value if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) else ""


def check_plain_input_contracts(qml_lines: dict[Path, list[str]]) -> None:
    approved_counts = {sink_id: 0 for sink_id in PLAIN_INPUT_IDS}
    declared_counts = {sink_id: 0 for sink_id in PLAIN_INPUT_IDS}
    declaration = re.compile(
        r"^\s*id\s*:\s*(?P<id>" + "|".join(sorted(PLAIN_INPUT_IDS)) + r")\s*$"
    )
    for path, lines in qml_lines.items():
        for line in lines:
            match = declaration.match(line)
            if match:
                declared_counts[match.group("id")] += 1
        for index, line in enumerate(lines):
            match = PLAIN_INPUT_OBJECT.match(line)
            if not match:
                continue
            end = object_end(lines, index, path)
            sink_id = object_id(lines[index + 1 : end], match.group("indent"))
            if sink_id in approved_counts:
                approved_counts[sink_id] += 1
    for sink_id in sorted(PLAIN_INPUT_IDS):
        if declared_counts[sink_id] != 1 or approved_counts[sink_id] != 1:
            fail(f"plain-input mutation exception {sink_id!r} is not exactly one reviewed input object")


def expression_references(value: str) -> set[str]:
    without_strings = STRING.sub("", value)
    return {token for token in IDENTIFIER.findall(without_strings) if token not in KEYWORDS}


def external_label_safe(kind: str, value: str) -> bool:
    allowed = ALLOWED_EXTERNAL_LABEL_REFS[kind]
    return expression_references(value) <= allowed


def rich_binding_kind(kind: str, object_name: str, value: str) -> str:
    if kind == "Text" and object_name == '"chatHeaderText"' and value == HEADER_TEXT_BINDING:
        return "header"
    if kind == "TextEdit" and object_name == '"messageText"' and value == MESSAGE_TEXT_BINDING:
        return "message"
    return ""


def declaration_coverage(source: str) -> tuple[bool, bool, bool]:
    lines = source.splitlines()
    canonical_text = sum(1 for line in lines if TEXT_OBJECT.match(line))
    canonical_tooltips = sum(1 for line in lines if TOOLTIP_OBJECT.match(line))
    canonical_external = sum(
        1 for line in lines
        if (EXTERNAL_INSTANCE.match(line) or WRAPPER_DECLARATION.match(line) or
            MENU_WRAPPER_DECLARATION.match(line) or EMPTY_CONTEXT_MENU.match(line))
    )
    return (
        len(TEXT_OBJECT_MULTILINE.findall(source)) == canonical_text,
        len(TOOLTIP_MULTILINE.findall(source)) == canonical_tooltips,
        len(EXTERNAL_MULTILINE.findall(source)) == canonical_external,
    )


def check_canonical_declarations(path: Path, source: str, lines: list[str]) -> None:
    text_covered, tooltips_covered, external_covered = declaration_coverage(source)
    if not text_covered:
        fail(f"non-canonical or unscanned text object in {path.relative_to(ROOT)}")
    if not tooltips_covered:
        fail(f"non-canonical or unscanned tooltip in {path.relative_to(ROOT)}")
    if not external_covered:
        fail(f"direct or unscanned external text component in {path.relative_to(ROOT)}")

    for index, line in enumerate(lines):
        if TEXT_OBJECT_ANYWHERE.search(line) and not TEXT_OBJECT.match(line):
            fail(f"non-canonical or unscanned text object at {path.relative_to(ROOT)}:{index + 1}")
        if "Controls.ToolTip" in line and "{" in line and not TOOLTIP_OBJECT.match(line):
            fail(f"non-canonical or unscanned tooltip at {path.relative_to(ROOT)}:{index + 1}")
        if EXTERNAL_ANYWHERE.search(line):
            if (EXTERNAL_INSTANCE.match(line) or WRAPPER_DECLARATION.match(line) or
                    MENU_WRAPPER_DECLARATION.match(line) or EMPTY_CONTEXT_MENU.match(line) or
                    re.match(r"^\s*component\s+(?:FormatBtn|EmojiPickerBtn)\s*:\s*ChatBtn\s*\{\s*$", line)):
                continue
            fail(f"direct or unscanned external text component at {path.relative_to(ROOT)}:{index + 1}")


def check_forbidden_patterns(path: Path, source: str) -> None:
    for line in source.splitlines():
        if line.startswith("import QtQuick.Controls") and line != "import QtQuick.Controls as Controls":
            fail(f"QtQuick.Controls must use the reviewed Controls alias in {path.relative_to(ROOT)}")
    if re.search(r"\b(?:Text|TextEdit)\.AutoText\b", source):
        fail(f"AutoText token is forbidden in {path.relative_to(ROOT)}")
    if FORBIDDEN_INDIRECT.search(source):
        fail(f"Binding or PropertyChanges can bypass a reviewed text sink in {path.relative_to(ROOT)}")
    if RELEVANT_KIND_COMMENT.search(source):
        fail(f"comment-obscured text object is forbidden in {path.relative_to(ROOT)}")
    for line in source.splitlines():
        if "textFormat" in line and not re.match(r"^\s*textFormat\s*:", line):
            fail(f"non-declarative textFormat use is forbidden in {path.relative_to(ROOT)}")
    if TEXT_FORMAT_ASSIGNMENT.search(source):
        fail(f"imperative textFormat mutation is forbidden in {path.relative_to(ROOT)}")
    if DYNAMIC_TEXT_PROPERTY.search(source):
        fail(f"dynamic text property mutation is forbidden in {path.relative_to(ROOT)}")
    for call in SET_PROPERTY_CALL.finditer(source):
        if call.group("id") not in MODEL_SET_PROPERTY_IDS:
            fail(f"setProperty on an unreviewed object is forbidden in {path.relative_to(ROOT)}")
    if LITERAL_BRACKET_WRITE.search(source) or LITERAL_BRACKET_ACCESS.search(source):
        fail(f"literal computed-property access is forbidden in {path.relative_to(ROOT)}")
    relative_path = str(path.relative_to(ROOT))
    formatted_digest = hashlib.sha256(source.encode()).hexdigest()
    allowed_computed_writes = set()
    if formatted_digest == COMPUTED_WRITE_SOURCE_SHA256.get(relative_path):
        allowed_computed_writes = COMPUTED_WRITE_ALLOWLIST.get(relative_path, set())
    for assignment in COMPUTED_PROPERTY_WRITE.finditer(source):
        if assignment.group("id") not in allowed_computed_writes:
            fail(f"computed write to unreviewed map {assignment.group('id')!r} in {path.relative_to(ROOT)}")
    if DYNAMIC_QML.search(source) or DYNAMIC_COMPONENT.search(source):
        fail(f"dynamic QML creation or loading is forbidden in {path.relative_to(ROOT)}")
    if TEXT_FORMAT_ALIAS.search(source):
        fail(f"textFormat alias is forbidden in {path.relative_to(ROOT)}")
    for line in source.splitlines():
        for control in CONTROL_OBJECT.finditer(line):
            kind = control.group("kind")
            if kind not in ALLOWED_CONTROL_OBJECTS or (
                    kind == "MenuItem" and not MENU_WRAPPER_DECLARATION.match(line)):
                fail(f"unreviewed inherited Controls.{kind} in {path.relative_to(ROOT)}")
    source_without_strings = STRING.sub(lambda match: " " * len(match.group(0)), source)
    source_without_noncode = COMMENT.sub(
        lambda match: re.sub(r"[^\n]", " ", match.group(0)), source_without_strings)
    direct_assignments = list(IMPERATIVE_TEXT_ASSIGNMENT.finditer(source_without_noncode))
    for assignment in direct_assignments:
        prop = assignment.group("dot") or assignment.group("bracket")
        if (prop != "text" or assignment.group("operator") != "=" or
                assignment.group("id") not in PLAIN_INPUT_IDS):
            fail(f"imperative {prop} assignment to an unreviewed sink in {path.relative_to(ROOT)}")
    for assignment in ANY_TEXT_PROPERTY_WRITE.finditer(source_without_noncode):
        if not any(direct.start() <= assignment.start() < direct.end()
                   for direct in direct_assignments):
            fail(f"non-canonical imperative text assignment in {path.relative_to(ROOT)}")
    if (re.search(r"\bObject\.(?:assign|defineProperty|defineProperties)\s*\(", source) or
            re.search(r"\bReflect\b", source)):
        fail(f"reflective object access is forbidden in {path.relative_to(ROOT)}")
    shadow = PLAIN_INPUT_SHADOW.search(source)
    parameter = PLAIN_INPUT_PARAMETER.search(source) or PLAIN_INPUT_ARROW_PARAMETER.search(source)
    if shadow or parameter:
        match = shadow or parameter
        fail(f"plain-input exception name {match.group('id')!r} is shadowed in {path.relative_to(ROOT)}")
    direct_mutations = list(TEXT_MUTATION_ACCESS.finditer(source_without_noncode))
    for access in direct_mutations:
        if (access.group("id") not in PLAIN_INPUT_IDS | MODEL_SET_PROPERTY_IDS or
                access.group("after").strip() != "("):
            fail(f"text-like mutation method on an unreviewed object in {path.relative_to(ROOT)}")
    for access in ANY_TEXT_MUTATION_ACCESS.finditer(source_without_noncode):
        if not any(direct.start() <= access.start() < direct.end()
                   for direct in direct_mutations):
            fail(f"non-canonical text mutation access in {path.relative_to(ROOT)}")
    for function in FUNCTION_PARAMETERS.finditer(source):
        if set(IDENTIFIER.findall(function.group("params"))) & REVIEWED_COMPUTED_IDS:
            fail(f"reviewed map identifier is shadowed by a function parameter in {path.relative_to(ROOT)}")
    for function in ARROW_PARAMETERS.finditer(source):
        params = function.group("list") or function.group("single") or ""
        if set(IDENTIFIER.findall(params)) & REVIEWED_COMPUTED_IDS:
            fail(f"reviewed map identifier is shadowed by an arrow parameter in {path.relative_to(ROOT)}")
    for line in source.splitlines():
        unqualified = re.match(r"^\s*(text|tooltipText|textFormat)\s*=\s*(.*)$", line)
        if unqualified and not (
                unqualified.group(1) == "text" and
                (unqualified.group(2).startswith("text.replace(") or
                 unqualified.group(2).startswith("limited"))):
            fail(f"unqualified imperative text mutation in {path.relative_to(ROOT)}")


def check_text_objects(path: Path, lines: list[str], rich: list[tuple[str, int, str]],
                       text_ids: set[str]) -> None:
    for index, line in enumerate(lines):
        match = TEXT_OBJECT.match(line)
        if not match:
            continue
        end = object_end(lines, index, path)
        block = lines[index + 1 : end]
        item_id = object_id(block, match.group("indent"))
        if item_id:
            text_ids.add(item_id)
        value = direct_property(block, match.group("indent"), "textFormat")
        location = f"{path.relative_to(ROOT)}:{index + 1}"
        if not value:
            if match.group("kind") in {"SafeText", "OmaQ.SafeText"}:
                continue
            fail(f"missing explicit textFormat at {location}")
        if "AutoText" in value:
            fail(f"AutoText is forbidden at {location}")
        expected = EXPECTED_PLAIN[match.group("kind")]
        if value == expected:
            continue
        approved = rich_binding_kind(
            match.group("kind"),
            direct_property(block, match.group("indent"), "objectName"),
            direct_property(block, match.group("indent"), "text"),
        )
        if ((match.group("kind") == "Text" and value == "Text.RichText") or
                (match.group("kind") == "TextEdit" and value == "TextEdit.RichText")):
            if approved:
                rich.append((str(path.relative_to(ROOT)), index + 1, approved))
                continue
            fail(f"unapproved rich-text data flow at {location}")
        fail(f"unexpected textFormat {value!r} at {location}")


def check_tooltips(path: Path, lines: list[str]) -> None:
    for index, line in enumerate(lines):
        match = TOOLTIP_OBJECT.match(line)
        if not match:
            continue
        end = object_end(lines, index, path)
        block = lines[index + 1 : end]
        content = direct_property(block, match.group("indent"), "contentItem")
        location = f"{path.relative_to(ROOT)}:{index + 1}"
        if not content.startswith(("Text {", "SafeText {", "OmaQ.SafeText {")):
            fail(f"tooltip relies on an implicit or unreviewed text item at {location}")


def check_external_components(path: Path, lines: list[str], external_ids: set[str]) -> None:
    for index, line in enumerate(lines):
        if MENU_WRAPPER_DECLARATION.match(line):
            end = object_end(lines, index, path)
            block = lines[index + 1 : end]
            if not direct_property(block, re.match(r"^(\s*)", line).group(1),
                                   "contentItem").startswith("RowLayout {"):
                fail("ContextMenuItem does not replace the inherited AutoText content item")
            continue
        wrapper = WRAPPER_DECLARATION.match(line)
        if wrapper:
            end = object_end(lines, index, path)
            block = lines[index + 1 : end]
            if direct_property(block, re.match(r"^(\s*)", line).group(1), "tooltipText") != '""':
                fail(f"{wrapper.group('name')} does not disable the inherited AutoText tooltip")
            continue

        match = EXTERNAL_INSTANCE.match(line)
        if not match:
            continue
        end = object_end(lines, index, path)
        block = lines[index + 1 : end]
        kind = match.group("kind")
        item_id = object_id(block, match.group("indent"))
        if item_id:
            external_ids.add(item_id)
        text = direct_property(block, match.group("indent"), "text")
        tooltip = direct_property(block, match.group("indent"), "tooltipText")
        icon = direct_property(block, match.group("indent"), "iconText")
        location = f"{path.relative_to(ROOT)}:{index + 1}"

        if kind in {"ChatBtn", "SurfaceBtn", "FormatBtn", "EmojiPickerBtn"}:
            if tooltip and tooltip != '""':
                fail(f"external {kind} re-enables its inherited AutoText tooltip at {location}")
            if icon and expression_references(icon):
                fail(f"dynamic text reaches external {kind} icon at {location}")
        if kind in ALLOWED_EXTERNAL_LABEL_REFS:
            if text and not external_label_safe(kind, text):
                fail(f"non-allowlisted expression reaches external {kind} label at {location}")
            continue
        if kind == "PanelSectionHeader":
            if text and not re.fullmatch(r'"[^"\n]*"', text):
                fail(f"dynamic text reaches external PanelSectionHeader at {location}")
            continue
        if kind == "BarIconButton":
            allowed = {"omaq.incomingCall", "omaq.pending", "omaq.pendingGroup"}
            if expression_references(text) - allowed or expression_references(tooltip) - allowed:
                fail(f"remote value reaches external BarIconButton text at {location}")


def check_sink_assignments(path: Path, source: str, text_ids: set[str],
                           external_ids: set[str]) -> None:
    aliases = set(text_ids | external_ids)
    external_aliases = set(external_ids)
    changed = True
    while changed:
        changed = False
        for match in list(SINK_ALIAS.finditer(source)) + list(OBJECT_ALIAS.finditer(source)):
            alias = match.group("alias")
            target = match.group("target")
            if target in aliases and alias not in aliases:
                aliases.add(alias)
                changed = True
            if target in external_aliases:
                external_aliases.add(alias)
    for item_id in sorted(external_aliases):
        item = r"\b" + re.escape(item_id)
        operator = r"\s*(?:\+=|-=|\*=|/=|\?\?=|\|\|=|&&=|=)"
        if (re.search(item + r"\.(?:text|tooltipText|iconText)" + operator, source) or
                re.search(item + r"\s*\[\s*[\"'](?:text|tooltipText|iconText)[\"']\s*\]" + operator, source) or
                re.search(item + r"\.setProperty\s*\(\s*[\"'](?:text|tooltipText|iconText)[\"']", source)):
            fail(f"runtime assignment can bypass a reviewed text gate for id {item_id!r} in {path.relative_to(ROOT)}")
    for assignment in COMPUTED_PROPERTY_WRITE.finditer(source):
        if assignment.group("id") in aliases:
            fail(f"computed runtime assignment can bypass a reviewed text gate in {path.relative_to(ROOT)}")
    if re.search(r"\bproperty\s+alias\s+\w+\s*:\s*\w+\.(?:text|tooltipText|iconText)\b", source):
        fail(f"property alias can bypass a reviewed text gate in {path.relative_to(ROOT)}")


def require_source_contracts() -> None:
    panel = (ROOT / "Panel.qml").read_text()
    chat = (ROOT / "ChatSurface.qml").read_text()
    page = (ROOT / "pages/ChatPage.qml").read_text()

    if "text: surfaceButton.helpText" not in chat:
        fail("SurfaceBtn does not route help text through its local tooltip")
    if "text: chatButton.helpText" not in page:
        fail("ChatBtn does not route help text through OmaqTooltip")
    if "root.escapeMarkup(replyPreview)" not in page:
        fail("reply preview is no longer escaped before rich-text rendering")
    if "var text = root.preserveLiteralSeparators(root.escapeMarkup(value))" not in page:
        fail("message Markdown no longer starts from escaped input")
    if "tokenButton.activeFocusReason === Qt.TabFocusReason" not in panel:
        fail("TokenButton keyboard focus no longer exposes its safe tooltip")


def extract_js_function(source: str, name: str) -> str:
    start_match = re.search(
        r"(?m)^(?P<indent>\s*)function\s+" + re.escape(name) + r"\s*\(",
        source,
    )
    if not start_match:
        fail(f"missing renderer function {name}")
    following = re.search(
        r"(?m)^" + re.escape(start_match.group("indent")) +
        r"function\s+\w+\s*\(",
        source[start_match.end() :],
    )
    if not following:
        fail(f"renderer function {name} is not followed by a canonical function boundary")
    end = start_match.end() + following.start()
    function_source = source[start_match.start() : end].strip()
    if not function_source.endswith("}"):
        fail(f"renderer function {name} has a non-canonical end")
    return function_source


class RichMarkupValidator(HTMLParser):
    ALLOWED = {"a", "b", "br", "font", "i"}

    def __init__(self) -> None:
        super().__init__(convert_charrefs=False)
        self.errors: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag not in self.ALLOWED:
            self.errors.append(f"forbidden tag {tag}")
            return
        attributes = dict(attrs)
        if tag == "a":
            href = attributes.get("href", "")
            if set(attributes) != {"href"} or not re.match(r"^(?:https?://|mailto:)", href):
                self.errors.append("unsafe anchor attributes")
        elif tag == "font":
            if set(attributes) != {"color"} or not attributes.get("color"):
                self.errors.append("unsafe font attributes")
        elif attributes:
            self.errors.append(f"attributes on {tag}")

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag != "br" or attrs:
            self.errors.append(f"forbidden self-closing tag {tag}")

    def handle_endtag(self, tag: str) -> None:
        if tag not in self.ALLOWED or tag == "br":
            self.errors.append(f"forbidden closing tag {tag}")

    def handle_decl(self, decl: str) -> None:
        self.errors.append(f"forbidden declaration {decl}")



def render_hostile_messages(url: str) -> list[str]:
    page = format_qml(ROOT / "pages/ChatPage.qml")
    functions = "\n".join(extract_js_function(page, name) for name in (
        "escapeMarkup", "preserveLiteralSeparators", "markdownInline",
        "markdownText", "messageMarkup",
    ))
    hostile = f'<img src="{url}" onerror="fetch(1)"><script>alert(1)</script>'
    reply = f'<object data="{url}">reply</object>'
    script = f"""
{functions}
const hostile = {json.dumps(hostile)};
const reply = {json.dumps(reply)};
const root = {{
  accent: "#55aaff", fg: "#eeeeee",
  escapeMarkup, preserveLiteralSeparators, markdownInline, markdownText,
  replyPreviewFor: function(id) {{ return id ? reply : ""; }}
}};
process.stdout.write(JSON.stringify([
  messageMarkup(hostile, "", false),
  messageMarkup("safe", "reply", false)
]));
"""
    result = subprocess.run(
        ["node", "-e", script], check=False, capture_output=True, text=True,
        timeout=10,
    )
    if result.returncode != 0:
        fail(f"message renderer fixture failed: {result.stderr.strip()}")
    try:
        rendered = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        fail(f"message renderer returned invalid fixture JSON: {error}")
    if not isinstance(rendered, list) or len(rendered) != 2:
        fail("message renderer fixture returned an unexpected result")
    return [str(value) for value in rendered]


def validate_rich_output(markup: str) -> None:
    validator = RichMarkupValidator()
    validator.feed(markup)
    validator.close()
    if validator.errors:
        fail(f"renderer emitted unsafe RichText: {validator.errors!r}: {markup!r}")
    if re.search(r"<\s*(?:img|object|embed|iframe|script|style|link|video|audio|source)\b",
                 markup, re.I):
        fail(f"renderer emitted a resource-bearing tag: {markup!r}")


class ProbeHandler(BaseHTTPRequestHandler):
    hits: list[str] = []
    lock = threading.Lock()
    png = bytes.fromhex(
        "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
        "0000000d49444154789c6360000000020001e221bc330000000049454e44ae426082"
    )

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        with self.lock:
            self.hits.append(self.path)
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(self.png)))
        self.end_headers()
        self.wfile.write(self.png)

    def log_message(self, format_string: str, *args: object) -> None:
        del format_string, args


def run_richtext_fixture(markup: str) -> None:
    runtime = Path("/usr/lib/qt6/bin/qml")
    if not runtime.is_file():
        fail("Qt 6 qml runtime is required for the RichText network fixture")
    qml = f"""import QtQuick
Item {{
  width: 320; height: 200
  Text {{ textFormat: Text.RichText; text: {json.dumps(markup)} }}
  Timer {{ interval: 800; running: true; onTriggered: Qt.quit() }}
}}
"""
    with tempfile.TemporaryDirectory(prefix="omaq-qml-plaintext-") as directory:
        fixture = Path(directory) / "fixture.qml"
        fixture.write_text(qml)
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = subprocess.run(
            [str(runtime), str(fixture)], check=False, capture_output=True,
            text=True, timeout=8, env=environment,
        )
    if result.returncode != 0:
        fail(f"Qt RichText fixture failed: {result.stderr.strip()}")


def run_richtext_mutation_fixture(url: str, method: str) -> None:
    runtime = Path("/usr/lib/qt6/bin/qml")
    extra = ""
    if method == "compound":
        sink = 'Text { id: victim; textFormat: Text.RichText; text: "safe" }'
        mutation = f"victim.text += {json.dumps(f'<img src={url!r}>')}"
    elif method == "insert":
        sink = 'TextEdit { id: victim; textFormat: TextEdit.RichText; text: "safe" }'
        mutation = f"victim.insert(victim.length, {json.dumps(f'<img src={url!r}>')})"
    else:
        raise AssertionError(method)
    qml = f"""import QtQuick
Item {{
  width: 320; height: 200
  {sink}
  {extra}
  Component.onCompleted: {mutation}
  Timer {{ interval: 800; running: true; onTriggered: Qt.quit() }}
}}
"""
    with tempfile.TemporaryDirectory(prefix="omaq-qml-mutation-") as directory:
        fixture = Path(directory) / "fixture.qml"
        fixture.write_text(qml)
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = subprocess.run(
            [str(runtime), str(fixture)], check=False, capture_output=True,
            text=True, timeout=8, env=environment,
        )
    if result.returncode != 0:
        fail(f"Qt RichText {method} mutation fixture failed: {result.stderr.strip()}")


def run_groupbox_fixture(url: str) -> None:
    runtime = Path("/usr/lib/qt6/bin/qml")
    qml = f"""import QtQuick
import QtQuick.Controls as Controls
Item {{
  width: 320; height: 200
  Controls.GroupBox {{ title: {json.dumps(f"<img src='{url}'>")} }}
  Timer {{ interval: 800; running: true; onTriggered: Qt.quit() }}
}}
"""
    with tempfile.TemporaryDirectory(prefix="omaq-qml-groupbox-") as directory:
        fixture = Path(directory) / "fixture.qml"
        fixture.write_text(qml)
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = subprocess.run(
            [str(runtime), str(fixture)], check=False, capture_output=True,
            text=True, timeout=8, env=environment,
        )
    if result.returncode != 0:
        fail(f"Qt GroupBox fixture failed: {result.stderr.strip()}")


def check_richtext_runtime() -> None:
    ProbeHandler.hits = []
    server = ThreadingHTTPServer(("127.0.0.1", 0), ProbeHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        base = f"http://127.0.0.1:{server.server_port}"
        rendered = render_hostile_messages(base + "/sanitized")
        for markup in rendered:
            validate_rich_output(markup)
        if "&lt;img" not in rendered[0] or "&lt;object" not in rendered[1]:
            fail("hostile message or reply markup is not preserved as escaped text")
        run_richtext_fixture(f"<img src='{base}/raw'>")
        with ProbeHandler.lock:
            raw_hit = "/raw" in ProbeHandler.hits
        if not raw_hit:
            fail("RichText HTTP control fixture did not observe its expected request")
        run_groupbox_fixture(base + "/groupbox")
        with ProbeHandler.lock:
            if "/groupbox" not in ProbeHandler.hits:
                fail("inherited GroupBox HTTP control fixture did not observe its request")
        for method in ("compound", "insert"):
            run_richtext_mutation_fixture(base + "/" + method, method)
            with ProbeHandler.lock:
                if "/" + method not in ProbeHandler.hits:
                    fail(f"RichText {method} mutation control fixture did not observe its request")
        run_richtext_fixture(rendered[0] + rendered[1])
        with ProbeHandler.lock:
            if "/sanitized" in ProbeHandler.hits:
                fail("escaped message RichText performed an HTTP resource request")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def expect_forbidden(source: str, label: str) -> None:
    formatted = format_qml_text(source)
    with contextlib.redirect_stderr(io.StringIO()):
        try:
            check_forbidden_patterns(ROOT / "fixture.qml", formatted)
        except SystemExit:
            return
    fail(f"full policy accepted adversarial fixture {label}")


def expect_full_policy_forbidden(source: str, label: str,
                                 path: Path | None = None) -> None:
    path = path or ROOT / "fixture.qml"
    formatted = format_qml_text(source)
    lines = formatted.splitlines()
    rich: list[tuple[str, int, str]] = []
    external_ids: set[str] = set()
    text_ids: set[str] = set()
    with contextlib.redirect_stderr(io.StringIO()):
        try:
            check_forbidden_patterns(path, formatted)
            check_canonical_declarations(path, formatted, lines)
            check_text_objects(path, lines, rich, text_ids)
            check_tooltips(path, lines)
            check_external_components(path, lines, external_ids)
            check_sink_assignments(path, formatted, text_ids, external_ids)
        except SystemExit:
            return
    fail(f"full policy accepted adversarial fixture {label}")


def check_adversarial_controls() -> None:
    declaration_fixtures = (
        "Button {\n  text: service.lastChatText\n}",
        "Button\n{\n  text: service.lastChatText\n}",
        "Text { text: service.lastChatText }",
        "Text\n{\n  text: service.lastChatText\n}",
        "Controls.ToolTip\n{\n  text: service.lastChatText\n}",
        "Controls.MenuItem {\n  text: service.lastChatText\n}",
    )
    for source in declaration_fixtures:
        formatted = format_qml_text(
            "import QtQuick\nimport QtQuick.Controls as Controls\n" + source + "\n"
        )
        lines = formatted.splitlines()
        if source.startswith("Text"):
            start = next(i for i, line in enumerate(lines) if TEXT_OBJECT.match(line))
            match = TEXT_OBJECT.match(lines[start])
            end = object_end(lines, start, ROOT / "fixture.qml")
            if direct_property(lines[start + 1 : end], match.group("indent"),
                               "textFormat"):
                fail(f"missing-format fixture unexpectedly declares textFormat {source!r}")
        elif source.startswith("Controls.ToolTip"):
            start = next(i for i, line in enumerate(lines) if TOOLTIP_OBJECT.match(line))
            match = TOOLTIP_OBJECT.match(lines[start])
            end = object_end(lines, start, ROOT / "fixture.qml")
            if direct_property(lines[start + 1 : end], match.group("indent"),
                               "contentItem"):
                fail("implicit-tooltip fixture unexpectedly replaces its content item")
        elif declaration_coverage(formatted)[2]:
            fail(f"external-control scanner accepts adversarial fixture {source!r}")

    safe_text_source = format_qml_text(
        "import QtQuick\nItem {\n  SafeText { text: service.lastChatText }\n}\n"
    )
    safe_text_rich: list[tuple[str, int, str]] = []
    safe_text_ids: set[str] = set()
    check_text_objects(
        ROOT / "fixture.qml", safe_text_source.splitlines(),
        safe_text_rich, safe_text_ids,
    )
    expect_full_policy_forbidden(
        "import QtQuick\nItem {\n"
        "  SafeText { textFormat: Text.RichText; text: service.lastChatText }\n"
        "}\n",
        "SafeText RichText override",
    )

    malicious = (
        "service.lastChatText",
        "omaq.groupName(id)",
        "modelData.title",
        'root.muted ? service.lastChatText : "Mute"',
    )
    for value in malicious:
        if external_label_safe("SurfaceBtn", value):
            fail(f"external-label policy accepts adversarial expression {value!r}")
    if rich_binding_kind(
        "Text", '"chatHeaderText"',
        '{ var safe = root.escapeMarkup(root.peerName) return root.peerName }',
    ):
        fail("header RichText policy accepts an unrelated escaping call")
    if rich_binding_kind(
        "TextEdit", '"messageText"',
        'root.messageMarkup(model.text, model.reply, line.edited); model.text',
    ):
        fail("message RichText policy accepts raw text after escaped markup")
    nested_source = """import QtQuick
Item {
  property string hostile: "<img src='http://127.0.0.1/'>"
  Text {
    text: {
      return /}/ / 1, parent.hostile
      textFormat: Text.PlainText
    }
  }
}
"""
    nested_formatted = format_qml_text(nested_source)
    nested_lines = nested_formatted.splitlines()
    nested_start = next(
        (index for index, line in enumerate(nested_lines) if TEXT_OBJECT.match(line)),
        -1,
    )
    if nested_start < 0:
        fail("Qt parser fixture lost its adversarial Text object")
    nested_match = TEXT_OBJECT.match(nested_lines[nested_start])
    nested_end = object_end(nested_lines, nested_start, ROOT / "fixture.qml")
    nested_block = nested_lines[nested_start + 1 : nested_end]
    if direct_property(nested_block, nested_match.group("indent"), "textFormat"):
        fail("formatted object scanner accepts a nested JavaScript textFormat decoy")
    if not re.search(r"\bText\.AutoText\b", "textFormat: Text.AutoText"):
        fail("global AutoText scanner misses an adversarial sink")
    comment_fixture = format_qml_text(
        "import QtQuick\nText /* comment */ { text: parent.hostile }\n"
    )
    if not RELEVANT_KIND_COMMENT.search(comment_fixture):
        fail("comment-obscured text object is not rejected")
    for indirect in (
        'Binding { target: victim; property: "text"; value: service.lastChatText }',
        'Binding /* comment */ { target: victim; property: "text"; value: service.lastChatText }',
        'PropertyChanges { target: victim; text: service.lastChatText }',
    ):
        if not FORBIDDEN_INDIRECT.search(indirect):
            fail(f"indirect-assignment scanner accepts {indirect!r}")
    for mutation in (
        'victim.textFormat = Text.RichText',
        'victim["textFormat"] = Text.AutoText',
        '`${victim.textFormat = Text.AutoText}`',
        'var item = victim; item.textFormat = Text.RichText',
    ):
        if not TEXT_FORMAT_ASSIGNMENT.search(mutation):
            fail(f"textFormat mutation scanner accepts {mutation!r}")
    if not DYNAMIC_TEXT_PROPERTY.search('victim.setProperty("textFormat", Text.RichText)'):
        fail("setProperty textFormat mutation is not rejected")
    if not DYNAMIC_QML.search('Qt.createQmlObject("Bind" + "ing {}", parent)'):
        fail("dynamic QML creation is not rejected")
    for dynamic in (
        'Loader { source: service.remoteQmlUrl }',
        'Qt["create" + "QmlObject"](source, parent)',
        'Qt["create" + "Component"](source)',
    ):
        if not DYNAMIC_COMPONENT.search(dynamic):
            fail(f"dynamic component scanner accepts {dynamic!r}")
    alias_assignment = IMPERATIVE_TEXT_ASSIGNMENT.search(
        "var alias = victim; alias.text = service.lastChatText"
    )
    if not alias_assignment or alias_assignment.group("id") in PLAIN_INPUT_IDS:
        fail("alias-based imperative text assignment is not rejected")
    inherited_fixture = format_qml_text(
        "import QtQuick\nimport QtQuick.Controls as Controls\n"
        "Controls.ToolButton { text: service.lastChatText }\n"
    )
    if declaration_coverage(inherited_fixture)[2]:
        fail("inherited ToolButton text control is not rejected")
    for kind in ("GroupBox", "Dialog", "MenuBarItem", "SwipeDelegate"):
        match = CONTROL_OBJECT.search(f"Controls.{kind} {{")
        if not match or match.group("kind") in ALLOWED_CONTROL_OBJECTS:
            fail(f"inherited Controls.{kind} is not denied by default")
    expect_forbidden(
        "import QtQuick\nimport QtQuick.Controls\n"
        "GroupBox { title: service.lastChatText }\n",
        "unaliased GroupBox",
    )
    expect_forbidden(
        "import QtQuick\nimport QtQuick.Controls as C\n"
        "C.Dialog { title: service.lastChatText }\n",
        "alternate Controls alias",
    )
    expect_forbidden(
        "import QtQuick\nLoader { source: service.remoteQmlUrl }\n",
        "remote Loader source",
    )
    expect_forbidden(
        "import QtQuick\nBinding /* comment */ { property: \"text\" }\n",
        "comment-obscured Binding",
    )
    expect_forbidden(
        """import QtQuick
Item {
  property string hostile: "<img src='http://127.0.0.1/'>"
  Text { id: victim; textFormat: Text.PlainText; text: parent.hostile }
  Component.onCompleted: {
    victim["text" + "Format"] = Text.RichText
    victim["te" + "xt"] = hostile
  }
}
""",
        "computed text and textFormat writes",
    )
    expect_forbidden(
        """import QtQuick
Item {
  Text { id: victim; textFormat: Text.PlainText; text: "safe" }
  Component.onCompleted: victim.setProperty(`text`, parent.hostile)
}
""",
        "template setProperty",
    )
    for mutation, label in (
        ("victim.text += service.lastChatText", "compound text assignment"),
        ("victim.insert(0, service.lastChatText)", "TextEdit insert mutation"),
        ("victim.paste()", "TextEdit paste mutation"),
        ("victim.undo()", "TextEdit undo mutation"),
        ("Object.assign(victim, { text: service.lastChatText })", "Object.assign text mutation"),
        ("Reflect.set(victim, \"text\", service.lastChatText)", "Reflect.set text mutation"),
    ):
        expect_full_policy_forbidden(
            "import QtQuick\nItem {\n"
            "  TextEdit { id: victim; textFormat: TextEdit.PlainText; text: \"safe\" }\n"
            f"  Component.onCompleted: {mutation}\n"
            "}\n",
            label,
        )
    for target, alias in (
        ("victim", ""),
        ("alias", "var alias = victim; "),
        ("alias", "var alias = (victim); "),
    ):
        expect_full_policy_forbidden(
            "import QtQuick\nItem {\n"
            "  Text { id: victim; textFormat: Text.PlainText; text: \"safe\" }\n"
            "  Component.onCompleted: { var key = service.remoteProperty; " + alias +
            f"{target}[key] = service.lastChatText }}\n"
            "}\n",
            f"computed text write through {alias or target}",
        )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  property alias victimAlias: victim
  Text { id: victim; textFormat: Text.PlainText; text: "safe" }
  Component.onCompleted: {
    var key = service.remoteProperty
    victimAlias[key] = service.lastChatText
  }
}
""",
        "computed text write through QML object alias",
    )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  Text { id: label; textFormat: Text.PlainText; text: "safe" }
  Component.onCompleted: {
    var input = label
    input.text = service.lastChatText
  }
}
""",
        "plain-input exception name shadowing",
    )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  Text { id: label; textFormat: Text.PlainText; text: "safe" }
  Component.onCompleted: (function(input) {
    input.text = service.lastChatText
  })(label)
}
""",
        "anonymous-function plain-input shadowing",
    )
    for mutation in (
        'var setter = Reflect.set; setter(label, "text", service.lastChatText)',
        'Reflect.set.call(null, label, "text", service.lastChatText)',
        '(label).text = service.lastChatText',
        'label.insert.call(label, 0, service.lastChatText)',
        '(label).insert(0, service.lastChatText)',
        '(label).insert?.(0, service.lastChatText)',
        'label?.insert(0, service.lastChatText)',
        'label /* split */ .insert(0, service.lastChatText)',
        'var writer = label.insert.bind(label); writer(0, service.lastChatText)',
    ):
        expect_full_policy_forbidden(
            "import QtQuick\nItem {\n"
            "  TextEdit { id: label; textFormat: TextEdit.PlainText; text: \"safe\" }\n"
            f"  Component.onCompleted: {{ {mutation} }}\n"
            "}\n",
            mutation,
        )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  Text { id: label; textFormat: Text.PlainText; text: "safe" }
  Component.onCompleted: {
    var key = service.remoteProperty
    label[
      // split
      key
    ] = service.lastChatText
  }
}
""",
        "multiline computed text write",
    )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  function poison(next) {
    var key = service.remoteProperty
    next[key] = service.lastChatText
  }
}
""",
        "reviewed computed-map identifier shadowing",
        ROOT / "Service.qml",
    )
    expect_full_policy_forbidden(
        """import QtQuick
Item {
  property string hostile: service.lastChatText
  SafeText { id: victim; text: parent.hostile }
  Component.onCompleted: {
    var next = (true ? victim : victim)
    var key = "text" + "Format"
    next[key] = Text.RichText
  }
}
""",
        "conditional SafeText alias computed-format write",
        ROOT / "Service.qml",
    )
    for prop in ("iconText", "tooltipText"):
        expect_full_policy_forbidden(
            "import QtQuick\nItem {\n"
            "  component ChatBtn: Button { tooltipText: \"\" }\n"
            f"  ChatBtn {{ id: victim; {prop}: service.lastChatText }}\n"
            "}\n",
            f"external ChatBtn {prop}",
        )
    alias_fixture = "property alias unsafeText: externalButton.text"
    if not re.search(
        r"\bproperty\s+alias\s+\w+\s*:\s*\w+\.(?:text|tooltipText|iconText)\b",
        alias_fixture,
    ):
        fail("external-label policy does not recognize alias bypasses")


def check_reviewed_qml_snapshot(paths: list[Path]) -> None:
    actual_paths = {str(path.relative_to(ROOT)) for path in paths}
    if actual_paths != set(QML_POLICY_SHA256):
        fail("QML source set changed without a reviewed plaintext-policy snapshot update")
    for relative, expected in sorted(QML_POLICY_SHA256.items()):
        digest = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
        if digest != expected:
            fail(f"{relative} changed without a reviewed plaintext-policy snapshot update")


def main() -> None:
    rich: list[tuple[str, int, str]] = []
    qml_sources: list[tuple[Path, str]] = []
    qml_lines: dict[Path, list[str]] = {}
    external_ids: set[str] = set()
    text_ids: set[str] = set()
    qml_paths = sorted(ROOT.rglob("*.qml"))
    check_reviewed_qml_snapshot(qml_paths)
    for path in qml_paths:
        source = format_qml(path)
        lines = source.splitlines()
        qml_sources.append((path, source))
        qml_lines[path] = lines
        check_forbidden_patterns(path, source)
        check_canonical_declarations(path, source, lines)
        check_text_objects(path, lines, rich, text_ids)
        check_tooltips(path, lines)
        check_external_components(path, lines, external_ids)

    check_plain_input_contracts(qml_lines)
    for path, source in qml_sources:
        check_sink_assignments(path, source, text_ids, external_ids)
    kinds = sorted(item[2] for item in rich)
    if kinds != ["header", "message"]:
        fail(f"expected exactly the escaped header and message RichText sinks, got {rich!r}")
    require_source_contracts()
    check_adversarial_controls()
    check_richtext_runtime()
    print("qml-plaintext: ok")


if __name__ == "__main__":
    main()
