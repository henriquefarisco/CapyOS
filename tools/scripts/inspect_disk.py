#!/usr/bin/env python3
import sys
import struct
import argparse
from pathlib import Path
import mmap

LOG_LINES = []
SECTOR_SIZE = 512

def log(msg: str):
    print(msg)
    LOG_LINES.append(msg)

def read_u8(f): return struct.unpack("<B", f.read(1))[0]
def read_u16(f): return struct.unpack("<H", f.read(2))[0]
def read_u32(f): return struct.unpack("<I", f.read(4))[0]
def u32(data, off): return struct.unpack_from("<I", data, off)[0]

PLACE_KS = b"\xFE\x0F\xDC\xBA"  # 0xBADC0FFE
PLACE_KL = b"\xCE\xFA\xED\xFE"  # 0xFEEDFACE
PLACE_S2L = b"\xEF\xBE\xAD\xDE" # 0xDEADBEEF
PLACE_S2S = b"\xBE\xBA\xFE\xCA" # 0xCAFEBABE

def count_pattern(buf, pat):
    c = 0
    plen = len(pat)
    for i in range(0, len(buf) - plen + 1):
        if buf[i:i+plen] == pat:
            c += 1
    return c

def read_lba(img: mmap.mmap, lba: int, sectors: int = 1) -> bytes:
    start = lba * SECTOR_SIZE
    end = start + sectors * SECTOR_SIZE
    return img[start:end]

