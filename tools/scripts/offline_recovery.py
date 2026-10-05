#!/usr/bin/env python3
"""Recover a powered-off VMware flat image into a NEW signed-bridge disk copy.

Linux/WSL entry point. Never writes an original image, physical disk, DATA,
ESP, confirmed kernel, VMDK descriptor or VM configuration. The resulting raw
copy is NOT automatically attached or health-confirmed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

from update_manifest_common import ManifestError, parse_manifest
from verify_migration_bridge import stable_key, verify_bridge

CHUNK = 1024 * 1024
FILES = ("bridge.ini", "latest.ini", "capyos-bridge64.bin", "capyos64.bin")


def regular(path: Path) -> Path:
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ManifestError(f"not a regular, non-symlink file: {path}")
    if path.stat().st_nlink != 1:
        raise ManifestError(f"hard-linked input refused: {path}")
    return path.resolve(strict=True)


def digest(path: Path, start: int = 0, end: int | None = None) -> str:
    hash_value = hashlib.sha256()
    with path.open("rb") as stream:
        stream.seek(start)
        remaining = path.stat().st_size - start if end is None else end - start
        while remaining:
            block = stream.read(min(CHUNK, remaining))
            if not block:
                raise ManifestError("short image read")
            hash_value.update(block)
            remaining -= len(block)
    return hash_value.hexdigest()


def vm_is_off(vmx: Path, vmrun: str) -> None:
    result = subprocess.run([vmrun, "-T", "ws", "list"], check=True,
                            capture_output=True, text=True)
    lines = result.stdout.strip().splitlines()
    # Fail closed on an unknown/localized response, not an empty running list.
    if not lines or not re.fullmatch(r"Total running VMs: \d+", lines[0]):
        raise ManifestError("could not establish VMware power state")
    if int(lines[0].rsplit(" ", 1)[1]) != 0:
        raise ManifestError("stop all local VMware VMs before offline recovery")
    if list(vmx.parent.glob("*.lck")):
        raise ManifestError("VMware lock files exist; do not remove them to bypass this check")


def attached_flat_image(vmx: Path, source: Path) -> None:
    configuration = vmx.read_text(encoding="utf-8")
    descriptors = re.findall(r'^\s*(?:scsi|sata|ide|nvme)\d+:\d+\.fileName\s*=\s*"([^"\r\n]+\.vmdk)"',
                             configuration, re.MULTILINE | re.IGNORECASE)
    matches = 0
    for name in descriptors:
        descriptor = regular(vmx.parent / local_path(name))
        text = descriptor.read_text(encoding="utf-8")
        extents = re.findall(r'^RW (\d+) FLAT "([^"\r\n]+)" 0\s*$', text, re.MULTILINE)
        all_extents = re.findall(r'^(?:RW|RDONLY|NOACCESS) ', text, re.MULTILINE)
        if len(extents) != 1 or len(all_extents) != 1:
            raise ManifestError("only single-extent, unsnapshotted flat VMDK disks are supported")
        sectors, flat_name = extents[0]
        flat = regular(descriptor.parent / local_path(flat_name))
        if flat == source:
            if int(sectors) * 512 != source.stat().st_size:
                raise ManifestError("descriptor size mismatch")
            matches += 1
    if matches != 1:
        raise ManifestError("source must match exactly one attached flat VMDK extent")


def local_path(name: str) -> Path:
    if re.match(r"^[A-Za-z]:[\\/]", name):
        result = subprocess.run(["wslpath", "-u", name], check=True,
                                capture_output=True, text=True)
        return Path(result.stdout.strip())
    return Path(name.replace("\\", "/"))


def backend(helper: Path, mode: str, image: Path, current: str,
            payload: Path | None = None, bridge: str | None = None) -> dict:
    command = [str(helper), mode, str(image), current]
    if payload is not None:
        command += [str(payload), str(bridge)]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def unchanged_regions(source: Path, copy: Path, identity: dict) -> dict:
    size = source.stat().st_size
    allowed = sorted(((identity["inactive_start"] * 512, identity["inactive_end"] * 512),
                      (identity["control_start"] * 512,
                       (identity["boot_lba"] + identity["boot_sectors"]) * 512)))
    previous = 0
    for start, end in allowed:
        if not previous <= start < end <= size:
            raise ManifestError("invalid backend write boundary")
        if digest(source, previous, start) != digest(copy, previous, start):
            raise ManifestError("bytes outside inactive BOOT/control were modified")
        previous = end
    if digest(source, previous, size) != digest(copy, previous, size):
        raise ManifestError("trailing protected bytes were modified")
    data_start = identity["data_lba"] * 512
    data_end = data_start + identity["data_sectors"] * 512
    return {"data_sha256": digest(copy, data_start, data_end),
            "all_bytes_outside_inactive_boot_and_control_unchanged": True}


def recover(*, source: Path, output: Path, vmx: Path, vmrun: str,
            bundle: Path, helper: Path, current: str, openssl: str) -> dict:
    if os.name != "posix":
        raise ManifestError("run this entry point under Linux/WSL")
    import fcntl
    source, vmx, helper = regular(source), regular(vmx), regular(helper)
    stable_key(current)
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ManifestError("output must be a new file in an existing directory")
    output = output.parent.resolve(strict=True) / output.name
    vm_is_off(vmx, vmrun)
    attached_flat_image(vmx, source)
    if source.stat().st_size % 512:
        raise ManifestError("source image is not sector aligned")
    if shutil.disk_usage(output.parent).free < source.stat().st_size + 16 * CHUNK:
        raise ManifestError("insufficient free space for the recovery copy")
    with tempfile.TemporaryDirectory(prefix=".capyos-recovery-", dir=output.parent) as directory:
        scratch = Path(directory)
        for name in FILES:
            original = regular(bundle / name)
            if original.stat().st_size > (767 if name.endswith(".ini") else 8 * CHUNK):
                raise ManifestError("bundle file exceeds policy limit")
            shutil.copyfile(original, scratch / name)
        bridge, _ = parse_manifest((scratch / "bridge.ini").read_bytes())
        final, _ = parse_manifest((scratch / "latest.ini").read_bytes())
        verify_bridge(scratch, final_version=final["available_version"],
                      bridge_version=bridge["available_version"],
                      published_at=final["published_at"], openssl=openssl)
        if stable_key(current) >= stable_key(bridge["available_version"]):
            raise ManifestError("recovery bridge must be newer than the confirmed kernel")
        identity = backend(helper, "inspect", source, current)
        with source.open("rb") as original:
            fcntl.flock(original, fcntl.LOCK_EX | fcntl.LOCK_NB)
            original_hash = digest(source)
            copy = scratch / "recovered.img"
            with copy.open("xb") as destination:
                shutil.copyfileobj(original, destination)
                destination.flush()
                os.fsync(destination.fileno())
            if digest(copy) != original_hash:
                raise ManifestError("source changed while copying")
            if backend(helper, "inspect", copy, current) != identity:
                raise ManifestError("source boot state changed before cloning")
            staged = backend(helper, "stage", copy, current,
                             scratch / "capyos-bridge64.bin", bridge["available_version"])
            payload_start = (identity["inactive_start"] + 1) * 512
            signed_size = int(bridge["payload_size"])
            if (staged["candidate_size"] != signed_size or
                    staged["candidate_sha256"] != bridge["payload_sha256"] or
                    digest(copy, payload_start, payload_start + signed_size) != bridge["payload_sha256"]):
                raise ManifestError("staged candidate differs from the authenticated manifest")
            if (staged["disk_guid"] != identity["disk_guid"] or
                    staged["confirmed_slot"] != identity["confirmed_slot"] or
                    staged["pending_slot"] != (identity["confirmed_slot"] ^ 1) or
                    staged["tries_remaining"] != 1):
                raise ManifestError("unexpected staged boot state")
            evidence = unchanged_regions(source, copy, identity)
            vm_is_off(vmx, vmrun)
            if digest(source) != original_hash:
                raise ManifestError("original image changed during recovery")
            # link publishes exclusively, unlike replace/rename that may overwrite.
            os.link(copy, output)
            copy.unlink()
    return {"format": "capyos-signed-offline-recovery-v1", "output": str(output),
            "source_sha256": original_hash, "original_image_unchanged": True,
            "production_bundle_verified": True, "auto_confirmed": False,
            "bridge_version": bridge["available_version"], **identity, **evidence,
            "pending_slot": staged["pending_slot"],
            "tries_remaining": staged["tries_remaining"],
            "staged_generation": staged["generation"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output", "vmx", "bundle", "helper"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--current", required=True)
    parser.add_argument("--vmrun", required=True)
    parser.add_argument("--openssl", default="openssl")
    args = vars(parser.parse_args())
    try:
        print(json.dumps(recover(**args), sort_keys=True))
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"[err] offline recovery refused: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
