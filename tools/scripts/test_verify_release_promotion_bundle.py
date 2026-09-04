from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.scripts.verify_release_promotion_bundle import (
    PromotionBundleError,
    verify_bundle,
)


class VerifyReleasePromotionBundleTests(unittest.TestCase):
    def make_bundle(self, root: Path) -> Path:
        bundle = root / "bundle"
        bundle.mkdir()
        payloads = {
            "CapyOS-Installer-UEFI.iso": b"iso",
            "capyos64.bin": b"kernel",
            "manifest.bin": b"manifest",
            "modules-index.txt": b"index",
            "modules.sha256": b"modules",
            "org.capyos.ai.assistant-0.2.2.bin": b"ai",
        }
        for name, data in payloads.items():
            (bundle / name).write_bytes(data)
        lines = [
            f"{hashlib.sha256(payloads[name]).hexdigest()}  {name}"
            for name in sorted(payloads)
        ]
        (bundle / "release-artifacts.sha256").write_text(
            "\n".join(lines) + "\n", encoding="utf-8"
        )
        (bundle / "release-artifacts.sha256.sig").write_bytes(b"s" * 64)
        (bundle / "release-ed25519.pub.pem").write_text("public", encoding="utf-8")
        (bundle / "release-public-key.manifest").write_text(
            "key-manifest\n", encoding="utf-8"
        )
        (bundle / "release-publication.manifest").write_text(
            "publication-manifest\n", encoding="utf-8"
        )
        (bundle / "latest.ini").write_text("signed-update\n", encoding="utf-8")
        return bundle

    def test_accepts_exact_signed_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            assets = verify_bundle(self.make_bundle(Path(tmp)))
            self.assertEqual(len(assets), 12)

    def test_rejects_missing_signed_material(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            (bundle / "latest.ini").unlink()
            with self.assertRaisesRegex(PromotionBundleError, "missing assets"):
                verify_bundle(bundle)

    def test_rejects_extra_private_key(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            (bundle / "release-ed25519.pem").write_text("private", encoding="utf-8")
            with self.assertRaisesRegex(PromotionBundleError, "unexpected assets"):
                verify_bundle(bundle)

    def test_rejects_multiple_ai_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            (bundle / "org.capyos.ai.assistant-0.2.3.bin").write_bytes(b"ai-2")
            with self.assertRaisesRegex(PromotionBundleError, "exactly one CapyAI"):
                verify_bundle(bundle)

    def test_rejects_checksum_inventory_drift(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            path = bundle / "release-artifacts.sha256"
            path.write_text(path.read_text(encoding="utf-8").splitlines()[0] + "\n")
            with self.assertRaisesRegex(PromotionBundleError, "inventory differs"):
                verify_bundle(bundle)

    def test_rejects_checksum_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            (bundle / "capyos64.bin").write_bytes(b"tampered")
            with self.assertRaisesRegex(PromotionBundleError, "checksum mismatch"):
                verify_bundle(bundle)

    def test_rejects_wrong_signature_size(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            (bundle / "release-artifacts.sha256.sig").write_bytes(b"short")
            with self.assertRaisesRegex(PromotionBundleError, "64-byte"):
                verify_bundle(bundle)

    def test_rejects_backslash_in_checksum_asset_name(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            bundle = self.make_bundle(Path(tmp))
            checksums = bundle / "release-artifacts.sha256"
            checksums.write_text(
                checksums.read_text(encoding="utf-8").replace(
                    "  capyos64.bin\n", "  nested\\capyos64.bin\n"
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(PromotionBundleError, "unsafe asset name"):
                verify_bundle(bundle)


if __name__ == "__main__":
    unittest.main()
