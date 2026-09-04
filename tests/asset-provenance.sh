#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)

python3 - "$root" <<'PY'
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

def read_regular(path, *, maximum, exact=None, directory=None):
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    try:
        descriptor = os.open(path, flags, dir_fd=directory)
    except OSError as error:
        raise SystemExit(f"asset-provenance: cannot safely open {path}: {error}") from error
    try:
        before = os.fstat(descriptor)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
            raise SystemExit(f"asset-provenance: unsafe file type or link count: {path}")
        if exact is not None and before.st_size != exact:
            raise SystemExit(f"asset-provenance: unexpected size: {path}")
        if before.st_size < 0 or before.st_size > maximum:
            raise SystemExit(f"asset-provenance: file exceeds limit: {path}")
        data = bytearray()
        while len(data) < before.st_size:
            chunk = os.read(descriptor, min(65_536, before.st_size - len(data)))
            if not chunk:
                raise SystemExit(f"asset-provenance: file changed while reading: {path}")
            data.extend(chunk)
        if os.read(descriptor, 1):
            raise SystemExit(f"asset-provenance: file grew while reading: {path}")
        after = os.fstat(descriptor)
        identity = lambda info: (
            info.st_dev, info.st_ino, info.st_mode, info.st_nlink,
            info.st_uid, info.st_gid, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns,
        )
        if identity(before) != identity(after):
            raise SystemExit(f"asset-provenance: file changed while reading: {path}")
        return bytes(data)
    finally:
        os.close(descriptor)


def read_text(path, *, maximum, exact=None, directory=None):
    try:
        return read_regular(
            path,
            maximum=maximum,
            exact=exact,
            directory=directory,
        ).decode("utf-8")
    except UnicodeDecodeError as error:
        raise SystemExit(f"asset-provenance: non-UTF-8 text: {path}") from error


def open_directory(path, *, parent=None):
    flags = os.O_RDONLY | os.O_CLOEXEC | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        descriptor = os.open(path, flags, dir_fd=parent)
    except OSError as error:
        raise SystemExit(
            f"asset-provenance: cannot safely open directory {path}: {error}"
        ) from error
    if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
        os.close(descriptor)
        raise SystemExit(f"asset-provenance: not a directory: {path}")
    return descriptor


def validate_inventory(directory, expected_files, expected_directories=()):
    entries = {entry.name: entry for entry in os.scandir(directory)}
    expected = set(expected_files) | set(expected_directories)
    if set(entries) != expected:
        raise SystemExit("asset-provenance: directory inventory changed")
    for name in expected_files:
        if not entries[name].is_file(follow_symlinks=False):
            raise SystemExit(f"asset-provenance: expected regular file: {name}")
    for name in expected_directories:
        if not entries[name].is_dir(follow_symlinks=False):
            raise SystemExit(f"asset-provenance: expected directory: {name}")


