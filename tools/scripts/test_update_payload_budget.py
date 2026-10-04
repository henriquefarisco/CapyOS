"""Boundary coverage for the exact update limit shared by build and signer."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from update_manifest_common import PAYLOAD_MAX_BYTES
from verify_update_payload_budget import LEGACY_CACHE_MAX_BYTES


class PayloadBudgetTests(unittest.TestCase):
    def test_legacy_cache_boundaries(self):
        script = Path(__file__).with_name("verify_update_payload_budget.py")
        with tempfile.TemporaryDirectory() as directory:
            payload = Path(directory) / "bridge.bin"
            for size, accepted in ((LEGACY_CACHE_MAX_BYTES, True),
                                   (LEGACY_CACHE_MAX_BYTES + 1, False)):
                with self.subTest(size=size):
                    with payload.open("wb") as stream:
                        stream.truncate(size)
                    result = subprocess.run([sys.executable, str(script), "--payload",
                                             str(payload), "--legacy-cache"],
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode == 0, accepted,
                                     result.stdout + result.stderr)

    def test_boundaries(self):
        script = Path(__file__).with_name("verify_update_payload_budget.py")
        with tempfile.TemporaryDirectory() as directory:
            payload = Path(directory) / "kernel.bin"
            for size, accepted in ((0, False), (1, True),
                                   (PAYLOAD_MAX_BYTES, True),
                                   (PAYLOAD_MAX_BYTES + 1, False)):
                with self.subTest(size=size):
                    with payload.open("wb") as stream:
                        stream.truncate(size)
                    result = subprocess.run([sys.executable, str(script), "--payload", str(payload)],
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode == 0, accepted, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
