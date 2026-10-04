"""Alternate VM artifacts must be explicit and never replace the default bundle."""
from pathlib import Path
import tempfile
import unittest
from smoke_x64_common import validate_installed_disk_artifacts


class ArtifactBundleTests(unittest.TestCase):
    def test_default_and_explicit_bundles_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ("boot/uefi_loader.efi", "capyos64.bin", "manifest.bin")
            for prefix in ("build", "build/bridge"):
                for name in names:
                    path = root / prefix / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(prefix, encoding="ascii")
            default = validate_installed_disk_artifacts(root)
            bridge = validate_installed_disk_artifacts(root, artifact_dir=Path("build/bridge"))
            self.assertEqual(default, tuple((root / "build" / name).resolve() for name in names))
            self.assertEqual(bridge, tuple((root / "build/bridge" / name).resolve() for name in names))
            self.assertTrue(all(p.read_text() == "build" for p in default))
            (root / "build/bridge/manifest.bin").unlink()
            with self.assertRaises(FileNotFoundError):
                validate_installed_disk_artifacts(root, artifact_dir=Path("build/bridge"))


if __name__ == "__main__":
    unittest.main()
