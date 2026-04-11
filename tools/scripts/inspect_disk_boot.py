#!/usr/bin/env python3
from __future__ import annotations

import mmap
import struct
from pathlib import Path

from inspect_disk_common import (
    BOOT_GUID,
    PLACE_KL,
    PLACE_KS,
    PLACE_S2L,
    PLACE_S2S,
    count_pattern,
    log,
    read_lba,
    u32,
)


def inspect_mbr(img: mmap.mmap):
    mbr = read_lba(img, 0)
    log(f"[*] MBR Signature: {hex(mbr[510])} {hex(mbr[511])}")
    for index in range(4):
        off = 446 + index * 16
        entry = mbr[off:off + 16]
        status = entry[0]
        type_ = entry[4]
        lba = struct.unpack("<I", entry[8:12])[0]
        size = struct.unpack("<I", entry[12:16])[0]
        log(
            f"    Target P{index + 1}: Status={hex(status)} "
            f"Type={hex(type_)} LBA={lba} Size={size}"
        )
    return mbr


def inspect_gpt(img: mmap.mmap):
    hdr = read_lba(img, 1)
    sig = int.from_bytes(hdr[:8], "little")
    if sig != 0x5452415020494645:
        log("[!] GPT signature not found; skipping GPT decode.")
        return None

    part_entry_lba = int.from_bytes(hdr[72:80], "little")
    num_entries = int.from_bytes(hdr[80:84], "little")
    entry_size = int.from_bytes(hdr[84:88], "little")
    log(
        f"[*] GPT: entries @ LBA {part_entry_lba}, "
        f"count={num_entries}, size={entry_size}"
    )

    entries_per_sector = 512 // entry_size
    seen = 0
    boot_part_lba = None
    lba = part_entry_lba
    idx = 0
    while idx < num_entries:
        sector = read_lba(img, lba)
        for slot in range(entries_per_sector):
            if idx >= num_entries:
                break
            base = slot * entry_size
            ptype = sector[base:base + 16]
            first_lba = int.from_bytes(sector[base + 32:base + 40], "little")
            last_lba = int.from_bytes(sector[base + 40:base + 48], "little")
            if first_lba == 0 and last_lba == 0:
                idx += 1
                continue

            tag = "OTHER"
            if ptype == BOOT_GUID:
                tag = "BOOT"
                if boot_part_lba is None:
                    boot_part_lba = first_lba
            log(f"    GPT[{idx:02d}] {tag}: LBA={first_lba}..{last_lba}")
            idx += 1
            seen += 1
        lba += 1

    if seen == 0:
        log("[!] GPT parsed but no entries found.")
    return boot_part_lba


def find_gpt_partition(img: mmap.mmap, type_guid: bytes):
    hdr = read_lba(img, 1)
    sig = int.from_bytes(hdr[:8], "little")
    if sig != 0x5452415020494645:
        return None

    part_entry_lba = int.from_bytes(hdr[72:80], "little")
    num_entries = int.from_bytes(hdr[80:84], "little")
    entry_size = int.from_bytes(hdr[84:88], "little")
    if entry_size == 0:
        return None

    entries_per_sector = 512 // entry_size
    lba = part_entry_lba
    idx = 0
    while idx < num_entries:
        sector = read_lba(img, lba)
        for slot in range(entries_per_sector):
            if idx >= num_entries:
                break
            base = slot * entry_size
            ptype = sector[base:base + 16]
            first_lba = int.from_bytes(sector[base + 32:base + 40], "little")
            last_lba = int.from_bytes(sector[base + 40:base + 48], "little")
            idx += 1
            if first_lba == 0 and last_lba == 0:
                continue
            if ptype == type_guid:
                return first_lba, last_lba
        lba += 1
    return None


def inspect_manifest(handle, lba: int):
    handle.seek(lba * 512)
    data = handle.read(512)
    if len(data) < 24:
        log("    [!] Manifest read too small")
        return None

    magic = u32(data, 0)
    version = u32(data, 4)
    count = u32(data, 8)
    log(f"[*] Manifest @ LBA {lba}: magic=0x{magic:08X} ver={version} entries={count}")

    entries = []
    offs = 16
    for index in range(min(count, 4)):
        base = offs + index * 20
        etype = u32(data, base + 0)
        elba = u32(data, base + 4)
        esec = u32(data, base + 8)
        echk = u32(data, base + 12)
        entries.append((etype, elba, esec, echk))
        log(f"    entry{index}: type={etype} lba={elba} sec={esec} cksum=0x{echk:08X}")
    return entries


def pick_kernel(entries, header_lba: int, header_sec: int):
    if entries:
        for etype, elba, esec, _ in entries:
            if etype == 1 and elba and esec:
                return elba, esec, "manifest:normal"
        for etype, elba, esec, _ in entries:
            if etype == 2 and elba and esec:
                return elba, esec, "manifest:recovery"
    return header_lba, header_sec, "header/fallback"


