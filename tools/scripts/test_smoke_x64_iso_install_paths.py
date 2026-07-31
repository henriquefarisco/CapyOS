#!/usr/bin/env python3

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from smoke_x64_iso_install import require_safe_disk_path, safe_disk_roots


class SmokeDiskPathSafetyTests(unittest.TestCase):
    def test_native_scratch_directory_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = root / "repo"
            scratch = root / "capyos-smoke-scratch"
            scratch.mkdir()
            with patch.dict(
                os.environ, {"CAPYOS_SMOKE_SCRATCH": str(scratch)}, clear=False
            ):
                roots = safe_disk_roots(repo)
                self.assertIn(scratch.resolve(), roots)
                require_safe_disk_path(repo, scratch / "install.img")

    def test_scratch_symlink_cannot_expand_allowed_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = root / "repo"
            alias = root / "capyos-smoke-scratch"
            outside = root / "outside"
            outside.mkdir()
            alias.symlink_to(outside, target_is_directory=True)
            with patch.dict(
                os.environ, {"CAPYOS_SMOKE_SCRATCH": str(alias)}, clear=False
            ):
                with self.assertRaisesRegex(ValueError, "must resolve"):
                    safe_disk_roots(repo)

    def test_disk_symlink_cannot_escape_safe_root(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = root / "repo"
            safe = repo / "build" / "ci"
            outside = root / "outside.img"
            safe.mkdir(parents=True)
            outside.touch()
            link = safe / "install.img"
            link.symlink_to(outside)
            with patch.dict(os.environ, {}, clear=False):
                os.environ.pop("CAPYOS_SMOKE_SCRATCH", None)
                with self.assertRaisesRegex(ValueError, "must stay under"):
                    require_safe_disk_path(repo, link)


if __name__ == "__main__":
    unittest.main()
