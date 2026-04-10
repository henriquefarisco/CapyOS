#!/usr/bin/env python3
"""
Gera manifest.bin para CAPYOS (boot_manifest).

Layout:
  entry 0: NORMAL kernel (obrigatÃ³rio)
  entry 1: RECOVERY kernel (opcional)

LBA/sectores sÃ£o relativos Ã  partiÃ§Ã£o BOOT (offset aplicado no loader).
"""
import argparse
import math
from pathlib import Path

BOOT_MANIFEST_MAGIC = 0x5442494E
BOOT_MANIFEST_VERSION = 1
BOOT_ENTRY_NORMAL = 1
BOOT_ENTRY_RECOVERY = 2


def checksum32(data: bytes) -> int:
    s = 0
    for b in data:
        s = (s + b) & 0xFFFFFFFF
    return s


def write_manifest(out: Path, kernel: Path, recovery: Path | None, kernel_lba: int, recovery_lba: int | None):
    kdata = kernel.read_bytes()
    ksecs = math.ceil(len(kdata) / 512)
    ksum = checksum32(kdata)

    entries = [
        (BOOT_ENTRY_NORMAL, kernel_lba, ksecs, ksum),
    ]

    if recovery and recovery.exists():
        rdata = recovery.read_bytes()
        rsecs = math.ceil(len(rdata) / 512)
        rsum = checksum32(rdata)
        entries.append((BOOT_ENTRY_RECOVERY, recovery_lba or (kernel_lba + ksecs), rsecs, rsum))

    buf = bytearray(512)
    buf[0:4] = BOOT_MANIFEST_MAGIC.to_bytes(4, "little")
    buf[4:8] = BOOT_MANIFEST_VERSION.to_bytes(4, "little")
    buf[8:12] = len(entries).to_bytes(4, "little")
    # reserved already zeroed
    off = 16
    for etype, lba, secs, csum in entries:
        buf[off:off+4] = etype.to_bytes(4, "little")
        buf[off+4:off+8] = lba.to_bytes(4, "little")
        buf[off+8:off+12] = secs.to_bytes(4, "little")
        buf[off+12:off+16] = csum.to_bytes(4, "little")
        off += 20

    out.write_bytes(buf)
    print(f"[ok] manifest gerado em {out} (entries={len(entries)})")
    print(f"     kernel lba={kernel_lba} sec={ksecs} checksum=0x{ksum:08X}")
    if len(entries) > 1:
        print(f"     recovery lba={entries[1][1]} sec={entries[1][2]} checksum=0x{entries[1][3]:08X}")


def main():
    ap = argparse.ArgumentParser(description="Gera manifest.bin (boot_manifest) para CAPYOS.")
    ap.add_argument("--out", default="build/manifest.bin", type=Path, help="Arquivo de saÃ­da manifest.bin")
    ap.add_argument("--kernel", required=True, type=Path, help="Caminho do kernel principal (ELF)")
    ap.add_argument("--recovery", type=Path, help="Kernel de recuperaÃ§Ã£o (opcional)")
    ap.add_argument("--kernel-lba", type=int, default=1, help="LBA (relativo Ã  partiÃ§Ã£o BOOT) do kernel principal (default: 1)")
    ap.add_argument("--recovery-lba", type=int, help="LBA (relativo Ã  BOOT) do kernel de recuperaÃ§Ã£o")
    args = ap.parse_args()

    write_manifest(args.out, args.kernel, args.recovery, args.kernel_lba, args.recovery_lba)


if __name__ == "__main__":
    main()
