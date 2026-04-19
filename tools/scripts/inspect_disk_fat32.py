#!/usr/bin/env python3
from __future__ import annotations

import mmap

from inspect_disk_common import SECTOR_SIZE, read_lba


def _fat32_load(img: mmap.mmap, part_lba: int) -> dict[str, int | bytes]:
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
    fat = read_lba(img, fat_start_lba, fatsz32)
    return {
        "spc": spc,
        "root_cluster": root_cluster,
        "data_start_lba": data_start_lba,
        "fat": fat,
    }


def _fat32_next_cluster(fat: bytes, cluster: int) -> int:
    off = cluster * 4
    if off + 4 > len(fat):
        return 0
    return int.from_bytes(fat[off:off + 4], "little") & 0x0FFFFFFF


def _fat32_cluster_lba(data_start_lba: int, spc: int, cluster: int) -> int:
    return data_start_lba + (cluster - 2) * spc


def _fat32_read_chain(
    img: mmap.mmap,
    fat: bytes,
    data_start_lba: int,
    spc: int,
    start_cluster: int,
    max_clusters: int = 65536,
) -> bytes:
    if start_cluster < 2:
        return b""

    out = bytearray()
    cluster = start_cluster
    seen: set[int] = set()
    for _ in range(max_clusters):
        if cluster in seen:
            break
        seen.add(cluster)
        out += read_lba(img, _fat32_cluster_lba(data_start_lba, spc, cluster), spc)
        nxt = _fat32_next_cluster(fat, cluster)
        if nxt >= 0x0FFFFFF8 or nxt == 0:
            break
        cluster = nxt
    return bytes(out)


def _fat32_iter_dir(
    img: mmap.mmap,
    fat: bytes,
    data_start_lba: int,
    spc: int,
    start_cluster: int,
):
    raw = _fat32_read_chain(img, fat, data_start_lba, spc, start_cluster)
    for off in range(0, len(raw), 32):
        ent = raw[off:off + 32]
        if len(ent) < 32:
            break
        first_byte = ent[0]
        if first_byte == 0x00:
            break
        if first_byte == 0xE5:
            continue
        attr = ent[11]
        if attr == 0x0F:
            continue
        name = ent[0:8].decode("ascii", errors="ignore").rstrip(" ")
        ext = ent[8:11].decode("ascii", errors="ignore").rstrip(" ")
        hi = int.from_bytes(ent[20:22], "little")
        lo = int.from_bytes(ent[26:28], "little")
        cluster = (hi << 16) | lo
        size = int.from_bytes(ent[28:32], "little")
        yield {
            "name": name,
            "ext": ext,
            "attr": attr,
            "cluster": cluster,
            "size": size,
        }


def _want_83(component: str) -> tuple[str, str]:
    comp = component.strip().upper()
    if "." in comp:
        name, ext = comp.split(".", 1)
    else:
        name, ext = comp, ""
    return name[:8], ext[:3]


def _resolve_dir_cluster(
    img: mmap.mmap,
    part_lba: int,
    path: str,
) -> tuple[dict[str, int | bytes], int]:
    fs = _fat32_load(img, part_lba)
    fat = fs["fat"]
    data_start_lba = int(fs["data_start_lba"])
    spc = int(fs["spc"])
    cur = int(fs["root_cluster"])
    parts = [p for p in path.replace("\\", "/").lstrip("/").split("/") if p]

    for comp in parts:
        want_name, want_ext = _want_83(comp)
        found = None
        for entry in _fat32_iter_dir(img, fat, data_start_lba, spc, cur):
            if entry["name"].upper() == want_name and entry["ext"].upper() == want_ext:
                found = entry
                break
        if not found:
            raise FileNotFoundError(f"Diretorio/arquivo nao encontrado na ESP: {path}")
        if (found["attr"] & 0x10) == 0:
            raise FileNotFoundError(f"Caminho invalido (nao e diretorio): {comp}")
        cur = int(found["cluster"])

    return fs, cur


def fat32_read_file(img: mmap.mmap, part_lba: int, path: str) -> bytes:
    fs = _fat32_load(img, part_lba)
    fat = fs["fat"]
    data_start_lba = int(fs["data_start_lba"])
    spc = int(fs["spc"])
    cur = int(fs["root_cluster"])
    parts = [p for p in path.replace("\\", "/").lstrip("/").split("/") if p]

    for index, comp in enumerate(parts):
        want_name, want_ext = _want_83(comp)
        found = None
        for entry in _fat32_iter_dir(img, fat, data_start_lba, spc, cur):
            if entry["name"].upper() == want_name and entry["ext"].upper() == want_ext:
                found = entry
                break
        if not found:
            raise FileNotFoundError(f"Arquivo nao encontrado na ESP: {path}")

        is_last = index == len(parts) - 1
        if is_last:
            data = _fat32_read_chain(
                img, fat, data_start_lba, spc, int(found["cluster"])
            )
            return data[: int(found["size"])]
        if (found["attr"] & 0x10) == 0:
            raise FileNotFoundError(
                f"Caminho invalido (nao e diretorio): {comp}"
            )
        cur = int(found["cluster"])

    raise FileNotFoundError(f"Caminho vazio: {path!r}")


def fat32_list_dir(img: mmap.mmap, part_lba: int, path: str = "/"):
    fs, cluster = _resolve_dir_cluster(img, part_lba, path)
    return list(
        _fat32_iter_dir(
            img,
            fs["fat"],
            int(fs["data_start_lba"]),
            int(fs["spc"]),
            cluster,
        )
    )
