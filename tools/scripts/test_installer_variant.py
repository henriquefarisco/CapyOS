"""Regression gates for diagnostic ISO and canonical-pointer isolation."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import verify_installer_variant as policy
from smoke_x64_vmware import resolve_iso


REPO = Path(__file__).resolve().parents[2]


class InstallerVariantTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.kernel = self.root / "kernel.bin"
        self.variant = self.root / "variant.txt"
        self.iso = self.root / "installer.iso"
        self.pointer = self.root / "installer.last-built.txt"
        self.kernel.write_bytes(b"normal-kernel")
        self.variant.write_text("cflags=-O2 -DCAPYOS_PREEMPTIVE_SCHEDULER\n")
        self.iso.write_bytes(b"preserve-installer")
        self.pointer.write_bytes(b"preserve-pointer")
        for name, value in (("CANONICAL_ISO", self.iso),
                            ("CANONICAL_POINTER", self.pointer)):
            context = patch.object(policy, name, value)
            context.start()
            self.addCleanup(context.stop)

    def verify(self, *, iso=None, pointer=None, reuse=False):
        policy.verify_installer_variant(
            self.kernel, self.variant, iso or self.iso,
            pointer or self.pointer, reuse,
        )

    def test_normal_installer_accepted(self):
        self.verify()

    def test_reuse_cannot_write_either_canonical_output(self):
        for iso, pointer in (
            (self.iso, self.pointer),
            (self.root / "diagnostic.iso", self.pointer),
            (self.iso, self.root / "diagnostic.txt"),
            (self.pointer, self.iso),
        ):
            with self.subTest(iso=iso, pointer=pointer), self.assertRaises(ValueError):
                self.verify(iso=iso, pointer=pointer, reuse=True)
        self.assertEqual(self.iso.read_bytes(), b"preserve-installer")
        self.assertEqual(self.pointer.read_bytes(), b"preserve-pointer")

    def test_path_alias_cannot_bypass_policy(self):
        nested = self.root / "nested"
        nested.mkdir()
        with self.assertRaises(ValueError):
            self.verify(iso=nested / ".." / self.iso.name, reuse=True)

    def test_diagnostic_outputs_accepted(self):
        self.kernel.write_bytes(policy.BOOT_MARKERS[0])
        self.verify(iso=self.root / "diagnostic.iso",
                    pointer=self.root / "diagnostic.txt", reuse=True)

    def test_missing_provenance_fails_closed(self):
        self.variant.write_text("")
        with self.assertRaises(ValueError):
            self.verify()
        self.variant.unlink()
        with self.assertRaises(OSError):
            self.verify()

    def test_diagnostic_flags_rejected_without_reuse(self):
        for macro in (
            "CAPYOS_BOOT_RUN_HELLO", "CAPYOS_BOOT_RUN_TWO_BUSY",
            "CAPYOS_BOOT_RUN_FUTURE_TEST", "CAPYOS_HELLO_FP_STATE",
            "CAPYOS_AUDIO_PLAYBACK_SMOKE", "CAPYOS_MEDIA_PLAYER_SMOKE",
            "CAPYOS_SMOKE_CAPYAI", "CAPYOS_PREEMPTIVE_DEMO",
            "CAPYOS_UPDATE_LAB_TRUST_KEY_HEX",
        ):
            for prefix in ("cflags=-O2 ", "userland-extra="):
                self.variant.write_text(f"{prefix}-D{macro}=1\n")
                with self.subTest(macro=macro, prefix=prefix), self.assertRaises(ValueError):
                    self.verify()

    def test_kernel_hooks_rejected_even_with_normal_flag_record(self):
        for marker in policy.BOOT_MARKERS:
            self.kernel.write_bytes(b"prefix\0" + marker + b"\0suffix")
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                self.verify()

    @unittest.skipUnless(shutil.which("make"), "GNU make is required")
    def test_real_make_defaults_separate_reused_artifacts(self):
        recipe = (".PHONY: review-print-iso-paths\nreview-print-iso-paths:\n"
                  "\t@echo REVIEW_ISO=$(ISO_IMG_EFI)\n"
                  "\t@echo REVIEW_POINTER=$(ISO_LAST_BUILT_FILE)\n")
        for reuse, iso, pointer in (
            ("0", "build/CapyOS-Installer-UEFI.iso",
             "build/CapyOS-Installer-UEFI.last-built.txt"),
            ("1", "build/ci/CapyOS-Smoke-UEFI.iso",
             "build/ci/CapyOS-Smoke-UEFI.last-built.txt"),
        ):
            result = subprocess.run(
                ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-",
                 "review-print-iso-paths", f"ISO_REUSE_X64_VARIANT={reuse}"],
                input=recipe, cwd=REPO, capture_output=True, text=True, check=True,
            )
            self.assertIn(f"REVIEW_ISO={iso}\n", result.stdout)
            self.assertIn(f"REVIEW_POINTER={pointer}\n", result.stdout)

    def test_fp_target_does_not_clean_canonical_artifacts(self):
        makefile = (REPO / "Makefile").read_text(encoding="utf-8")
        recipe = makefile.split("\nsmoke-x64-fp-context:\n", 1)[1].split("\n\n", 1)[0]
        self.assertNotIn("$(MAKE) clean", recipe)
        self.assertIn("ISO_IMG_EFI=$(BUILD)/ci/CapyOS-Smoke-FP-UEFI.iso", recipe)
        self.assertIn("ISO_LAST_BUILT_FILE=$(BUILD)/ci/CapyOS-Smoke-FP-UEFI.last-built.txt", recipe)

    def test_vmware_missing_diagnostic_never_falls_back_to_installer(self):
        build = self.root / "build"
        build.mkdir()
        official = build / "CapyOS-Installer-UEFI.iso"
        official.write_bytes(b"older installer")
        (build / "CapyOS-Installer-UEFI.last-built.txt").write_text(str(official))
        diagnostic = build / "ci" / "missing-diagnostic.iso"
        self.assertEqual(resolve_iso(self.root, diagnostic), diagnostic)

    def test_diagnostic_consumers_select_their_iso(self):
        makefile = (REPO / "Makefile").read_text(encoding="utf-8")
        import re
        for match in re.finditer(r"^([a-zA-Z0-9][^\n]*:\n)((?:\t[^\n]*(?:\n|$))+)",
                                 makefile, re.MULTILINE):
            recipe = match.group(0)
            if "ISO_REUSE_X64_VARIANT=1" in recipe and "smoke_x64_vmware.py" in recipe:
                self.assertIn("--iso ", recipe, match.group(1))
        qemu_ab = makefile.split("\nsmoke-x64-qemu-update-ab:\n", 1)[1].split("\n\n", 1)[0]
        vmware_ab = makefile.split("\nsmoke-x64-vmware-update-ab:\n", 1)[1].split("\n\n", 1)[0]
        self.assertIn('--iso "$(SMOKE_ISO_IMG_EFI)"', qemu_ab)
        self.assertIn("cat $(SMOKE_ISO_LAST_BUILT_FILE)", vmware_ab)


if __name__ == "__main__":
    unittest.main()
