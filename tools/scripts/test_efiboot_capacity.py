#!/usr/bin/env python3
"""Exercise the real FAT16 writer, including independent chain/readback checks."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


class EfiCapacityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="capy-efi-test-")
        cls.root = Path(cls.tmp.name)
        cls.tool = cls.root / "mk_efiboot_img"
        source = Path(__file__).resolve().parents[1] / "host/src/mk_efiboot_img.c"
        repo = source.parents[3]
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra",
                        "-Werror", "-I" + str(repo / "include"),
                        "-I" + str(repo / "tools/host/include"),
                        "-I" + str(repo / "third_party/tinf"),
                        str(source), "-o", str(cls.tool)], check=True, timeout=60)
        cls.payload = bytes(range(256)) * (9 * 4096)
        (cls.root / "kernel").write_bytes(cls.payload)
        (cls.root / "loader").write_bytes(b"loader" * 1000)
        (cls.root / "manifest").write_bytes(b"manifest" * 200)
        (cls.root / "config").write_bytes(b"config" * 100)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_tool(self, size, *extra):
        return subprocess.run([str(self.tool), "--out", str(self.root / "image"),
                               "--bootx64", str(self.root / "loader"), "--kernel",
                               str(self.root / "kernel"), "--size", size, *extra],
                              capture_output=True, text=True, timeout=30)

    def test_oversized_preserves_existing_output(self):
        output = self.root / "image"
        output.write_bytes(b"keep-existing-image")
        result = self.run_tool("8M")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("capacity", result.stderr)
        self.assertEqual(output.read_bytes(), b"keep-existing-image")
        output.unlink()
        self.assertNotEqual(self.run_tool("8M").returncode, 0)
        self.assertFalse(output.exists())

    def test_bad_geometry(self):
        for size, extra in [("512", []), ("16M", ["--spc", "0"]),
                            ("16M", ["--spc", "3"]), ("16M", ["--spc", "256"]),
                            ("-16M", []), ("16Mbad", []),
                            ("18446744073709551616M", []), ("18446744073709551615G", [])]:
            with self.subTest(size=size, extra=extra):
                self.assertNotEqual(self.run_tool(size, *extra).returncode, 0)

    @unittest.skipUnless(Path("/dev/full").exists(), "requires Linux full device")
    def test_write_failure_is_not_success(self):
        result = self.run_tool("16M", "--out", "/dev/full")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot initialize", result.stderr)

    def test_complete_readback(self):
        for manifest in (False, True):
            with self.subTest(manifest=manifest):
                args = ["--bootcfg", str(self.root / "config")]
                if manifest:
                    args += ["--manifest", str(self.root / "manifest")]
                result = self.run_tool("16M", *args)
                self.assertEqual(result.returncode, 0, result.stderr)
                img = (self.root / "image").read_bytes()
                u16 = lambda offset: struct.unpack_from("<H", img, offset)[0]
                sectors = u16(19) or struct.unpack_from("<I", img, 32)[0]
                self.assertEqual(len(img), sectors * 512)
                self.assertEqual(len(img), 16 * 1024 * 1024)
                spc, fats = img[13], img[16]
                reserved, fat_sectors, entries = u16(14), u16(22), u16(17)
                root_offset = (reserved + fats * fat_sectors) * 512
                data_offset = root_offset + ((entries * 32 + 511) // 512) * 512
                count = (len(img) - data_offset) // (spc * 512)
                fat = img[reserved * 512:(reserved + fat_sectors) * 512]
                self.assertEqual(fat, img[(reserved + fat_sectors) * 512:root_offset])
                allocated = set()

                def chain(cluster):
                    result = bytearray()
                    while cluster < 0xFFF8:
                        self.assertGreaterEqual(cluster, 2)
                        self.assertLess(cluster, count + 2)
                        self.assertNotIn(cluster, allocated, "cycle or cross-linked FAT chain")
                        allocated.add(cluster)
                        offset = data_offset + (cluster - 2) * spc * 512
                        result += img[offset:offset + spc * 512]
                        cluster = struct.unpack_from("<H", fat, cluster * 2)[0]
                    return result

                found = {}

                def walk(directory, prefix=""):
                    for offset in range(0, len(directory), 32):
                        entry = directory[offset:offset + 32]
                        if entry[0] == 0:
                            break
                        if entry[0] == ord("."):
                            continue
                        name = entry[:8].decode().rstrip()
                        ext = entry[8:11].decode().rstrip()
                        path = prefix + name + ("." + ext if ext else "")
                        contents = chain(struct.unpack_from("<H", entry, 26)[0])
                        if entry[11] & 0x10:
                            walk(contents, path + "/")
                        else:
                            found[path] = contents[:struct.unpack_from("<I", entry, 28)[0]]

                walk(img[root_offset:data_offset])
                self.assertEqual(found["BOOT/CAPYOS64.BIN"], self.payload)
                self.assertEqual(found["EFI/BOOT/BOOTX64.EFI"], (self.root / "loader").read_bytes())
                self.assertEqual(found["BOOT/CAPYCFG.BIN"], (self.root / "config").read_bytes())
                self.assertEqual(found["CAPYOS.INI"], b"INSTALLER=1\n")
                if manifest:
                    self.assertEqual(found["BOOT/MANIFEST.BIN"], (self.root / "manifest").read_bytes())
                for cluster in range(2, count + 2):
                    if struct.unpack_from("<H", fat, cluster * 2)[0]:
                        self.assertIn(cluster, allocated, "unreachable allocated cluster")


if __name__ == "__main__":
    unittest.main()
