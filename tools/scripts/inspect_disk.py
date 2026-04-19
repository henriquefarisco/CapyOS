#!/usr/bin/env python3
from __future__ import annotations

import argparse
import mmap
import struct
import sys
from pathlib import Path

from inspect_disk_boot import (
    find_gpt_partition,
    inspect_gpt,
    inspect_gpt_boot,
    inspect_mbr,
    inspect_stage2,
    write_stage2,
)
from inspect_disk_common import ESP_GUID, LOG_LINES, log, read_lba
from inspect_disk_fat32 import fat32_list_dir, fat32_read_file


def _cat_esp(img: mmap.mmap, path: str) -> int:
    esp = find_gpt_partition(img, ESP_GUID)
    if not esp:
        log("[erro] ESP nao encontrada no GPT.")
        return 1

    esp_lba, _ = esp
    try:
        data = fat32_read_file(img, esp_lba, path)
        sys.stdout.buffer.write(data)
        if data and not data.endswith(b"\n"):
            sys.stdout.buffer.write(b"\n")
    except Exception as exc:
        log(f"[erro] Falha ao ler ESP:{path}: {exc}")
        return 1
    return 0


def _ls_esp(img: mmap.mmap, path: str) -> int:
    esp = find_gpt_partition(img, ESP_GUID)
    if not esp:
        log("[erro] ESP nao encontrada no GPT.")
        return 1

    esp_lba, _ = esp
    try:
        entries = fat32_list_dir(img, esp_lba, path)
        log(f"[*] ESP ls: {path}")
        for entry in entries:
            name = entry["name"]
            ext = entry["ext"]
            full = f"{name}.{ext}" if ext else name
            is_dir = (entry["attr"] & 0x10) != 0
            tag = "DIR " if is_dir else "FILE"
            log(
                f"    {tag} {full:<12} "
                f"size={entry['size']} cl={entry['cluster']}"
            )
    except Exception as exc:
        log(f"[erro] Falha ao listar ESP:{path}: {exc}")
        return 1
    return 0


def _inspect_disk(img_path: Path, args) -> tuple[int, dict | None]:
    info = None
    with img_path.open("rb") as handle:
        mm = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        if args.cat_esp:
            return _cat_esp(mm, args.cat_esp), None
        if args.ls_esp is not None:
            return _ls_esp(mm, args.ls_esp), None

        inspect_mbr(mm)
        boot_part_lba = inspect_gpt(mm)
        if boot_part_lba:
            inspect_gpt_boot(handle, boot_part_lba)
            return 0, None

        boot_lba = struct.unpack("<I", read_lba(mm, 0)[454:458])[0]
        if boot_lba == 0:
            log("Boot partition starts at 0? Fishy.")
            return 0, None
        info = inspect_stage2(handle, boot_lba)
    return 0, info


def _write_log_file(path: str) -> None:
    try:
        Path(path).write_text("\n".join(LOG_LINES), encoding="utf-8")
        log(f"[info] Log salvo em {path}")
    except Exception as exc:
        log(f"[erro] Falha ao salvar log em {path}: {exc}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inspeciona MBR/GPT/stage2 de um disco CAPYOS."
    )
    parser.add_argument(
        "disk",
        nargs="?",
        default=(
            r"C:\ProgramData\Microsoft\Windows\Virtual Hard Disks\CapyOSGenII.vhd"
        ),
        help="Caminho para o disco (default: %(default)s)",
    )
    parser.add_argument(
        "--cat-esp",
        metavar="PATH",
        help="Mostra um arquivo dentro da ESP FAT32 (ex.: EFI/CAPYOS.LOG)",
    )
    parser.add_argument(
        "--ls-esp",
        nargs="?",
        const="/",
        metavar="DIR",
        help="Lista um diretorio dentro da ESP FAT32 (ex.: EFI, EFI/BOOT, ou /)",
    )
    parser.add_argument("--log-file", help="Opcional: grava a saida em um arquivo de log")
    parser.add_argument(
        "--write-stage2",
        action="store_true",
        help="Sobrescreve stage2 no disco usando o binario indicado (veja --stage2-bin)",
    )
    parser.add_argument(
        "--stage2-bin",
        default="build/boot/stage2.new.bin",
        help="Binario stage2 a gravar (default: %(default)s)",
    )
    args = parser.parse_args()

    img = Path(args.disk)
    if not img.exists():
        log(f"[erro] Arquivo de disco nao encontrado: {img}")
        return 1

    exit_code, info = _inspect_disk(img, args)
    if args.write_stage2:
        if info is None:
            log("[erro] Dados do stage2 nao foram coletados; abortando escrita.")
        else:
            write_stage2(img, Path(args.stage2_bin), info)

    if args.log_file:
        _write_log_file(args.log_file)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
