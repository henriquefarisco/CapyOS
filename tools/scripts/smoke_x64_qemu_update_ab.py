#!/usr/bin/env python3
"""QEMU pre-flight for the Etapa 8 signed A/B update gate.

Installs the official ISO on a blank disk, then drives two complete signed
update cycles across four power cycles: the first is confirmed healthy, the
second is deliberately left unconfirmed so the UEFI loader restores the
confirmed slot and the updater reports the rollback.

Development feedback / CI pre-flight only; VMware + UEFI + E1000 remains the
official release-acceptance gate (`smoke-x64-vmware-update-ab`).

Requires a kernel built with CAPYOS_UPDATE_LAB_TRUST_KEY_HEX matching
--expected-public-key-hex, and CAPYOS_UPDATE_LAB_MANIFEST_URL matching the
manifest URL served here. `make smoke-x64-qemu-update-ab` wires all three.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_auth import (  # noqa: E402  (sys.path tweak above)
    complete_iso_install,
    login,
    maybe_run_first_boot_setup,
)
from smoke_x64_common import (  # noqa: E402
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    validate_iso_artifact,
)
from smoke_x64_helpers import (  # noqa: E402
    ensure_shell_after_login,
    run_cmd,
    trigger_poweroff,
    trigger_reboot,
)
from smoke_x64_iso_install import (  # noqa: E402
    extract_volume_key,
    prepare_exclusive_disk,
    require_safe_disk_path,
)
from smoke_x64_update_ab_contract import (  # noqa: E402
    EVIDENCE_FORMAT,
    LAB_MANIFEST_NAME,
    LAB_PAYLOAD_NAME,
    LOCAL_HTTP_PORT,
    QEMU_SLIRP_GATEWAY,
    TRACK,
    TRUST_ANCHOR,
    manifest_url,
    next_prerelease_version,
    payload_url,
    release_tag_from_version_yaml,
    render_evidence,
    validate_evidence,
)
from smoke_x64_update_ab_flow import (  # noqa: E402
    assert_attempt_pending,
    assert_provider_ready,
    assert_rollback_reported,
    assert_slot_state,
    confirm_boot_health,
    require_boot_attempt,
    restage_and_arm_update,
    stage_and_arm_update,
    start_local_http_server,
)
from update_manifest_common import payload_metadata  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", default="build/CapyOS-Installer-UEFI.iso")
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf")
    parser.add_argument("--memory", type=int, default=1024)
    parser.add_argument("--storage-bus", choices=("sata", "nvme"), default="sata")
    parser.add_argument("--step-timeout", type=float, default=90.0)
    parser.add_argument("--log", default="build/ci/smoke_x64_qemu_update_ab.log")
    parser.add_argument("--disk", default="build/ci/smoke_x64_qemu_update_ab.img")
    parser.add_argument("--disk-size", default="2G")
    parser.add_argument("--keep-disk", action="store_true")
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="smoke-update-ab-pass")
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--expected-public-key-hex", required=True)
    parser.add_argument("--current-version", required=True)
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--payload", default="build/capyos64.bin")
    parser.add_argument("--http-port", type=int, default=LOCAL_HTTP_PORT)
    parser.add_argument("--host", default=QEMU_SLIRP_GATEWAY)
    parser.add_argument("--www-root", default="build/ci/update-ab/www")
    parser.add_argument("--evidence", default="build/ci/update-ab-evidence.manifest")
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def build_signed_material(args: argparse.Namespace, www_root: Path) -> tuple[str, str, int]:
    """Emit latest.ini + payload into the served directory. Returns metadata."""
    payload_src = (REPO_ROOT / args.payload).resolve()
    if not payload_src.is_file():
        raise RuntimeError(f"payload not found: {payload_src}")
    www_root.mkdir(parents=True, exist_ok=True)
    served_payload = www_root / LAB_PAYLOAD_NAME
    shutil.copyfile(payload_src, served_payload)
    size, digest = payload_metadata(served_payload)
    version = next_prerelease_version(args.current_version)
    manifest = www_root / LAB_MANIFEST_NAME
    subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "scripts" / "build_update_manifest.py"),
            "--version",
            version,
            "--channel",
            "stable",
            "--branch",
            "main",
            "--source",
            "github:henriquefarisco/CapyOS",
            "--published-at",
            args.published_at,
            "--payload",
            str(served_payload),
            "--payload-url",
            payload_url(args.host, args.http_port),
            "--private-key",
            str(args.private_key),
            "--expected-public-key-hex",
            args.expected_public_key_hex,
            "--allow-lab-http-payload-url",
            # The lab key lives under build/ci on the Windows-shared workspace,
            # where POSIX modes are fixed at 0777 and chmod is a no-op. It is a
            # per-run throwaway that signs nothing a shipped kernel accepts, so
            # the mode guard that protects the production key is waived here.
            "--allow-insecure-key",
            "--output",
            str(manifest),
            "--force",
            "--openssl",
            args.openssl,
        ],
        check=True,
        cwd=str(REPO_ROOT),
    )
    subprocess.run(
        [
            sys.executable,
            str(REPO_ROOT / "tools" / "scripts" / "verify_update_manifest.py"),
            "--manifest",
            str(manifest),
            "--payload",
            str(served_payload),
            "--expected-public-key-hex",
            args.expected_public_key_hex,
            "--expected-version",
            version,
            "--expected-payload-url",
            payload_url(args.host, args.http_port),
            "--allow-lab-http-payload-url",
            "--openssl",
            args.openssl,
        ],
        check=True,
        cwd=str(REPO_ROOT),
    )
    return version, digest, size


def phase_log_paths(log_base: Path, phase: str) -> tuple[Path, Path]:
    return (
        log_base.with_name(f"{log_base.stem}.{phase}{log_base.suffix}"),
        log_base.with_name(f"{log_base.stem}.{phase}.debugcon.log"),
    )


def boot_session(args, qemu_bin, ovmf_code, ovmf_vars, disk, phase, log_base, **extra):
    log_path, debugcon_log = phase_log_paths(log_base, phase)
    return boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars,
        disk_path=disk,
        log_path=log_path,
        debugcon_log=debugcon_log,
        memory_mb=args.memory,
        storage_bus=args.storage_bus,
        verbose=args.verbose,
        networking=True,
        **extra,
    ), log_path


def run_installer(args, qemu_bin, ovmf_code, ovmf_vars, disk, iso, log_base) -> str:
    session, log_path = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "installer", log_base,
        iso_path=iso, boot_from="cdrom",
    )
    try:
        complete_iso_install(
            session,
            args.step_timeout,
            "us",
            args.user,
            args.password,
            expected_eligible_targets=1,
            target_selection=1,
        )
    finally:
        session.stop()
    return extract_volume_key(log_path) or ""


def login_shell(args, session) -> None:
    mode = login(session, args.step_timeout, args.user, args.password,
                 allow_desktop=True)
    ensure_shell_after_login(session, args.step_timeout, mode)


def run_first_boot(args, qemu_bin, ovmf_code, ovmf_vars, disk, log_base, volume_key,
                   version) -> tuple[bool, int]:
    """Boot 1: finish setup, prove the provider, stage+arm the first attempt."""
    session, log_path = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "boot1", log_base
    )
    try:
        outcome = maybe_run_first_boot_setup(
            session,
            args.step_timeout,
            args.user,
            args.password,
            "us",
            volume_key=volume_key or None,
            module_profile="basic",
            require_interactive=True,
        )
        if outcome == "rebooted":
            return False, 0
        login_shell(args, session)
        require_boot_attempt(session.text(), 0, "confirmed")
        assert_provider_ready(session, args.step_timeout)
        run_cmd(
            session,
            "update-channel show",
            args.step_timeout,
            expect=manifest_url(args.host, args.http_port),
            expect_ignore_line_breaks=True,
        )
        stage_and_arm_update(session, args.step_timeout, expect_version=version)
        assert_slot_state(session, args.step_timeout, "state=valid")
        run_cmd(session, "do-sync", args.step_timeout, expect_optional=True)
        if not trigger_reboot(session, args.step_timeout * 2):
            raise RuntimeError("shutdown-reboot did not terminate the VM")
    finally:
        session.stop()
    _ = log_path
    return True, 1


def run_setup_completion_boot(args, qemu_bin, ovmf_code, ovmf_vars, disk, log_base,
                              volume_key, version) -> None:
    """Extra boot used only when the wizard rebooted to activate modules."""
    session, _ = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "boot1b", log_base
    )
    try:
        outcome = maybe_run_first_boot_setup(
            session,
            args.step_timeout,
            args.user,
            args.password,
            "us",
            volume_key=volume_key or None,
            module_profile="basic",
            require_interactive=False,
        )
        if outcome == "rebooted":
            raise RuntimeError("first-boot setup rebooted twice before the update cycle")
        login_shell(args, session)
        require_boot_attempt(session.text(), 0, "confirmed")
        assert_provider_ready(session, args.step_timeout)
        stage_and_arm_update(session, args.step_timeout, expect_version=version)
        run_cmd(session, "do-sync", args.step_timeout, expect_optional=True)
        if not trigger_reboot(session, args.step_timeout * 2):
            raise RuntimeError("shutdown-reboot did not terminate the VM")
    finally:
        session.stop()


def run_confirm_boot(args, qemu_bin, ovmf_code, ovmf_vars, disk, log_base,
                     version) -> None:
    """Boot 2: the loader spent the attempt; confirm health, then arm cycle two."""
    session, _ = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "boot2", log_base
    )
    try:
        login_shell(args, session)
        require_boot_attempt(session.text(), 1, "pending")
        assert_attempt_pending(session, args.step_timeout)
        confirm_boot_health(session, args.step_timeout)
        assert_slot_state(session, args.step_timeout, "health=confirmed [ACTIVE]")
        restage_and_arm_update(session, args.step_timeout, expect_version=version)
        run_cmd(session, "do-sync", args.step_timeout, expect_optional=True)
        if not trigger_reboot(session, args.step_timeout * 2):
            raise RuntimeError("shutdown-reboot did not terminate the VM")
    finally:
        session.stop()


def run_unconfirmed_boot(args, qemu_bin, ovmf_code, ovmf_vars, disk, log_base) -> None:
    """Boot 3: attempt spent and deliberately never confirmed."""
    session, _ = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "boot3", log_base
    )
    try:
        login_shell(args, session)
        require_boot_attempt(session.text(), 0, "pending")
        assert_attempt_pending(session, args.step_timeout)
        run_cmd(session, "do-sync", args.step_timeout, expect_optional=True)
        if not trigger_reboot(session, args.step_timeout * 2):
            raise RuntimeError("shutdown-reboot did not terminate the VM")
    finally:
        session.stop()


def run_rollback_boot(args, qemu_bin, ovmf_code, ovmf_vars, disk, log_base) -> None:
    """Boot 4: the loader restored the confirmed slot; the updater reports it."""
    session, _ = boot_session(
        args, qemu_bin, ovmf_code, ovmf_vars, disk, "boot4", log_base
    )
    try:
        login_shell(args, session)
        require_boot_attempt(session.text(), 1, "rollback")
        assert_rollback_reported(session, args.step_timeout)
        assert_slot_state(session, args.step_timeout, "state=failed")
        trigger_poweroff(session, args.step_timeout * 2)
    finally:
        session.stop()


def main() -> int:
    args = parse_args()
    log_base = (REPO_ROOT / args.log).resolve()
    disk_path = (REPO_ROOT / args.disk).resolve()
    www_root = (REPO_ROOT / args.www_root).resolve()
    evidence_path = (REPO_ROOT / args.evidence).resolve()
    require_safe_disk_path(REPO_ROOT, disk_path)
    require_safe_disk_path(REPO_ROOT, evidence_path)

    try:
        qemu_bin = resolve_qemu_binary(args.qemu)
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(args.ovmf)
        iso_path = validate_iso_artifact(REPO_ROOT, args.iso)
    except (FileNotFoundError, RuntimeError) as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    release_tag = release_tag_from_version_yaml(
        (REPO_ROOT / "VERSION.yaml").read_text(encoding="utf-8", errors="replace")
    )
    http_server = None
    ovmf_vars_runtime = None
    disk_created = False
    completed = False
    try:
        version, digest, size = build_signed_material(args, www_root)
        http_server = start_local_http_server(www_root, args.http_port)
        print(
            f"[info] signed lab manifest served on host :{args.http_port} "
            f"(guest fetches {manifest_url(args.host, args.http_port)})"
        )
        prepare_exclusive_disk(disk_path, args.disk_size)
        disk_created = True
        ovmf_vars_runtime = create_runtime_ovmf_vars(log_base, ovmf_vars_template)
        volume_key = run_installer(
            args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, iso_path, log_base
        )
        armed, boots = run_first_boot(
            args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, log_base,
            volume_key, version,
        )
        if not armed:
            print("[info] wizard rebooted to activate modules; completing setup")
            run_setup_completion_boot(
                args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, log_base,
                volume_key, version,
            )
            boots = 2
        run_confirm_boot(
            args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, log_base, version
        )
        run_unconfirmed_boot(
            args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, log_base
        )
        run_rollback_boot(
            args, qemu_bin, ovmf_code, ovmf_vars_runtime, disk_path, log_base
        )
        evidence = {
            "format": EVIDENCE_FORMAT,
            "release_tag": release_tag,
            "track": TRACK,
            "provider": "qemu-ovmf",
            "trust_anchor": TRUST_ANCHOR,
            "lab_public_key": args.expected_public_key_hex.lower(),
            "manifest_version": version,
            "manifest_url": manifest_url(args.host, args.http_port),
            "payload_url": payload_url(args.host, args.http_port),
            "payload_size": str(size),
            "payload_sha256": digest,
            "first_attempt_slot": "1",
            "second_attempt_slot": "0",
            "boots_observed": str(boots + 3),
            "provider_ready": "yes",
            "signed_manifest_accepted": "yes",
            "payload_verified": "yes",
            "inactive_slot_written": "yes",
            "attempt_armed": "yes",
            "loader_consumed_attempt": "yes",
            "health_confirmed": "yes",
            "second_attempt_armed": "yes",
            "unconfirmed_attempt_spent": "yes",
            "loader_applied_rollback": "yes",
            "rollback_reported": "yes",
            "confirmed_slot_restored": "yes",
            "recovery_key_included": "no",
        }
        validate_evidence(evidence)
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(render_evidence(evidence), encoding="ascii")
        completed = True
        print(f"[ok] signed A/B update cycle proven under QEMU: {evidence_path}")
        return 0
    except Exception as exc:  # noqa: BLE001 - the harness reports, never crashes
        print(f"[err] signed A/B update gate failed: {exc}", file=sys.stderr)
        for phase in ("installer", "boot1", "boot1b", "boot2", "boot3", "boot4"):
            log_path, _ = phase_log_paths(log_base, phase)
            if log_path.exists():
                print_log_tail(log_path)
        if disk_created:
            print(f"[info] preserving failed install target: {disk_path}", file=sys.stderr)
        return 1
    finally:
        if http_server is not None:
            http_server.shutdown()
            http_server.server_close()
        cleanup_file(ovmf_vars_runtime)
        if completed and not args.keep_disk and disk_created:
            cleanup_file(disk_path)


if __name__ == "__main__":
    raise SystemExit(main())