def assert_reader_boundaries():
    with tempfile.TemporaryDirectory(prefix="omaq-asset-reader.") as temporary:
        directory = Path(temporary)
        for size in (7, 8):
            candidate = directory / f"regular-{size}"
            candidate.write_bytes(b"x" * size)
            if len(read_regular(candidate, maximum=8)) != size:
                raise SystemExit("asset-provenance: bounded reader truncated input")
        oversized = directory / "regular-9"
        oversized.write_bytes(b"x" * 9)
        fifo = directory / "fifo"
        os.mkfifo(fifo)
        symlink = directory / "symlink"
        symlink.symlink_to(directory / "regular-8")
        hardlink = directory / "hardlink"
        os.link(directory / "regular-8", hardlink)
        for unsafe in (oversized, fifo, symlink, hardlink):
            try:
                read_regular(unsafe, maximum=8)
            except SystemExit:
                continue
            raise SystemExit(f"asset-provenance: unsafe reader fixture passed: {unsafe.name}")

        inventory = directory / "inventory"
        inventory.mkdir()
        known = inventory / "known.png"
        known.write_bytes(b"known")
        inventory_directory = open_directory(inventory)
        try:
            validate_inventory(inventory_directory, {"known.png"})
            extras = (
                ("EXTRA.PNG", "file"),
                ("extra-link", "symlink"),
                ("extra-fifo", "fifo"),
                ("extra-directory", "directory"),
            )
            for name, kind in extras:
                path = inventory / name
                if kind == "file":
                    path.write_bytes(b"extra")
                elif kind == "symlink":
                    path.symlink_to(known)
                elif kind == "fifo":
                    os.mkfifo(path)
                else:
                    path.mkdir()
                try:
                    try:
                        validate_inventory(inventory_directory, {"known.png"})
                    except SystemExit:
                        pass
                    else:
                        raise SystemExit(
                            f"asset-provenance: inventory fixture passed: {name}"
                        )
                finally:
                    if kind == "directory":
                        path.rmdir()
                    else:
                        path.unlink()
        finally:
            os.close(inventory_directory)

        real_directory = directory / "real-directory"
        real_directory.mkdir()
        linked_directory = directory / "linked-directory"
        linked_directory.symlink_to(real_directory, target_is_directory=True)
        try:
            open_directory(linked_directory)
        except SystemExit:
            pass
        else:
            raise SystemExit("asset-provenance: intermediate symlink fixture passed")


assert_reader_boundaries()
root = Path(sys.argv[1])
root_directory = open_directory(root)
assets_directory = open_directory("assets", parent=root_directory)
emoji_directory = open_directory("emoji", parent=assets_directory)
licenses_directory = open_directory("LICENSES", parent=emoji_directory)
scripts_directory = open_directory("scripts", parent=root_directory)
pages_directory = open_directory("pages", parent=root_directory)
packaging_directory = open_directory("packaging", parent=root_directory)
script = root / "scripts/extract-emoji.py"

license_bytes = read_regular(
    "OFL-1.1.txt",
    maximum=4_330,
    exact=4_330,
    directory=licenses_directory,
)
if hashlib.sha256(license_bytes).hexdigest() != (
    "500bb1ccf43df7bbb522112f9133a52b16e1c35e809632f5d8609b179152de5b"
):
    raise SystemExit("emoji-provenance: OFL text changed")

text = read_text("ATTRIBUTION.md", maximum=16_384, directory=emoji_directory)
source = read_text("extract-emoji.py", maximum=262_144, directory=scripts_directory)
required_attribution = (
    "Noto Color Emoji",
    "Version: `2.051`",
    "8998f5dd683424a73e2314a8c1f1e359c19e8742",
    "fonts/NotoColorEmoji.ttf",
    "72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b",
    "Copyright 2013 Google LLC",
    "OFL-1.1-no-RFN",
    "LICENSES/OFL-1.1.txt",
    "imagemagick 7.1.2.30-1",
    "-extent 136x136",
    "-filter Lanczos -resize 64x64 -strip",
)
for value in required_attribution:
    if value not in text:
        raise SystemExit(f"emoji-provenance: attribution lost {value!r}")
required_source = (
    'FONT_VERSION = "2.051"',
    'FONT_COMMIT = "8998f5dd683424a73e2314a8c1f1e359c19e8742"',
    'FONT_SHA256 = "72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b"',
    "font_sha256 != FONT_SHA256",
    'TemporaryDirectory(prefix="omaq-emoji-native.")',
    '"-strip"',
)
for value in required_source:
    if value not in source:
        raise SystemExit(f"emoji-provenance: extractor lost {value!r}")

row = re.compile(
    r"^\| `([^`]+\.png)` \| `([0-9a-f]{64})` \| `([0-9a-f]{64})` \|$",
    re.MULTILINE,
)
records = row.findall(text)
if len(records) != 22:
    raise SystemExit(f"emoji-provenance: expected 22 ledger rows, got {len(records)}")
ledger = {name: (original, distributed) for name, original, distributed in records}
if len(ledger) != len(records):
    raise SystemExit("emoji-provenance: duplicate ledger filename")