def inspect_gpt_boot(handle, boot_part_lba: int):
    log(f"\n[*] Inspecting GPT BOOT partition @ LBA {boot_part_lba}...")
    entries = inspect_manifest(handle, boot_part_lba)
    if not entries:
        log("    [!] No manifest entries found at BOOT start")
        return

    chosen = None
    for etype, elba, esec, echk in entries:
        if etype == 1:
            chosen = (etype, elba, esec, echk)
            break
    if not chosen:
        chosen = entries[0]

    _, rel_lba, sec, _ = chosen
    abs_lba = boot_part_lba + rel_lba
    handle.seek(abs_lba * 512)
    khead = handle.read(16)
    if len(khead) < 4:
        log("    [!] Kernel read too small")
        return

    kmagic = struct.unpack_from("<I", khead, 0)[0]
    log(
        f"[*] Kernel (gpt:manifest) rel_lba={rel_lba} "
        f"abs_lba={abs_lba} sec={sec} magic=0x{kmagic:08X}"
    )
    if kmagic != 0x464C457F:
        log("    [!] Kernel magic incorreto (esperado 0x7F454C46)")
    else:
        log("    Kernel magic OK (ELF)")


def inspect_stage2(handle, stage2_lba: int):
    log(f"\n[*] Inspecting Stage 2 at LBA {stage2_lba}...")
    handle.seek(stage2_lba * 512)
    stage2 = handle.read(32768)
    if len(stage2) < 24:
        log("    [!] Stage2 read too small")
        return None

    kernel_sec = u32(stage2, 0x04)
    kernel_lba = u32(stage2, 0x08)
    stage2_lba_self = u32(stage2, 0x0C)
    stage2_sec = u32(stage2, 0x10)
    magic = u32(stage2, 0x14)
    log(
        "    header: "
        f"kernel_sectors={kernel_sec} kernel_lba={kernel_lba} "
        f"stage2_lba={stage2_lba_self} stage2_sec={stage2_sec} "
        f"magic=0x{magic:08X}"
    )

    log(
        "    placeholders: "
        f"KS={count_pattern(stage2, PLACE_KS)} "
        f"KL={count_pattern(stage2, PLACE_KL)} "
        f"S2L={count_pattern(stage2, PLACE_S2L)} "
        f"S2S={count_pattern(stage2, PLACE_S2S)}"
    )

    manifest_lba = stage2_lba + (stage2_sec if stage2_sec else 0)
    entries = inspect_manifest(handle, manifest_lba) if stage2_sec else None
    kernel_lba, kernel_sec, source = pick_kernel(entries, kernel_lba, kernel_sec)
    if kernel_lba is None or kernel_sec == 0:
        log("    [!] Kernel LBA/sectores indisponiveis")
        return None

    handle.seek(kernel_lba * 512)
    khead = handle.read(16)
    if len(khead) < 4:
        log("    [!] Kernel read too small")
        return None

    kmagic = struct.unpack_from("<I", khead, 0)[0]
    log(f"[*] Kernel ({source}) lba={kernel_lba} sec={kernel_sec} magic=0x{kmagic:08X}")
    if kmagic != 0x464C457F:
        log("    [!] Kernel magic incorreto (esperado 0x7F454C46)")
    else:
        log("    Kernel magic OK (ELF)")

    return {
        "stage2_lba": stage2_lba,
        "stage2_sec": stage2_sec,
        "kernel_lba": kernel_lba,
        "kernel_sec": kernel_sec,
    }


def write_stage2(disk: Path, stage2_bin: Path, info: dict):
    if not stage2_bin.exists():
        log(f"[erro] stage2 bin nao encontrado: {stage2_bin}")
        return False

    buf = bytearray(stage2_bin.read_bytes())
    struct.pack_into("<I", buf, 0x04, info["kernel_sec"])
    struct.pack_into("<I", buf, 0x08, info["kernel_lba"])
    struct.pack_into("<I", buf, 0x0C, info["stage2_lba"])
    struct.pack_into("<I", buf, 0x10, info["stage2_sec"])

    total_bytes = info["stage2_sec"] * 512
    padded = bytearray(total_bytes)
    padded[:len(buf)] = buf[:total_bytes]
    offset = info["stage2_lba"] * 512
    try:
        with disk.open("r+b") as handle:
            handle.seek(offset)
            handle.write(padded)
        log(
            f"[ok] stage2 escrito em {disk} @LBA "
            f"{info['stage2_lba']} ({info['stage2_sec']} sec)"
        )
        return True
    except Exception as exc:
        log(f"[erro] falha ao escrever stage2: {exc}")
        return False
