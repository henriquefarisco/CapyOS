"""A cached kernel cannot hide an incomplete desktop source export."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
MAKE_ENV = {key: value for key, value in os.environ.items()
            if key not in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES")}


@unittest.skipUnless(shutil.which("make"), "GNU make is required")
class DesktopSourceContract(unittest.TestCase):
    def test_missing_source_fails_even_with_cached_kernel(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            kernel = root / "cached-kernel.bin"
            kernel.write_bytes(b"cached kernel must not mask missing source")
            source = root / "media_player.c"
            source.write_text("/* fixture only; the cached kernel is not relinked */\n")
            command = [
                "make", "--no-print-directory", "-s", str(kernel),
                f"CAPYOS_ELF64={kernel}", "CAPYOS64_OBJS=",
                "DESKTOP_OBJS=", "WINDOW_OBJS=",
                "APPS_OBJS=build/x86_64/capyui-apps/media_player.o",
                f"APPS_SRC_ROOT={root}",
            ]
            good = subprocess.run(command, cwd=REPO, env=MAKE_ENV, capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            source.unlink()
            missing = subprocess.run(command, cwd=REPO, env=MAKE_ENV, capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("incomplete desktop source integration", missing.stdout)
            self.assertIn("media_player.c", missing.stdout)
            self.assertEqual(kernel.read_bytes(), b"cached kernel must not mask missing source")

    def test_core_only_does_not_require_desktop_sources(self):
        result = subprocess.run(
            ["make", "--no-print-directory", "-s", "check-desktop-sources",
             "PROFILE=core-only", "CAPYUI_DIR=/nonexistent-capyui-contract-fixture"],
            cwd=REPO, env=MAKE_ENV, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
