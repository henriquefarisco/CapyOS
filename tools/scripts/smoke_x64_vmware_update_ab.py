#!/usr/bin/env python3
"""Official VMware + UEFI + E1000 gate for the Etapa 8 signed A/B update cycle.

Creates a scratch UEFI VM, installs the official ISO on a blank disk, then drives
two complete signed update cycles across four power cycles:

  boot 1  provider ready -> signed fetch -> verified download -> apply -> reboot
  boot 2  loader spent the attempt -> confirm health -> apply cycle two -> reboot
  boot 3  attempt spent and deliberately NOT confirmed -> reboot
  boot 4  loader restored the confirmed slot; the updater reports the rollback

Runs with Windows Python (vmrun + vmware-vdiskmanager + a named-pipe COM1), and
reuses the console, redaction and evidence discipline of the installer wizard
gate. The behavioural assertions come from smoke_x64_update_ab_flow so this gate
and the QEMU pre-flight can never disagree.

Requires a kernel built with CAPYOS_UPDATE_LAB_TRUST_KEY_HEX matching
--expected-public-key-hex and CAPYOS_UPDATE_LAB_MANIFEST_URL pointing at
http://<--host>:<--http-port>/latest.ini. `make smoke-x64-vmware-update-ab`
wires all of it through tools/scripts/update_ab_lab_config.py.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_auth import (  # noqa: E402  (sys.path tweak above)
    complete_iso_install,
    login,
    maybe_run_first_boot_setup,
)
from smoke_x64_helpers import (  # noqa: E402
    ensure_shell_after_login,
    run_cmd,
    trigger_poweroff,
    trigger_reboot,
)
from smoke_x64_qemu_update_ab import build_signed_material  # noqa: E402
from smoke_x64_update_ab_contract import (  # noqa: E402
    EVIDENCE_FORMAT,
    TRACK,
    TRUST_ANCHOR,
    manifest_url,
    next_prerelease_version,
    payload_url,
    release_tag_from_version_yaml,
    render_evidence,
    render_update_ab_vmx,
    sanitize_public_text,
    validate_evidence,
)
from smoke_x64_update_ab_flow import (  # noqa: E402
    assert_armed_attempt_state,
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
from smoke_x64_vmware_installer import (  # noqa: E402
    create_vmdk,
    extract_recovery_key,
    start_console,
    write_vmx,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument(
        "--vmrun",
        type=Path,
        default=Path(r"C:\Program Files\VMware\VMware Workstation\vmrun.exe"),
    )
    parser.add_argument(
        "--vdiskmanager",
        type=Path,
        default=Path(
            r"C:\Program Files\VMware\VMware Workstation\vmware-vdiskmanager.exe"
        ),
    )
    parser.add_argument("--work-root", default="build/ci/vmware-update-ab", type=Path)
    parser.add_argument(
        "--evidence", default="build/ci/update-ab-vmware-evidence.manifest", type=Path
    )
    parser.add_argument("--target-size", default="2GB")
    parser.add_argument("--step-timeout", type=float, default=240.0)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="vmware-update-ab-pass")
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--expected-public-key-hex", required=True)
    parser.add_argument("--current-version", required=True)
    parser.add_argument("--published-at", required=True)
    parser.add_argument("--payload", default="build/capyos64.bin")
    parser.add_argument("--host", required=True)
    parser.add_argument("--http-port", type=int, default=18083)
    parser.add_argument("--www-root", default="build/ci/update-ab/www", type=Path)
    parser.add_argument("--keep-vm", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def login_shell(args: argparse.Namespace, console) -> None:
    mode = login(
        console, args.step_timeout, args.user, args.password, allow_desktop=True
    )
    ensure_shell_after_login(console, args.step_timeout, mode)


def sync_and_reboot(args: argparse.Namespace, console) -> None:
    run_cmd(console, "do-sync", args.step_timeout, expect_optional=True)
    if not trigger_reboot(console, args.step_timeout * 2):
        raise RuntimeError("shutdown-reboot did not end the VMware boot phase")


def main() -> int:
    args = parse_args()
    if os.name != "nt":
        print(
            "[err] VMware signed A/B gate must run with Windows Python",
            file=sys.stderr,
        )
        return 2

    safe_root = (REPO_ROOT / "build" / "ci").resolve()
    work_root = (REPO_ROOT / args.work_root).resolve()
    evidence_path = (REPO_ROOT / args.evidence).resolve()
    www_root = (REPO_ROOT / args.www_root).resolve()
    for path in (work_root, evidence_path):
        if safe_root not in path.parents and path != safe_root:
            print(f"[err] VMware gate path must stay under {safe_root}: {path}",
                  file=sys.stderr)
            return 2
    if not args.iso.is_file():
        print(f"[err] ISO not found: {args.iso}", file=sys.stderr)
        return 2

    run_id = uuid.uuid4().hex[:12]
    run_root = work_root / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    pipe_name = f"capyos-update-ab-{run_id}"
    release_tag = release_tag_from_version_yaml(
        (REPO_ROOT / "VERSION.yaml").read_text(encoding="utf-8", errors="replace")
    )
    version = next_prerelease_version(args.current_version)
    logs = {
        phase: safe_root / f"smoke_x64_vmware_update_ab_{run_id}.{phase}.log"
        for phase in ("installer", "boot1", "boot1b", "boot2", "boot3", "boot4")
    }
    vmx_path = run_root / "capyos-update-ab.vmx"
    http_server = None
    recovery_key = ""
    completed = False
    try:
        _, digest, size = build_signed_material(args, www_root)
        http_server = start_local_http_server(www_root, args.http_port)
        print(
            f"[info] signed lab manifest served on host :{args.http_port} "
            f"(guest fetches {manifest_url(args.host, args.http_port)})"
        )
        target = create_vmdk(
            args.vdiskmanager, run_root / "target.vmdk", args.target_size
        )

        vmx_text = render_update_ab_vmx(
            display_name=f"CapyOS signed A/B {run_id}",
            iso_path=args.iso,
            target_descriptor=target,
            pipe_name=pipe_name,
            boot_from="cdrom",
        )
        write_vmx(vmx_path, vmx_text)
        installer = start_console(
            args.vmrun, vmx_path, pipe_name, logs["installer"], verbose=False
        )
        try:
            complete_iso_install(
                installer,
                args.step_timeout,
                "us",
                args.user,
                args.password,
                expected_eligible_targets=1,
                target_selection=1,
            )
            recovery_key = extract_recovery_key(installer.text())
            installer.secrets = (recovery_key,)
        finally:
            installer.stop()

        vmx_text = render_update_ab_vmx(
            display_name=f"CapyOS signed A/B {run_id}",
            iso_path=args.iso,
            target_descriptor=target,
            pipe_name=pipe_name,
            boot_from="hdd",
        )
        write_vmx(vmx_path, vmx_text)

        boots = 1
        armed = False
        for phase, interactive in (("boot1", True), ("boot1b", False)):
            console = start_console(
                args.vmrun, vmx_path, pipe_name, logs[phase],
                secrets=(recovery_key,), verbose=args.verbose,
            )
            try:
                outcome = maybe_run_first_boot_setup(
                    console,
                    args.step_timeout,
                    args.user,
                    args.password,
                    "us",
                    volume_key=recovery_key,
                    module_profile="basic",
                    require_interactive=interactive,
                )
                if outcome == "rebooted":
                    if not interactive:
                        raise RuntimeError(
                            "VMware first boot rebooted twice before the update cycle"
                        )
                    boots += 1
                    continue
                login_shell(args, console)
                require_boot_attempt(console.text(), 0, "confirmed")
                assert_provider_ready(console, args.step_timeout)
                run_cmd(
                    console,
                    "update-channel show",
                    args.step_timeout,
                    expect=manifest_url(args.host, args.http_port),
                    expect_ignore_line_breaks=True,
                )
                stage_and_arm_update(
                    console, args.step_timeout, expect_version=version
                )
                assert_armed_attempt_state(console, args.step_timeout)
                sync_and_reboot(args, console)
                armed = True
            finally:
                console.stop()
            if armed:
                break
        if not armed:
            raise RuntimeError("VMware first boot never armed the first attempt")

        console = start_console(
            args.vmrun, vmx_path, pipe_name, logs["boot2"],
            secrets=(recovery_key,), verbose=args.verbose,
        )
        try:
            login_shell(args, console)
            require_boot_attempt(console.text(), 1, "pending")
            assert_attempt_pending(console, args.step_timeout)
            confirm_boot_health(console, args.step_timeout)
            assert_slot_state(
                console, args.step_timeout, "health=confirmed [ACTIVE]"
            )
            restage_and_arm_update(console, args.step_timeout, expect_version=version)
            assert_armed_attempt_state(console, args.step_timeout)
            sync_and_reboot(args, console)
        finally:
            console.stop()

        console = start_console(
            args.vmrun, vmx_path, pipe_name, logs["boot3"],
            secrets=(recovery_key,), verbose=args.verbose,
        )
        try:
            login_shell(args, console)
            require_boot_attempt(console.text(), 0, "pending")
            assert_attempt_pending(console, args.step_timeout)
            sync_and_reboot(args, console)
        finally:
            console.stop()

        console = start_console(
            args.vmrun, vmx_path, pipe_name, logs["boot4"],
            secrets=(recovery_key,), verbose=args.verbose,
        )
        try:
            login_shell(args, console)
            require_boot_attempt(console.text(), 1, "rollback")
            assert_rollback_reported(console, args.step_timeout)
            assert_slot_state(console, args.step_timeout, "state=failed")
            trigger_poweroff(console, args.step_timeout * 2)
        finally:
            console.stop()

        evidence = {
            "format": EVIDENCE_FORMAT,
            "release_tag": release_tag,
            "track": TRACK,
            "provider": "vmware-workstation",
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
        evidence_path.write_text(
            sanitize_public_text(render_evidence(evidence), (recovery_key,)),
            encoding="ascii",
        )
        completed = True
        print(f"[ok] VMware signed A/B update gate passed: {evidence_path}")
        for path in logs.values():
            if path.exists():
                print(f"[ok] evidence log: {path}")
        return 0
    except Exception as exc:  # noqa: BLE001 - the gate reports, never crashes
        print(
            f"[err] VMware signed A/B update gate failed: "
            f"{type(exc).__name__}: {exc}",
            file=sys.stderr,
        )
        print(f"[info] preserving VMware scratch evidence: {run_root}", file=sys.stderr)
        return 1
    finally:
        recovery_key = ""
        if http_server is not None:
            http_server.shutdown()
            http_server.server_close()
        if completed and not args.keep_vm:
            shutil.rmtree(run_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