def inspect_mbr(img: mmap.mmap):
    mbr = read_lba(img, 0)
    log(f"[*] MBR Signature: {hex(mbr[510])} {hex(mbr[511])}")
    # Check partitions
    for i in range(4):
        off = 446 + i*16
        entry = mbr[off:off+16]
        status = entry[0]
        type_ = entry[4]
        lba = struct.unpack("<I", entry[8:12])[0]
        size = struct.unpack("<I", entry[12:16])[0]
        log(f"    Target P{i+1}: Status={hex(status)} Type={hex(type_)} LBA={lba} Size={size}")
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
    log(f"[*] GPT: entries @ LBA {part_entry_lba}, count={num_entries}, size={entry_size}")
    entries_per_sector = SECTOR_SIZE // entry_size
    esp_guid = bytes([0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
    boot_guid = bytes([0x76,0x0b,0x98,0x04,0x42,0x10,0x4c,0x9b,0x86,0x1f,0x11,0xe0,0x29,0xea,0xc1,0x01])
    seen = 0
    boot_part_lba = None
    lba = part_entry_lba
    idx = 0
    while idx < num_entries:
        sector = read_lba(img, lba)
        for i in range(entries_per_sector):
            if idx >= num_entries:
                break
            base = i * entry_size
            ptype = sector[base:base+16]
            first_lba = int.from_bytes(sector[base+32:base+40], "little")
            last_lba  = int.from_bytes(sector[base+40:base+48], "little")
            if first_lba == 0 and last_lba == 0:
                idx += 1
                continue
            tag = "OTHER"
            if ptype == esp_guid:
                tag = "ESP"
            elif ptype == boot_guid:
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
    entries_per_sector = SECTOR_SIZE // entry_size
    lba = part_entry_lba
    idx = 0
    while idx < num_entries:
        sector = read_lba(img, lba)
        for i in range(entries_per_sector):
            if idx >= num_entries:
                break
            base = i * entry_size
            ptype = sector[base:base+16]
            first_lba = int.from_bytes(sector[base+32:base+40], "little")
            last_lba  = int.from_bytes(sector[base+40:base+48], "little")
            idx += 1
            if first_lba == 0 and last_lba == 0:
                continue
            if ptype == type_guid:
                return first_lba, last_lba
        lba += 1
    return None

def fat32_read_file(img: mmap.mmap, part_lba: int, path: str) -> bytes:
    bs = read_lba(img, part_lba)
    bps = int.from_bytes(bs[11:13], "little")
    spc = bs[13]
    reserved = int.from_bytes(bs[14:16], "little")
    num_fats = bs[16]
    fatsz32 = int.from_bytes(bs[36:40], "little")
    root_cluster = int.from_bytes(bs[44:48], "little")

    if bps != 512 or spc == 0 or fatsz32 == 0 or num_fats == 0:
        raise ValueError("FAT32 invÃƒÂ¡lido/inesperado na ESP.")

    fat_start_lba = part_lba + reserved
    data_start_lba = fat_start_lba + num_fats * fatsz32
    cluster_bytes = spc * SECTOR_SIZE

    fat = read_lba(img, fat_start_lba, fatsz32)

    def fat_next(cl: int) -> int:
        off = cl * 4
        if off + 4 > len(fat):
            return 0
        v = int.from_bytes(fat[off:off+4], "little") & 0x0FFFFFFF
        return v

    def cluster_lba(cl: int) -> int:
        return data_start_lba + (cl - 2) * spc

    def read_chain(start: int, max_clusters: int = 65536) -> bytes:
        if start < 2:
            return b""
        out = bytearray()
        cl = start
        seen = set()
        for _ in range(max_clusters):
            if cl in seen:
                break
            seen.add(cl)
            out += read_lba(img, cluster_lba(cl), spc)
            nxt = fat_next(cl)
            if nxt >= 0x0FFFFFF8 or nxt == 0:
                break
            cl = nxt
        return bytes(out)

    def iter_dir(start_cluster: int):
        raw = read_chain(start_cluster)
        for off in range(0, len(raw), 32):
            ent = raw[off:off+32]
            if len(ent) < 32:
                break
            fb = ent[0]
            if fb == 0x00:
                break
            if fb == 0xE5:
                continue
            attr = ent[11]
            if attr == 0x0F:
                continue  # LFN
            name = ent[0:8].decode("ascii", errors="ignore").rstrip(" ")
            ext = ent[8:11].decode("ascii", errors="ignore").rstrip(" ")
            hi = int.from_bytes(ent[20:22], "little")
            lo = int.from_bytes(ent[26:28], "little")
            cl = (hi << 16) | lo
            size = int.from_bytes(ent[28:32], "little")
            yield {
                "name": name,
                "ext": ext,
                "attr": attr,
                "cluster": cl,
                "size": size,
            }

    def want_83(comp: str):
        comp = comp.strip().upper()
        if "." in comp:
            n, e = comp.split(".", 1)
        else:
            n, e = comp, ""
        return n[:8], e[:3]

    parts = path.replace("\\", "/").lstrip("/").split("/")
    parts = [p for p in parts if p]
    cur = root_cluster
    for i, comp in enumerate(parts):
        wn, we = want_83(comp)
        found = None
        for e in iter_dir(cur):
            if e["name"].upper() == wn and e["ext"].upper() == we:
                found = e
                break
        if not found:
            raise FileNotFoundError(f"Arquivo nÃƒÂ£o encontrado na ESP: {path}")
        is_last = (i == len(parts) - 1)
        if is_last:
            data = read_chain(found["cluster"])
            return data[: found["size"]]
        if (found["attr"] & 0x10) == 0:
            raise FileNotFoundError(f"Caminho invÃƒÂ¡lido (nÃƒÂ£o ÃƒÂ© diretÃƒÂ³rio): {comp}")
        cur = found["cluster"]

    raise FileNotFoundError(f"Caminho vazio: {path!r}")

def fat32_list_dir(img: mmap.mmap, part_lba: int, path: str = "/"):
    bs = read_lba(img, part_lba)
    bps = int.from_bytes(bs[11:13], "little")
    spc = bs[13]
    reserved = int.from_bytes(bs[14:16], "little")
    num_fats = bs[16]
    fatsz32 = int.from_bytes(bs[36:40], "little")
    root_cluster = int.from_bytes(bs[44:48], "little")

    if bps != 512 or spc == 0 or fatsz32 == 0 or num_fats == 0:
        raise ValueError("FAT32 invalido/inesperado na ESP.")

    fat_start_lba = part_lba + reserved
    data_start_lba = fat_start_lba + num_fats * fatsz32
    cluster_bytes = spc * SECTOR_SIZE

    fat = read_lba(img, fat_start_lba, fatsz32)

    def fat_next(cl: int) -> int:
        off = cl * 4
        if off + 4 > len(fat):
            return 0
        v = int.from_bytes(fat[off:off+4], "little") & 0x0FFFFFFF
        return v

    def cluster_lba(cl: int) -> int:
        return data_start_lba + (cl - 2) * spc

    def read_chain(start: int, max_clusters: int = 65536) -> bytes:
        if start < 2:
            return b""
        out = bytearray()
        cl = start
        seen = set()
        for _ in range(max_clusters):
            if cl in seen:
                break
            seen.add(cl)
            out += read_lba(img, cluster_lba(cl), spc)
            nxt = fat_next(cl)
            if nxt >= 0x0FFFFFF8 or nxt == 0:
                break
            cl = nxt
        return bytes(out)

    def iter_dir(start_cluster: int):
        raw = read_chain(start_cluster)
        for off in range(0, len(raw), 32):
            ent = raw[off:off+32]
            if len(ent) < 32:
                break
            fb = ent[0]
            if fb == 0x00:
                break
            if fb == 0xE5:
                continue
            attr = ent[11]
            if attr == 0x0F:
                continue  # LFN
            name = ent[0:8].decode("ascii", errors="ignore").rstrip(" ")
            ext = ent[8:11].decode("ascii", errors="ignore").rstrip(" ")
            hi = int.from_bytes(ent[20:22], "little")
            lo = int.from_bytes(ent[26:28], "little")
            cl = (hi << 16) | lo
            size = int.from_bytes(ent[28:32], "little")
            yield {
                "name": name,
                "ext": ext,
                "attr": attr,
                "cluster": cl,
                "size": size,
            }

    def want_83(comp: str):
        comp = comp.strip().upper()
        if "." in comp:
            n, e = comp.split(".", 1)
        else:
            n, e = comp, ""
        return n[:8], e[:3]

    parts = path.replace("\\", "/").lstrip("/").split("/")
    parts = [p for p in parts if p]
    cur = root_cluster
    for comp in parts:
        wn, we = want_83(comp)
        found = None
        for e in iter_dir(cur):
            if e["name"].upper() == wn and e["ext"].upper() == we:
                found = e
                break
        if not found:
            raise FileNotFoundError(f"Diretorio/arquivo nao encontrado na ESP: {path}")
        if (found["attr"] & 0x10) == 0:
            raise FileNotFoundError(f"Caminho invalido (nao e diretorio): {comp}")
        cur = found["cluster"]

    return list(iter_dir(cur))

def inspect_gpt_boot(f, boot_part_lba: int):
    log(f"\n[*] Inspecting GPT BOOT partition @ LBA {boot_part_lba}...")
    entries = inspect_manifest(f, boot_part_lba)
    if not entries:
        log("    [!] No manifest entries found at BOOT start")
        return
    # Prefer NORMAL
    chosen = None
    for etype, elba, esec, echk in entries:
        if etype == 1:
            chosen = (etype, elba, esec, echk)
            break
    if not chosen:
        chosen = entries[0]
    _, rel_lba, sec, _ = chosen
    abs_lba = boot_part_lba + rel_lba
    f.seek(abs_lba * 512)
    khead = f.read(16)
    if len(khead) < 4:
        log("    [!] Kernel read too small")
        return
    kmagic = struct.unpack_from("<I", khead, 0)[0]
    log(f"[*] Kernel (gpt:manifest) rel_lba={rel_lba} abs_lba={abs_lba} sec={sec} magic=0x{kmagic:08X}")
    if kmagic != 0x464C457F:
        log("    [!] Kernel magic incorreto (esperado 0x7F454C46)")
    else:
        log("    Kernel magic OK (ELF)")

def inspect_manifest(f, lba):
    f.seek(lba * 512)
    data = f.read(512)
    if len(data) < 24:
        log("    [!] Manifest read too small")
        return None
    magic = u32(data, 0)
    ver = u32(data, 4)
    cnt = u32(data, 8)
    log(f"[*] Manifest @ LBA {lba}: magic=0x{magic:08X} ver={ver} entries={cnt}")
    entries = []
    offs = 16
    for i in range(min(cnt, 4)):
        base = offs + i * 20
        etype = u32(data, base + 0)
        elba = u32(data, base + 4)
        esec = u32(data, base + 8)
        echk = u32(data, base + 12)
        entries.append((etype, elba, esec, echk))
        log(f"    entry{i}: type={etype} lba={elba} sec={esec} cksum=0x{echk:08X}")
    return entries

def pick_kernel(entries, header_lba, header_sec):
    # Prefer NORMAL, then RECOVERY, else header values.
    if entries:
        for etype, elba, esec, _ in entries:
            if etype == 1 and elba and esec:
                return elba, esec, "manifest:normal"
        for etype, elba, esec, _ in entries:
            if etype == 2 and elba and esec:
                return elba, esec, "manifest:recovery"
    return header_lba, header_sec, "header/fallback"

def inspect_stage2(f, stage2_lba):
    log(f"\n[*] Inspecting Stage 2 at LBA {stage2_lba}...")
    f.seek(stage2_lba * 512)
    stage2 = f.read(32768) # Read 32KB max
    if len(stage2) < 24:
        log("    [!] Stage2 read too small")
        return None

    ks = u32(stage2, 0x04)
    kl = u32(stage2, 0x08)
    s2l = u32(stage2, 0x0C)
    s2s = u32(stage2, 0x10)
    magic = u32(stage2, 0x14)
    log(f"    header: kernel_sectors={ks} kernel_lba={kl} stage2_lba={s2l} stage2_sec={s2s} magic=0x{magic:08X}")

    log(f"    placeholders: KS={count_pattern(stage2, PLACE_KS)} KL={count_pattern(stage2, PLACE_KL)} S2L={count_pattern(stage2, PLACE_S2L)} S2S={count_pattern(stage2, PLACE_S2S)}")
    
    # Patched values in stage2.asm (Data section at end)
    # kernel_sectors
    # kernel_lba_low
    # stage2_lba_low (patched self-reference)
    
    # We can search for known patterns or just the sequence 
    # 0xBADC0FFE (placeholder) to see if it missed patching.
    
    manifest_lba = stage2_lba + (s2s if s2s else 0)
    entries = inspect_manifest(f, manifest_lba) if s2s else None
    kl_pick, ks_pick, src = pick_kernel(entries, kl, ks)
    if kl_pick is None or ks_pick == 0:
        log("    [!] Kernel LBA/sectores indisponiveis")
        return None
    f.seek(kl_pick * 512)
    khead = f.read(16)
    if len(khead) < 4:
        log("    [!] Kernel read too small")
        return None
    kmagic = struct.unpack_from("<I", khead, 0)[0]
    log(f"[*] Kernel ({src}) lba={kl_pick} sec={ks_pick} magic=0x{kmagic:08X}")
    if kmagic != 0x464C457F:
        log("    [!] Kernel magic incorreto (esperado 0x7F454C46)")
    else:
        log("    Kernel magic OK (ELF)")
    return {
        "stage2_lba": stage2_lba,
        "stage2_sec": s2s,
        "kernel_lba": kl_pick,
        "kernel_sec": ks_pick,
    }

def write_stage2(disk: Path, stage2_bin: Path, info: dict):
    if not stage2_bin.exists():
        log(f"[erro] stage2 bin nao encontrado: {stage2_bin}")
        return False
    buf = bytearray(stage2_bin.read_bytes())
    # Patch header with current layout
    struct.pack_into("<I", buf, 0x04, info["kernel_sec"])
    struct.pack_into("<I", buf, 0x08, info["kernel_lba"])
    struct.pack_into("<I", buf, 0x0C, info["stage2_lba"])
    struct.pack_into("<I", buf, 0x10, info["stage2_sec"])
    total_bytes = info["stage2_sec"] * 512
    padded = bytearray(total_bytes)
    padded[:len(buf)] = buf[:total_bytes]
    offset = info["stage2_lba"] * 512
    try:
        with disk.open("r+b") as f:
            f.seek(offset)
            f.write(padded)
        log(f"[ok] stage2 escrito em {disk} @LBA {info['stage2_lba']} ({info['stage2_sec']} sec)")
        return True
    except Exception as e:
        log(f"[erro] falha ao escrever stage2: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Inspeciona MBR/GPT/stage2 de um disco CAPYOS.")
    parser.add_argument("disk", nargs="?", default=r"C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\CapyOSGenII.vhd",
                        help="Caminho para o disco (default: %(default)s)")
    parser.add_argument("--cat-esp", metavar="PATH",
                        help="Mostra um arquivo dentro da ESP FAT32 (ex.: EFI/CAPYOS.LOG)")
    parser.add_argument("--ls-esp", nargs="?", const="/", metavar="DIR",
                        help="Lista um diretÃƒÂ³rio dentro da ESP FAT32 (ex.: EFI, EFI/BOOT, ou /)")
    parser.add_argument("--log-file", help="Opcional: grava a saÃƒÂ­da em um arquivo de log")
    parser.add_argument("--write-stage2", action="store_true",
                        help="Sobrescreve stage2 no disco usando o binario indicado (veja --stage2-bin)")
    parser.add_argument("--stage2-bin", default="build/boot/stage2.new.bin",
                        help="Binario stage2 a gravar (default: %(default)s)")
    args = parser.parse_args()

    img = Path(args.disk)
    if not img.exists():
        log(f"[erro] Arquivo de disco nao encontrado: {img}")
        sys.exit(1)

    info = None
    with img.open("rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        if args.cat_esp:
            esp_guid = bytes([0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
            esp = find_gpt_partition(mm, esp_guid)
            if not esp:
                log("[erro] ESP nÃƒÂ£o encontrada no GPT.")
                return
            esp_lba, _ = esp
            try:
                data = fat32_read_file(mm, esp_lba, args.cat_esp)
                sys.stdout.buffer.write(data)
                if data and not data.endswith(b"\n"):
                    sys.stdout.buffer.write(b"\n")
            except Exception as e:
                log(f"[erro] Falha ao ler ESP:{args.cat_esp}: {e}")
            return
        if args.ls_esp is not None:
            esp_guid = bytes([0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B])
            esp = find_gpt_partition(mm, esp_guid)
            if not esp:
                log("[erro] ESP nÃƒÂ£o encontrada no GPT.")
                return
            esp_lba, _ = esp
            try:
                entries = fat32_list_dir(mm, esp_lba, args.ls_esp)
                log(f"[*] ESP ls: {args.ls_esp}")
                for e in entries:
                    name = e["name"]
                    ext = e["ext"]
                    full = f"{name}.{ext}" if ext else name
                    is_dir = (e["attr"] & 0x10) != 0
                    tag = "DIR " if is_dir else "FILE"
                    log(f"    {tag} {full:<12} size={e['size']} cl={e['cluster']}")
            except Exception as e:
                log(f"[erro] Falha ao listar ESP:{args.ls_esp}: {e}")
            return
        inspect_mbr(mm)
        boot_part_lba = inspect_gpt(mm)
        if boot_part_lba:
            inspect_gpt_boot(f, boot_part_lba)
        else:
            # Legacy MBR path: Assume P1 ÃƒÂ© BOOT.
            boot_lba = struct.unpack("<I", read_lba(mm, 0)[454:458])[0]
            if boot_lba == 0:
                log("Boot partition starts at 0? Fishy.")
            else:
                info = inspect_stage2(f, boot_lba)

    if args.write_stage2:
        if info is None:
            log("[erro] Dados do stage2 nao foram coletados; abortando escrita.")
        else:
            write_stage2(img, Path(args.stage2_bin), info)

    if args.log_file:
        try:
            Path(args.log_file).write_text("\n".join(LOG_LINES), encoding="utf-8")
            log(f"[info] Log salvo em {args.log_file}")
        except Exception as e:
            log(f"[erro] Falha ao salvar log em {args.log_file}: {e}")

if __name__ == "__main__":
    main()
