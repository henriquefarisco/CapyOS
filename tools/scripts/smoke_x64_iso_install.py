#!/usr/bin/env python3
"""
CAPYOS x64 smoke test for the official ISO install flow:

- boots the release ISO in UEFI mode
- completes the installer wizard on a blank disk
- reboots from the installed disk
- runs first-boot setup when required
- validates login + core CLI
- writes a marker file, reboots, validates persistence
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

from smoke_x64_common import (
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    parse_size,
    print_log_tail,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    run_build_if_requested,
    validate_iso_artifact,
)
from smoke_x64_auth import module_install_completed
from smoke_x64_flow import (
    complete_iso_install,
    ensure_shell_after_login,
    login,
    maybe_run_first_boot_setup,
    smoke_first_boot,
    smoke_second_boot,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="CAPYOS x64 ISO install smoke test (QEMU/UEFI)"
    )
    parser.add_argument("--iso", default="build/CapyOS-Installer-UEFI.iso", help="Official installer ISO path")
    parser.add_argument("--qemu", default="qemu-system-x86_64", help="QEMU binary")
    parser.add_argument("--ovmf", default=None, help="Path to OVMF_CODE.fd")
    parser.add_argument("--memory", type=int, default=1024, help="Guest memory in MB")
    parser.add_argument(
        "--storage-bus",
        choices=("sata", "nvme"),
        default="sata",
        help="Storage bus used by the install target disk",
    )
    parser.add_argument(
        "--step-timeout",
        type=float,
        default=60.0,
        help="Timeout per interaction step in seconds",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help="Run make all64 && make iso-uefi && make manifest64 first",
    )
    parser.add_argument("--log", default="build/ci/smoke_x64_iso_install.log", help="Base log file path")
    parser.add_argument(
        "--disk",
        default="build/ci/smoke_x64_iso_install.img",
        help="Blank install target disk path",
    )
    parser.add_argument("--disk-size", default="2G", help="Install target disk size")
    parser.add_argument("--keep-disk", action="store_true", help="Do not delete the target disk image")
    parser.add_argument(
        "--guard-disk",
        default="",
        help="Optional larger disk that must remain byte-identical across install",
    )
    parser.add_argument("--guard-disk-size", default="3G", help="Guard disk size")
    parser.add_argument(
        "--allow-external-disks",
        action="store_true",
        help="Allow destructive smoke disk paths outside build/ci",
    )
    parser.add_argument(
        "--target-selection",
        type=int,
        default=1,
        help="Eligible installer target number to select",
    )
    parser.add_argument("--user", default="admin", help="Admin username for first-boot + login")
    parser.add_argument(
        "--password",
        default="smoke-install-pass",
        help="Admin password for first-boot + login",
    )
    parser.add_argument(
        "--keyboard-layout",
        default="us",
        choices=("us", "br-abnt2"),
        help="Keyboard layout selected in the installer and first boot",
    )
    parser.add_argument(
        "--module-profile",
        default="basic",
        choices=("basic", "full", "custom"),
        help="Module profile selected in the first-boot wizard",
    )
    parser.add_argument(
        "--modules-index-url",
        default="",
        help="Override the first-boot modules-index URL prompt",
    )
    parser.add_argument(
        "--first-boot-net",
        action="store_true",
        help="Attach real QEMU user-net (SLIRP NAT) on first boot so the "
        "module bootstrap can fetch the index/payloads over DNS+TLS+redirect "
        "(networked module-download regression gate; off by default)",
    )
    parser.add_argument(
        "--require-module-install",
        action="store_true",
        help="Fail unless first boot completed at least one module and rebooted",
    )
    parser.add_argument("--verbose", action="store_true", help="Print live serial output")
    return parser.parse_args()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def prepare_exclusive_disk(path: Path, size: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.truncate(parse_size(size))


def prepare_guard_disk(path: Path, size: str) -> None:
    size_bytes = parse_size(size)
    if size_bytes < 8192:
        raise ValueError("guard disk must be at least 8192 bytes")
    prepare_exclusive_disk(path, size)
    try:
        with path.open("r+b") as stream:
            stream.write(b"CAPYOS-INSTALLER-GUARD-BEGIN-v1")
            stream.seek(size_bytes - 4096)
            stream.write(b"CAPYOS-INSTALLER-GUARD-END-v1")
    except BaseException:
        cleanup_file(path)
        raise


def attached_disks(guard_disk: Path | None) -> tuple[Path, ...]:
    return (guard_disk,) if guard_disk is not None else ()


def require_safe_disk_path(repo_root: Path, path: Path) -> None:
    safe_root = (repo_root / "build/ci").resolve()
    try:
        path.relative_to(safe_root)
    except ValueError as exc:
        raise ValueError(f"destructive smoke disk must stay under {safe_root}") from exc
    if path == safe_root:
        raise ValueError("destructive smoke disk must name a file under build/ci")


def run_installer_boot(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    iso_path: Path,
    disk_path: Path,
    installer_log: Path,
    installer_debugcon_log: Path,
    parsed: argparse.Namespace,
    guard_disk: Path | None,
) -> None:
    print("[info] boot #0: official ISO installer")
    if parsed.verbose:
        print("[info] installer live output suppressed until recovery key redaction")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=installer_log,
        debugcon_log=installer_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=False,
        iso_path=iso_path,
        boot_from="cdrom",
        extra_disks=attached_disks(guard_disk),
    )
    try:
        complete_iso_install(
            session=session,
            timeout=parsed.step_timeout,
            keyboard_layout=parsed.keyboard_layout,
            user=parsed.user,
            password=parsed.password,
            expected_eligible_targets=2 if guard_disk is not None else 1,
            target_selection=parsed.target_selection,
            target_size_mib=(
                parse_size(parsed.disk_size) // (1024 * 1024)
                if guard_disk is not None
                else None
            ),
        )
    finally:
        session.stop()


def extract_volume_key(installer_log: Path) -> str | None:
    try:
        text = installer_log.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    match = re.search(r"\b([A-Z0-9]{4}(?:-[A-Z0-9]{4}){5})\b", text)
    if not match:
        return None
    volume_key = match.group(1)
    installer_log.write_text(
        text.replace(volume_key, "[REDACTED-VOLUME-RECOVERY-KEY]"),
        encoding="utf-8",
    )
    return volume_key


def installer_failed_before_loader(installer_log: Path) -> bool:
    """Return true only for a firmware-level miss before CapyOS starts."""
    try:
        text = installer_log.read_text(encoding="latin-1", errors="replace")
    except OSError:
        return False
    if "CapyOS UEFI loader" in text:
        return False
    return any(
        marker in text
        for marker in (
            "Start HTTP Boot",
            "EFI Internal Shell",
            "failed to load Boot",
        )
    )


def run_boot1(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot1_log: Path,
    boot1_debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
    volume_key: str | None,
    guard_disk: Path | None,
) -> bool:
    print("[info] boot #1: first boot from installed disk")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=boot1_log,
        debugcon_log=boot1_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
        networking=parsed.first_boot_net,
        extra_disks=attached_disks(guard_disk),
    )
    try:
        setup_result = maybe_run_first_boot_setup(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            keyboard_layout=parsed.keyboard_layout,
            volume_key=volume_key,
            module_profile=parsed.module_profile,
            modules_index_url=parsed.modules_index_url,
            require_interactive=True,
        )
        if setup_result == "rebooted":
            return False
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        ensure_shell_after_login(session, parsed.step_timeout, mode)
        smoke_first_boot(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            marker=marker,
        )
        return True
    finally:
        session.stop()


def run_marker_write_boot(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    log_path: Path,
    debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
    volume_key: str | None,
    guard_disk: Path | None,
) -> None:
    print("[info] boot #2: login and persistence marker write")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=log_path,
        debugcon_log=debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
        networking=parsed.first_boot_net,
        extra_disks=attached_disks(guard_disk),
    )
    try:
        setup_result = maybe_run_first_boot_setup(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            keyboard_layout=parsed.keyboard_layout,
            volume_key=volume_key,
            module_profile=parsed.module_profile,
            modules_index_url=parsed.modules_index_url,
            require_interactive=False,
        )
        if setup_result == "rebooted":
            raise RuntimeError("first-boot setup rebooted twice before persistence write")
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        ensure_shell_after_login(session, parsed.step_timeout, mode)
        smoke_first_boot(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            marker=marker,
        )
    finally:
        session.stop()


def run_boot2(
    qemu_bin: str,
    ovmf_code: str,
    ovmf_vars_runtime: Path,
    disk_path: Path,
    boot2_log: Path,
    boot2_debugcon_log: Path,
    parsed: argparse.Namespace,
    marker: str,
    guard_disk: Path | None,
    boot_number: int,
) -> None:
    print(f"[info] boot #{boot_number}: persistence validation")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=boot2_log,
        debugcon_log=boot2_debugcon_log,
        memory_mb=parsed.memory,
        storage_bus=parsed.storage_bus,
        verbose=parsed.verbose,
        extra_disks=attached_disks(guard_disk),
    )
    try:
        mk = session.marker()
        session.wait_for_any(
            ["Usuario:", "User:"],
            timeout=parsed.step_timeout * 4,
            start_at=mk,
        )
        mode = login(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            allow_desktop=True,
        )
        ensure_shell_after_login(session, parsed.step_timeout, mode)
        smoke_second_boot(
            session=session,
            timeout=parsed.step_timeout,
            user=parsed.user,
            password=parsed.password,
            marker=marker,
        )
    finally:
        session.stop()


def main() -> int:
    parsed = parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    log_base = (repo_root / parsed.log).resolve()
    disk_path = (repo_root / parsed.disk).resolve()
    guard_disk = (repo_root / parsed.guard_disk).resolve() if parsed.guard_disk else None
    installer_log = log_base.with_name(log_base.stem + ".installer" + log_base.suffix)
    boot1_log = log_base.with_name(log_base.stem + ".boot1" + log_base.suffix)
    marker_write_log = log_base.with_name(log_base.stem + ".marker-write" + log_base.suffix)
    boot2_log = log_base.with_name(log_base.stem + ".boot2" + log_base.suffix)
    installer_debugcon_log = log_base.with_name(log_base.stem + ".installer.debugcon.log")
    boot1_debugcon_log = log_base.with_name(log_base.stem + ".boot1.debugcon.log")
    marker_write_debugcon_log = log_base.with_name(log_base.stem + ".marker-write.debugcon.log")
    boot2_debugcon_log = log_base.with_name(log_base.stem + ".boot2.debugcon.log")
    marker = "persist-ok"

    try:
        qemu_bin = resolve_qemu_binary(parsed.qemu)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    try:
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(parsed.ovmf)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    ovmf_vars_runtime: Path | None = None
    target_created = False
    guard_created = False
    smoke_completed = False
    try:
        run_build_if_requested(repo_root, parsed.build)
        iso_path = validate_iso_artifact(repo_root, parsed.iso)
        if parsed.target_selection < 1:
            raise ValueError("target selection must be positive")
        ovmf_runtime_path = log_base.with_name(
            log_base.stem + ".OVMF_VARS.runtime.fd"
        ).resolve()
        if ovmf_runtime_path == disk_path or ovmf_runtime_path == guard_disk:
            raise ValueError("OVMF runtime path collides with a destructive disk")
        if not parsed.allow_external_disks:
            require_safe_disk_path(repo_root, disk_path)
            if guard_disk is not None:
                require_safe_disk_path(repo_root, guard_disk)
        if guard_disk is not None:
            if guard_disk == disk_path:
                raise ValueError("target and guard disks must be different files")
            if parse_size(parsed.guard_disk_size) <= parse_size(parsed.disk_size):
                raise ValueError("guard disk must be larger than the install target")
        prepare_exclusive_disk(disk_path, parsed.disk_size)
        target_created = True
        target_before = file_sha256(disk_path)
        guard_before = ""
        if guard_disk is not None:
            prepare_guard_disk(guard_disk, parsed.guard_disk_size)
            guard_created = True
            guard_before = file_sha256(guard_disk)
        ovmf_vars_runtime = create_runtime_ovmf_vars(log_base, ovmf_vars_template)

        installer_error: BaseException | None = None
        hash_error: BaseException | None = None
        guard_after_install = ""
        target_after_install = ""
        for installer_attempt in range(2):
            installer_error = None
            try:
                run_installer_boot(
                    qemu_bin=qemu_bin,
                    ovmf_code=ovmf_code,
                    ovmf_vars_runtime=ovmf_vars_runtime,
                    iso_path=iso_path,
                    disk_path=disk_path,
                    installer_log=installer_log,
                    installer_debugcon_log=installer_debugcon_log,
                    parsed=parsed,
                    guard_disk=guard_disk,
                )
            except BaseException as exc:
                installer_error = exc
            hash_error = None
            try:
                guard_after_install = (
                    file_sha256(guard_disk) if guard_disk is not None else ""
                )
                target_after_install = file_sha256(disk_path)
            except BaseException as exc:
                hash_error = exc
                break
            safe_to_retry = (
                installer_attempt == 0
                and installer_error is not None
                and target_after_install == target_before
                and (guard_disk is None or guard_after_install == guard_before)
                and installer_failed_before_loader(installer_log)
            )
            if not safe_to_retry:
                break
            print("[warn] firmware missed installer ISO; retrying once with fresh OVMF state")
            cleanup_file(ovmf_vars_runtime)
            ovmf_vars_runtime = create_runtime_ovmf_vars(log_base, ovmf_vars_template)
        volume_key = extract_volume_key(installer_log)
        if hash_error is not None:
            raise hash_error from installer_error
        if guard_disk is not None and guard_after_install != guard_before:
            raise RuntimeError("guard disk changed during installer boot") from installer_error
        if target_after_install == target_before:
            raise RuntimeError("install target remained byte-identical after installer boot") from installer_error
        if installer_error is not None:
            raise installer_error

        marker_written = run_boot1(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot1_log=boot1_log,
            boot1_debugcon_log=boot1_debugcon_log,
            parsed=parsed,
            marker=marker,
            volume_key=volume_key,
            guard_disk=None,
        )
        if parsed.require_module_install:
            boot1_text = boot1_log.read_text(encoding="latin-1", errors="replace")
            if marker_written or not module_install_completed(boot1_text):
                raise RuntimeError("required module installation did not complete and reboot")
        persistence_boot_number = 2
        if not marker_written:
            run_marker_write_boot(
                qemu_bin=qemu_bin,
                ovmf_code=ovmf_code,
                ovmf_vars_runtime=ovmf_vars_runtime,
                disk_path=disk_path,
                log_path=marker_write_log,
                debugcon_log=marker_write_debugcon_log,
                parsed=parsed,
                marker=marker,
                volume_key=volume_key,
                guard_disk=None,
            )
            persistence_boot_number = 3
        run_boot2(
            qemu_bin=qemu_bin,
            ovmf_code=ovmf_code,
            ovmf_vars_runtime=ovmf_vars_runtime,
            disk_path=disk_path,
            boot2_log=boot2_log,
            boot2_debugcon_log=boot2_debugcon_log,
            parsed=parsed,
            marker=marker,
            guard_disk=None,
            boot_number=persistence_boot_number,
        )
        smoke_completed = True
    except Exception as exc:
        print(f"[err] ISO smoke failed: {exc}", file=sys.stderr)
        print_log_tail(installer_log)
        print_log_tail(boot1_log)
        print_log_tail(marker_write_log)
        print_log_tail(boot2_log)
        if target_created:
            print(f"[info] preserving failed install target: {disk_path}", file=sys.stderr)
        if guard_created and guard_disk is not None:
            print(f"[info] preserving failed installer guard: {guard_disk}", file=sys.stderr)
        return 1
    finally:
        cleanup_file(ovmf_vars_runtime)
        if smoke_completed and not parsed.keep_disk:
            if target_created:
                cleanup_file(disk_path)
            if guard_created:
                cleanup_file(guard_disk)

    if guard_disk is not None:
        print("[ok] larger multi-disk guard remained byte-identical.")
    print("[ok] smoke x64 ISO install + persistence passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
