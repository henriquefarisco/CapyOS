"""Host image safety tests. Synthetic kernels/signature mocks are NOT VM evidence."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from offline_recovery import (
    ManifestError, attached_flat_image, backend, digest, recover, regular,
    unchanged_regions, vm_is_off,
)
from provision_gpt_layout import partition_gpt
import test_migration_bridge_contract as bridge_tests

ROOT = Path(__file__).resolve().parents[2]
CURRENT = "0.10.0+20260924"
BRIDGE = "0.11.2+20261004"


@unittest.skipUnless(os.name == "posix", "host backend requires Linux/WSL")
class OfflineRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.image = self.root / "target-flat.vmdk"
        with self.image.open("wb") as stream:
            stream.truncate(16 * 1024 * 1024)
        partition_gpt(self.image, "1M", "8M")
        self.helper = ROOT / "build/offline-recovery-slot"
        subprocess.run([str(ROOT / "build/offline-recovery-fixture"), str(self.image)], check=True)
        self.before = digest(self.image)
        self.identity = backend(self.helper, "inspect", self.image, CURRENT)
        self.vmx = self.root / "test.vmx"
        self.vmx.write_text('scsi0:0.fileName = "target.vmdk"\n')
        (self.root / "target.vmdk").write_text('RW 32768 FLAT "target-flat.vmdk" 0\n')
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        bridge_tests.MigrationBridgeContractTests().fixture(self.bundle)
        self.output = self.root / "recovered.img"

    def recovery(self):
        return recover(source=self.image, output=self.output, vmx=self.vmx,
                       vmrun="vmrun", bundle=self.bundle, helper=self.helper,
                       current=CURRENT, openssl="openssl")

    @patch("offline_recovery.vm_is_off")
    @patch("verify_migration_bridge.verify_signature")
    def test_new_copy_only_one_trial_original_and_data_preserved(self, signature, power):
        result = self.recovery()
        self.assertEqual(signature.call_count, 2)
        self.assertEqual(digest(self.image), self.before)
        self.assertTrue(result["all_bytes_outside_inactive_boot_and_control_unchanged"])
        self.assertFalse(result["auto_confirmed"])
        # Pending trial must reject recovery a second time; no silent confirm.
        with self.assertRaises(subprocess.CalledProcessError):
            backend(self.helper, "inspect", self.output, CURRENT)

    @patch("offline_recovery.vm_is_off")
    def test_real_signature_verifier_rejects_unsigned_fixture(self, power):
        with self.assertRaises(ManifestError):
            self.recovery()
        self.assertFalse(self.output.exists())
        self.assertEqual(digest(self.image), self.before)

    @patch("offline_recovery.vm_is_off")
    @patch("verify_migration_bridge.verify_signature")
    def test_corrupt_payload_aborts_before_output(self, signature, power):
        (self.bundle / "capyos-bridge64.bin").write_bytes(b"\177ELFcorrupt")
        with self.assertRaisesRegex(ManifestError, "size/hash mismatch"):
            self.recovery()
        self.assertFalse(self.output.exists())
        self.assertEqual(digest(self.image), self.before)

    def test_never_overwrites_existing_output(self):
        self.output.write_bytes(b"keep me")
        with self.assertRaises(ManifestError):
            self.recovery()
        self.assertEqual(self.output.read_bytes(), b"keep me")

    @patch("offline_recovery.vm_is_off")
    @patch("verify_migration_bridge.verify_signature")
    @patch("offline_recovery.os.link", side_effect=OSError("publish failed"))
    def test_failed_publish_cleans_scratch_and_preserves_original(self, link, signature, power):
        with self.assertRaises(OSError):
            self.recovery()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".capyos-recovery-*")))
        self.assertEqual(digest(self.image), self.before)

    @patch("offline_recovery.vm_is_off")
    @patch("verify_migration_bridge.verify_signature")
    def test_backend_failure_never_publishes_partial_image(self, signature, power):
        real_backend = backend
        def fail_stage(helper, mode, *args):
            if mode == "stage":
                raise subprocess.CalledProcessError(1, [str(helper), mode])
            return real_backend(helper, mode, *args)
        with patch("offline_recovery.backend", side_effect=fail_stage):
            with self.assertRaises(subprocess.CalledProcessError):
                self.recovery()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".capyos-recovery-*")))
        self.assertEqual(digest(self.image), self.before)

    @patch("offline_recovery.vm_is_off")
    @patch("verify_migration_bridge.verify_signature")
    def test_payload_change_after_signature_verification_is_refused(self, signature, power):
        real_backend = backend
        def change_payload(helper, mode, image, current, payload=None, bridge=None):
            if mode == "stage":
                with payload.open("ab") as stream:
                    stream.write(b"post-verification-change")
            return real_backend(helper, mode, image, current, payload, bridge)
        with patch("offline_recovery.backend", side_effect=change_payload):
            with self.assertRaisesRegex(ManifestError, "authenticated manifest"):
                self.recovery()
        self.assertFalse(self.output.exists())
        self.assertEqual(digest(self.image), self.before)

    def test_wrong_confirmed_version_and_gpt_crc_refused(self):
        with self.assertRaises(subprocess.CalledProcessError):
            backend(self.helper, "inspect", self.image, BRIDGE)
        with self.image.open("r+b") as stream:
            stream.seek(512 + 16)
            stream.write(b"\0\0\0\0")
        with self.assertRaises(subprocess.CalledProcessError):
            backend(self.helper, "inspect", self.image, CURRENT)

    def test_corrupt_confirmed_payload_refused(self):
        with self.image.open("r+b") as stream:
            stream.seek((self.identity["boot_lba"] + 1) * 512)
            stream.write(b"BAD!")
        with self.assertRaises(subprocess.CalledProcessError):
            backend(self.helper, "inspect", self.image, CURRENT)

    def test_protected_region_change_detected(self):
        shutil.copyfile(self.image, self.output)
        with self.output.open("r+b") as stream:
            stream.seek(self.identity["data_lba"] * 512)
            stream.write(b"BAD!")
        with self.assertRaises(ManifestError):
            unchanged_regions(self.image, self.output, self.identity)

    def test_symlink_hardlink_and_descriptor_mismatch_refused(self):
        link = self.root / "link"
        link.symlink_to(self.image)
        with self.assertRaises(ManifestError):
            regular(link)
        link.unlink()
        os.link(self.image, link)
        with self.assertRaises(ManifestError):
            regular(self.image)
        link.unlink()
        (self.root / "target.vmdk").write_text('RW 32767 FLAT "target-flat.vmdk" 0\n')
        with self.assertRaises(ManifestError):
            attached_flat_image(self.vmx, self.image)

    @patch("offline_recovery.subprocess.run")
    def test_running_vm_unknown_power_and_lock_refused(self, run):
        for response in ("Total running VMs: 1\nx.vmx\n", "unexpected response"):
            run.return_value.stdout = response
            with self.assertRaises(ManifestError):
                vm_is_off(self.vmx, "vmrun")
        run.return_value.stdout = "Total running VMs: 0\n"
        (self.root / "test.vmx.lck").mkdir()
        with self.assertRaises(ManifestError):
            vm_is_off(self.vmx, "vmrun")


if __name__ == "__main__":
    unittest.main()