wanted_constants = {"GLYPHS", "SOURCE_SHA256", "OUTPUT_SHA256"}
extractor = {}
for statement in ast.parse(source, filename=str(script)).body:
    if (
        isinstance(statement, ast.Assign)
        and len(statement.targets) == 1
        and isinstance(statement.targets[0], ast.Name)
        and statement.targets[0].id in wanted_constants
    ):
        name = statement.targets[0].id
        if name in extractor:
            raise SystemExit(f"emoji-provenance: duplicate extractor constant: {name}")
        extractor[name] = ast.literal_eval(statement.value)
if set(extractor) != wanted_constants:
    raise SystemExit("emoji-provenance: extractor constants are missing or duplicated")
if extractor["SOURCE_SHA256"] != {
    name: original for name, (original, _distributed) in ledger.items()
}:
    raise SystemExit("emoji-provenance: source hash table and ledger differ")
if extractor["OUTPUT_SHA256"] != {
    name: distributed for name, (_original, distributed) in ledger.items()
}:
    raise SystemExit("emoji-provenance: output hash table and ledger differ")
qml = read_text("ChatPage.qml", maximum=524_288, directory=pages_directory)
emoji_set = re.search(
    r"readonly property var emojiSet:\s*(\[.*?\])",
    qml,
    re.DOTALL,
)
if emoji_set is None or json.loads(emoji_set.group(1)) != extractor["GLYPHS"]:
    raise SystemExit("emoji-provenance: extractor and QML emoji sets differ")
validate_inventory(
    emoji_directory,
    set(ledger) | {"ATTRIBUTION.md"},
    {"LICENSES"},
)
validate_inventory(licenses_directory, {"OFL-1.1.txt"})
for name, (_original, distributed) in ledger.items():
    actual = hashlib.sha256(
        read_regular(name, maximum=1_048_576, directory=emoji_directory)
    ).hexdigest()
    if distributed != actual:
        raise SystemExit(f"emoji-provenance: distributed hash changed for {name}")

third_party = read_text("THIRD_PARTY.md", maximum=65_536, directory=root_directory)
manifest = read_text("manifest.json", maximum=65_536, directory=root_directory)
pkgbuild = read_text("PKGBUILD", maximum=65_536, directory=packaging_directory)
if "Noto Color Emoji 2.051" not in third_party or "assets/emoji/ATTRIBUTION.md" not in third_party:
    raise SystemExit("emoji-provenance: THIRD_PARTY entry missing")
if hashlib.sha256(
    read_regular(
        "kofi-mono.svg",
        maximum=1_176,
        exact=1_176,
        directory=assets_directory,
    )
).hexdigest() != (
    "225b3203b688a7ebd95a8035fddd22300db96a65c8d645965948a714af9b963f"
):
    raise SystemExit("asset-provenance: Ko-fi asset changed")
third_party_words = " ".join(third_party.split())
for value in (
    "8c2d958a86d745a1f1553b6dabe8218ab0227405",
    "864a4d997519f937a57b11b4f052c100ca31d55936936ffaec5e653283f29e52",
    "225b3203b688a7ebd95a8035fddd22300db96a65c8d645965948a714af9b963f",
    "sounds/LICENSES/CC0-1.0.txt",
    "does not imply endorsement",
):
    if value not in third_party_words:
        raise SystemExit(f"asset-provenance: Ko-fi attribution lost {value!r}")
if manifest.count("OFL-1.1-no-RFN") != 1:
    raise SystemExit("emoji-provenance: manifest OFL expression changed")
expected_license = (
    "license=('MIT' 'GPL-3.0-only' 'Apache-2.0' 'CC-BY-SA-4.0' "
    "'CC0-1.0' 'OFL-1.1-no-RFN' 'custom:Pixabay Content License')"
)
if expected_license not in pkgbuild:
    raise SystemExit("emoji-provenance: PKGBUILD license array changed")
for descriptor in (
    packaging_directory,
    pages_directory,
    scripts_directory,
    licenses_directory,
    emoji_directory,
    assets_directory,
    root_directory,
):
    os.close(descriptor)
PY

echo "asset-provenance: ok"
