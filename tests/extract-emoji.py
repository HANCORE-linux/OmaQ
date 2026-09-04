#!/usr/bin/env python3
import hashlib
import importlib.util
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "omaq_extract_emoji", ROOT / "scripts" / "extract-emoji.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ExtractEmojiTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="omaq-extract-test.")
        base = Path(self.temporary.name)
        self.base = base
        self.root = base / "root"
        self.target = self.root / "assets" / "emoji" / "41.png"
        self.target.parent.mkdir(parents=True)
        self.font = base / "font.ttf"
        self.bin = base / "bin"
        self.bin.mkdir()
        magick = self.bin / "magick"
        magick.write_text(
            """#!/bin/sh
for destination do :; done
case ${FAKE_MAGICK_MODE:?} in
  under) printf '1234567' >"$destination" ;;
  exact) printf '12345678' >"$destination" ;;
  over) printf '123456789' >"$destination" ;;
  fifo) mkfifo "$destination" ;;
  hang)
    sleep 10 &
    child=$!
    printf '%s\n' "$child" >"${FAKE_PID_FILE:?}"
    wait "$child"
    ;;
  *) exit 64 ;;
esac
"""
        )
        magick.chmod(0o755)

        self.source = b"SRC"
        MODULE.FONT_SIZE = len(self.source)
        MODULE.FONT_SHA256 = hashlib.sha256(self.source).hexdigest()
        MODULE.MAX_EMOJI_OUTPUT = 8
        MODULE.MAGICK_TIMEOUT = 0.1
        MODULE.GLYPHS = ["A"]
        MODULE.SOURCE_SHA256 = {
            "41.png": hashlib.sha256(self.source).hexdigest()
        }
        MODULE.load_font = lambda _font: ({65: 1}, {1: object()}, b"")
        MODULE.glyph_png = lambda _locations, _data, _gid: self.source
        self.old_path = os.environ.get("PATH")
        os.environ["PATH"] = f"{self.bin}:{self.old_path or ''}"

    def tearDown(self):
        if self.old_path is None:
            os.environ.pop("PATH", None)
        else:
            os.environ["PATH"] = self.old_path
        os.environ.pop("FAKE_MAGICK_MODE", None)
        os.environ.pop("FAKE_PID_FILE", None)
        self.temporary.cleanup()

    def set_target_sentinel(self):
        self.target.write_bytes(b"sentinel")

    def run_mode(self, mode, expected):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = mode
        MODULE.OUTPUT_SHA256 = {"41.png": hashlib.sha256(expected).hexdigest()}
        MODULE.generate(self.root, self.font)
        self.assertEqual(self.target.read_bytes(), expected)

    def test_font_must_have_the_exact_pinned_size_and_hash(self):
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        for content in (self.source[:-1], b"BAD", self.source + b"!"):
            with self.subTest(content=content):
                self.set_target_sentinel()
                self.font.write_bytes(content)
                with self.assertRaises(SystemExit):
                    MODULE.generate(self.root, self.font)
                self.assertEqual(self.target.read_bytes(), b"sentinel")

    def test_font_symlinks_and_hard_links_are_rejected(self):
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        backing = self.base / "backing-font.ttf"
        backing.write_bytes(self.source)
        for link_type in ("symbolic", "hard"):
            with self.subTest(link_type=link_type):
                self.set_target_sentinel()
                self.font.unlink(missing_ok=True)
                if link_type == "symbolic":
                    self.font.symlink_to(backing)
                else:
                    os.link(backing, self.font)
                with self.assertRaises(SystemExit):
                    MODULE.generate(self.root, self.font)
                self.assertEqual(self.target.read_bytes(), b"sentinel")
                self.font.unlink()

    def test_output_destination_links_are_rejected(self):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        protected = self.base / "protected"
        for link_type in ("symbolic", "hard"):
            with self.subTest(link_type=link_type):
                self.target.unlink(missing_ok=True)
                protected.unlink(missing_ok=True)
                protected.write_bytes(b"protected")
                if link_type == "symbolic":
                    self.target.symlink_to(protected)
                else:
                    os.link(protected, self.target)
                with self.assertRaises(SystemExit):
                    MODULE.generate(self.root, self.font)
                self.assertEqual(protected.read_bytes(), b"protected")

    def test_symlinked_output_directory_is_rejected(self):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        self.target.parent.rmdir()
        protected_directory = self.base / "protected-directory"
        protected_directory.mkdir()
        protected = protected_directory / "protected"
        protected.write_bytes(b"protected")
        self.target.parent.symlink_to(protected_directory, target_is_directory=True)
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertEqual(protected.read_bytes(), b"protected")

    def test_symlinked_assets_parent_is_rejected(self):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        self.target.parent.rmdir()
        self.target.parent.parent.rmdir()
        protected_assets = self.base / "protected-assets"
        protected_emoji = protected_assets / "emoji"
        protected_emoji.mkdir(parents=True)
        protected = protected_emoji / "protected"
        protected.write_bytes(b"protected")
        self.target.parent.parent.symlink_to(protected_assets, target_is_directory=True)
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertEqual(protected.read_bytes(), b"protected")

    def test_group_or_world_writable_output_directory_is_rejected(self):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = "exact"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        self.set_target_sentinel()
        self.target.parent.chmod(0o777)
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertEqual(self.target.read_bytes(), b"sentinel")

    def test_magick_fifo_output_is_rejected_without_blocking(self):
        self.font.write_bytes(self.source)
        os.environ["FAKE_MAGICK_MODE"] = "fifo"
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"unused").hexdigest()
        }
        self.set_target_sentinel()
        started = time.monotonic()
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertEqual(self.target.read_bytes(), b"sentinel")

    def test_output_just_below_and_at_limit_is_accepted(self):
        self.run_mode("under", b"1234567")
        self.run_mode("exact", b"12345678")

    def test_output_over_limit_is_stopped_and_does_not_change_target(self):
        oversized = self.base / "oversized-output"
        os.environ["FAKE_MAGICK_MODE"] = "over"
        with self.assertRaises(SystemExit):
            MODULE.run_magick([str(self.bin / "magick"), str(oversized)])
        self.assertEqual(oversized.stat().st_size, MODULE.MAX_EMOJI_OUTPUT)

        self.set_target_sentinel()
        self.font.write_bytes(self.source)
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"123456789").hexdigest()
        }
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertEqual(self.target.read_bytes(), b"sentinel")

    def test_hanging_magick_process_group_is_killed_without_target_change(self):
        self.set_target_sentinel()
        self.font.write_bytes(self.source)
        pid_file = self.base / "magick-child.pid"
        os.environ["FAKE_MAGICK_MODE"] = "hang"
        os.environ["FAKE_PID_FILE"] = str(pid_file)
        MODULE.OUTPUT_SHA256 = {
            "41.png": hashlib.sha256(b"12345678").hexdigest()
        }
        started = time.monotonic()
        with self.assertRaises(SystemExit):
            MODULE.generate(self.root, self.font)
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertEqual(self.target.read_bytes(), b"sentinel")
        child_pid = int(pid_file.read_text())
        deadline = time.monotonic() + 2.0
        while Path(f"/proc/{child_pid}").exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertFalse(Path(f"/proc/{child_pid}").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
