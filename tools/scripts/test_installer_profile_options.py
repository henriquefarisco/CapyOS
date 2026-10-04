"""Reject contradictory installer gates before allocating disks or starting VMs."""
import io
import unittest
from unittest.mock import patch

import smoke_x64_iso_install as qemu
import smoke_x64_vmware_installer as vmware
import smoke_x64_boot as boot


class InstallerProfileOptions(unittest.TestCase):
    def test_basic_cannot_require_desktop_or_remote_modules(self):
        for module in (qemu, vmware):
            for requirement in ("--require-desktop-after-login", "--require-module-install"):
                with self.subTest(provider=module.__name__, requirement=requirement):
                    with patch("sys.argv", ["smoke", "--iso", "test.iso", requirement]), \
                         patch("sys.stderr", new_callable=io.StringIO) as stderr:
                        with self.assertRaises(SystemExit) as raised:
                            module.parse_args()
                        self.assertEqual(raised.exception.code, 2)
                        self.assertIn("--module-profile full or custom", stderr.getvalue())

    def test_full_accepts_all_requirements(self):
        for module in (qemu, vmware):
            with patch("sys.argv", ["smoke", "--iso", "test.iso", "--module-profile", "full",
                                    "--require-desktop-after-login", "--require-module-install",
                                    "--require-builtin-music"]):
                args = module.parse_args()
                self.assertEqual(args.module_profile, "full")
                self.assertTrue(args.require_builtin_music)

    def test_music_gate_checks_every_preset(self):
        session = object()
        with patch.object(boot, "run_cmd") as command:
            boot.require_builtin_music(session, 60)
        self.assertEqual(command.call_count, 3)
        self.assertEqual([call.kwargs["expect"] for call in command.call_args_list],
                         [" - Capy Acoustic.ogg", " - Capy Opera.ogg", " - Capy Sound.ogg"])


if __name__ == "__main__":
    unittest.main()
