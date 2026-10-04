"""Bridge identity and publication-contract regressions; never waive production gates."""
from pathlib import Path
import hashlib
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from update_manifest_common import ManifestError, canonical_body
from verify_migration_bridge import ROOT_URL, SOURCE, stable_key, verify_bridge
from verify_update_payload_budget import LEGACY_CACHE_MAX_BYTES

ROOT = Path(__file__).resolve().parents[2]
FINAL = "0.11.3+20261004"
BRIDGE = "0.11.2+20261004"


class MigrationBridgeContractTests(unittest.TestCase):
    def fixture(self, root):
        for name, payload, version in (("bridge.ini", "capyos-bridge64.bin", BRIDGE),
                                       ("latest.ini", "capyos64.bin", FINAL)):
            data = b"\x7fELFfixture"
            (root / payload).write_bytes(data)
            fields = dict(available_version=version, channel="stable", branch="main",
                          source=SOURCE, published_at="2026-10-04",
                          payload_url=f"{ROOT_URL}v{FINAL}/{payload}",
                          payload_size=str(len(data)),
                          payload_sha256=hashlib.sha256(data).hexdigest())
            (root / name).write_bytes(canonical_body(fields) +
                                     b"signature_ed25519=" + b"00" * 64 + b"\n")

    def verify(self, root, **kwargs):
        verify_bridge(root, final_version=FINAL, bridge_version=kwargs.get("bridge", BRIDGE),
                      published_at="2026-10-04")

    def test_runtime_identity_is_distinct_only_in_bridge_build(self):
        header = (ROOT / "include/core/version.h").read_text()
        expected = re.search(r'#define CAPYOS_VERSION_FULL\s+"([^"]+)"', header)[1]
        for flags, version in (([], expected), (["-DCAPYOS_MIGRATION_BRIDGE"], BRIDGE)):
            result = subprocess.run(["gcc", "-E", "-P", "-I", str(ROOT / "include"),
                                     "-include", "core/runtime_version.h", *flags, "-"],
                                    input="CAPYOS_RUNTIME_VERSION_FULL\n", text=True,
                                    capture_output=True, check=True)
            self.assertEqual(result.stdout.strip(), f'"{version}"')

    def test_splash_http_and_updater_use_runtime_identity(self):
        for path in ("src/arch/x86_64/boot_splash.c",
                     "src/net/services/http/url_request_builder.c",
                     "src/arch/x86_64/kernel_boot_stages.c"):
            source = (ROOT / path).read_text()
            self.assertNotRegex(source, r"CAPYOS_VERSION_(FULL|EXTENDED)")
            self.assertIn('"core/runtime_version.h"', source)

    @patch("verify_migration_bridge.verify_signature")
    def test_exact_contract_and_both_signatures(self, signature):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            self.verify(root)
            self.assertEqual(signature.call_count, 2)

    @patch("verify_migration_bridge.verify_signature", side_effect=ManifestError("bad signature"))
    def test_bad_production_signature_stops_verification(self, signature):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            with self.assertRaisesRegex(ManifestError, "bad signature"):
                self.verify(root)
            self.assertEqual(signature.call_count, 1)

    @patch("verify_migration_bridge.verify_signature")
    def test_contract_mutations_fail_closed(self, signature):
        mutations = (("bridge.ini", b"channel=stable", b"channel=develop"),
                     ("bridge.ini", SOURCE.encode(), b"github:other/CapyOS"),
                     ("bridge.ini", b"/capyos-bridge64.bin", b"/other.bin"),
                     ("latest.ini", b"payload_size=11", b"payload_size=12"),
                     ("capyos-bridge64.bin", b"\x7fELF", b"bad!"))
        for name, old, new in mutations:
            with self.subTest(name=name, old=old), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                path = root / name
                raw = path.read_bytes()
                self.assertIn(old, raw)
                path.write_bytes(raw.replace(old, new))
                with self.assertRaises(ManifestError):
                    self.verify(root)

    def test_ordering_rejects_equal_newer_and_nonstable_bridge(self):
        for value in (FINAL, "0.12.0+20261004", "0.11.2-rc.1+20261004"):
            with self.subTest(value=value), self.assertRaises(ManifestError):
                self.verify(Path("unused"), bridge=value)
        self.assertLess(stable_key(BRIDGE), stable_key(FINAL))

    @patch("verify_migration_bridge.verify_signature")
    def test_legacy_bound_and_missing_materials(self, signature):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            with (root / "capyos-bridge64.bin").open("r+b") as stream:
                stream.truncate(LEGACY_CACHE_MAX_BYTES + 1)
            with self.assertRaisesRegex(ManifestError, "exceeds cache limit"):
                self.verify(root)
        for name in ("bridge.ini", "capyos-bridge64.bin", "latest.ini", "capyos64.bin"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root)
                (root / name).unlink()
                with self.assertRaises(ManifestError):
                    self.verify(root)


if __name__ == "__main__":
    unittest.main()
