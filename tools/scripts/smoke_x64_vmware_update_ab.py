#!/usr/bin/env python3
"""VMware + UEFI + E1000 gates for the Etapa 8 signed A/B update cycle.

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

The default mode is the hermetic lab gate and requires a kernel built with
CAPYOS_UPDATE_LAB_TRUST_KEY_HEX. ``--production`` instead installs a released
predecessor ISO, verifies already-published materials without a private key and
drives the guest through GitHub's public Latest route. Production deliberately
executes rollback before confirmation: once the candidate is confirmed, its
runtime version equals latest.ini and anti-downgrade correctly forbids applying
the same release a second time.
"""

from __future__ import annotations

import argparse
import hashlib
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
    LAB_BANNER,
    PRODUCTION_CYCLE_ORDER,
    PRODUCTION_EVIDENCE_FORMAT,
    PRODUCTION_TRUST_ANCHOR,
    TRACK,
    TRUST_ANCHOR,
    channel_extended_from_version_yaml,
    compare_update_versions,
    manifest_url,
    payload_url,
    release_tag_from_version_yaml,
    render_evidence,
    render_production_evidence,
    render_update_ab_vmx,
    sanitize_public_text,
    stable_runtime_identity_marker,
    validate_evidence,
    validate_production_evidence,
)
from smoke_x64_update_ab_flow import (  # noqa: E402
    DOWNLOAD_OK,
    assert_armed_attempt_state,
    assert_attempt_pending,
    assert_equal_release_refused,
    assert_http_endpoint_reachable,
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
from update_manifest_common import (  # noqa: E402
    PINNED_PUBLIC_KEY_HEX,
    ensure_regular_file,
    normalize_public_key_hex,
    parse_manifest,
    payload_metadata,
    verify_signature,
)

PRODUCTION_SOURCE = "github:henriquefarisco/CapyOS"


def default_openssl() -> str:
    discovered = shutil.which("openssl")
    if discovered:
        return discovered
    if os.name == "nt":
        git_openssl = Path(r"C:\Program Files\Git\usr\bin\openssl.exe")
        if git_openssl.is_file():
            return str(git_openssl)
    return "openssl"


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
    parser.add_argument("--production", action="store_true")
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--expected-public-key-hex")
    parser.add_argument("--current-version", required=True)
    parser.add_argument("--published-at")
    parser.add_argument("--payload", default="build/capyos64.bin")
    parser.add_argument("--host")
    parser.add_argument("--http-port", type=int, default=18083)
    parser.add_argument("--www-root", default="build/ci/update-ab/www", type=Path)
    parser.add_argument("--production-manifest", type=Path)
    parser.add_argument("--production-payload", type=Path)
    parser.add_argument("--expected-iso-sha256")
    parser.add_argument("--openssl", default=default_openssl())
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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_vmx_generated_mac(vmx_path: Path) -> str:
    """Return the normalized E1000 identity assigned by VMware."""
    key = "ethernet0.generatedaddress"
    for raw_line in vmx_path.read_text(encoding="utf-8", errors="strict").splitlines():
        name, separator, raw_value = raw_line.partition("=")
        if separator and name.strip().lower() == key:
            value = raw_value.strip().strip('"').upper()
            octets = value.split(":")
            if len(octets) != 6 or any(
                len(octet) != 2
                or any(char not in "0123456789ABCDEF" for char in octet)
                for octet in octets
            ):
                raise RuntimeError(
                    f"VMware wrote an invalid ethernet0.generatedAddress: {value!r}"
                )
            if int(octets[0], 16) & 1:
                raise RuntimeError("VMware generated a multicast station address")
            return value
    raise RuntimeError("VMware did not persist ethernet0.generatedAddress")


def assert_guest_uses_vmx_mac(console, timeout: float, vmx_path: Path) -> None:
    expected_mac = read_vmx_generated_mac(vmx_path)
    run_cmd(
        console,
        "net-status",
        timeout,
        expect=f"mac={expected_mac}",
        expect_ignore_line_breaks=True,
    )


def collect_restage_failure_diagnostics(
    console, timeout: float, payload_endpoint: str
) -> None:
    """Preserve transport state and test one clean retry after a restage failure."""
    checks = (
        ("update status", "update-status", "summary=payload download failed"),
        ("network status", "net-status", "runtime=ready ready=yes"),
        (
            "direct payload fetch",
            f"net-fetch {payload_endpoint}",
            "status=200",
        ),
        (
            "payload retry",
            "update-download-payload",
            DOWNLOAD_OK,
        ),
    )
    for label, command, expected in checks:
        try:
            run_cmd(console, command, timeout * 8, expect=expected)
            print(f"[diag] {label}: passed")
        except Exception as exc:  # noqa: BLE001 - preserve every diagnostic
            print(f"[diag] {label}: {type(exc).__name__}: {exc}")


def normalize_sha256_hex(value: str, label: str) -> str:
    normalized = value.strip().lower()
    if len(normalized) != 64 or any(
        char not in "0123456789abcdef" for char in normalized
    ):
        raise ValueError(f"{label} must be lower-case or upper-case hex64")
    return normalized


def public_manifest_url(source: str) -> str:
    prefix = "github:"
    if not source.startswith(prefix):
        raise ValueError(f"unsupported production source: {source!r}")
    return (
        f"https://github.com/{source[len(prefix):]}"
        "/releases/latest/download/latest.ini"
    )


def require_lab_arguments(args: argparse.Namespace) -> None:
    missing = [
        name
        for name in ("private_key", "expected_public_key_hex", "published_at", "host")
        if not getattr(args, name)
    ]
    if missing:
        required = ", ".join("--" + name.replace("_", "-") for name in missing)
        raise ValueError(f"lab gate requires: {required}")
    if any(
        value is not None
        for value in (
            args.production_manifest,
            args.production_payload,
        )
    ):
        raise ValueError("production material arguments require --production")


def prepare_production_material(args: argparse.Namespace) -> dict[str, str]:
    if args.private_key is not None:
        raise ValueError("production gate refuses --private-key")
    if args.host is not None or args.published_at is not None:
        raise ValueError("production gate refuses lab host/published-at arguments")
    if not args.expected_iso_sha256:
        raise ValueError("production gate requires --expected-iso-sha256")
    missing = [
        name
        for name in (
            "production_manifest",
            "production_payload",
        )
        if getattr(args, name) is None
    ]
    if missing:
        raise ValueError(
            "production gate requires: "
            + ", ".join("--" + name.replace("_", "-") for name in missing)
        )

    manifest_path = args.production_manifest.resolve()
    payload_path = args.production_payload.resolve()
    ensure_regular_file(manifest_path, "production manifest")
    ensure_regular_file(payload_path, "production payload")

    expected_public_hex = normalize_public_key_hex(PINNED_PUBLIC_KEY_HEX)
    if args.expected_public_key_hex and normalize_public_key_hex(
        args.expected_public_key_hex
    ) != expected_public_hex:
        raise ValueError("production gate refuses an update trust-key override")
    actual_public = bytes.fromhex(expected_public_hex)

    fields, signed = parse_manifest(manifest_path.read_bytes())
    verify_signature(
        args.openssl,
        actual_public,
        signed,
        bytes.fromhex(fields["signature_ed25519"]),
    )
    if fields["channel"] != "stable" or fields["branch"] != "main":
        raise ValueError("production manifest must select stable/main")
    if fields["source"] != PRODUCTION_SOURCE:
        raise ValueError(
            f"production source mismatch: {fields['source']!r}"
        )

    repo_version = channel_extended_from_version_yaml(
        (REPO_ROOT / "VERSION.yaml").read_text(
            encoding="utf-8", errors="strict"
        ),
        "stable",
    )
    version = fields["available_version"]
    if version != repo_version:
        raise ValueError(
            f"published version {version!r} does not match checkout {repo_version!r}"
        )
    if compare_update_versions(version, args.current_version) <= 0:
        raise ValueError(
            "published production manifest is not newer than the predecessor"
        )

    size, digest = payload_metadata(payload_path)
    if str(size) != fields.get("payload_size"):
        raise ValueError("production payload size does not match latest.ini")
    if digest != fields["payload_sha256"].lower():
        raise ValueError("production payload SHA-256 does not match latest.ini")

    expected_payload_url = (
        f"https://github.com/{PRODUCTION_SOURCE[len('github:'):]}"
        f"/releases/download/v{version}/capyos64.bin"
    )
    if fields["payload_url"] != expected_payload_url:
        raise ValueError("production payload URL is not the immutable release asset")
    return {
        "release_tag": f"v{version}",
        "version": version,
        "manifest_url": public_manifest_url(fields["source"]),
        "payload_url": fields["payload_url"],
        "payload_size": str(size),
        "payload_sha256": digest,
        "trust_public_key": expected_public_hex,
        "expected_iso_sha256": normalize_sha256_hex(
            args.expected_iso_sha256, "expected predecessor ISO SHA-256"
        ),
    }


def assert_production_runtime(console, timeout: float, expected_version: str) -> None:
    if LAB_BANNER in console.text():
        raise RuntimeError("production boot exposed the lab trust override banner")
    marker = stable_runtime_identity_marker(expected_version)
    run_cmd(
        console,
        "print-version",
        timeout,
        expect=marker,
        expect_ignore_line_breaks=True,
    )
    if LAB_BANNER in console.text():
        raise RuntimeError("production boot exposed the lab trust override banner")


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
    args.iso = args.iso.resolve()
    if not args.iso.is_file():
        print(f"[err] ISO not found: {args.iso}", file=sys.stderr)
        return 2

    try:
        if args.production:
            material = prepare_production_material(args)
        else:
            require_lab_arguments(args)
        boot_media_sha256 = sha256_file(args.iso)
        if (
            args.production
            and boot_media_sha256 != material["expected_iso_sha256"]
        ):
            raise ValueError(
                "predecessor ISO SHA-256 does not match the published-byte pin"
            )
    except Exception as exc:  # noqa: BLE001 - report invalid input uniformly
        print(
            f"[err] VMware signed A/B update gate preflight failed: "
            f"{type(exc).__name__}: {exc}",
            file=sys.stderr,
        )
        return 2

    run_id = uuid.uuid4().hex[:12]
    run_root = work_root / run_id
    run_root.mkdir(parents=True, exist_ok=False)
    pipe_name = f"capyos-update-ab-{run_id}"
    logs = {
        phase: safe_root / f"smoke_x64_vmware_update_ab_{run_id}.{phase}.log"
        for phase in ("installer", "boot1", "boot1b", "boot2", "boot3", "boot4")
    }
    vmx_path = run_root / "capyos-update-ab.vmx"
    http_server = None
    recovery_key = ""
    completed = False
    try:
        if args.production:
            release_tag = material["release_tag"]
            version = material["version"]
            manifest_endpoint = material["manifest_url"]
            payload_endpoint = material["payload_url"]
            digest = material["payload_sha256"]
            size = int(material["payload_size"])
            print(
                "[info] production materials verified without a private key; "
                f"guest fetches {manifest_endpoint}"
            )
        else:
            release_tag = release_tag_from_version_yaml(
                (REPO_ROOT / "VERSION.yaml").read_text(
                    encoding="utf-8", errors="replace"
                ),
                args.current_version,
            )
            version, digest, size = build_signed_material(args, www_root)
            http_server = start_local_http_server(www_root, args.http_port)
            manifest_endpoint = manifest_url(args.host, args.http_port)
            payload_endpoint = payload_url(args.host, args.http_port)
            print(
                f"[info] signed lab manifest served on host :{args.http_port} "
                f"(guest fetches {manifest_endpoint})"
            )
        target_descriptor = run_root / "target.vmdk"
        create_vmdk(args.vdiskmanager, target_descriptor, args.target_size)
        if not target_descriptor.is_file():
            raise RuntimeError(
                f"VMware target descriptor was not created: {target_descriptor}"
            )

        vmx_text = render_update_ab_vmx(
            display_name=f"CapyOS signed A/B {run_id}",
            iso_path=args.iso,
            target_descriptor=target_descriptor,
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
            target_descriptor=target_descriptor,
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
                if args.production:
                    assert_production_runtime(
                        console, args.step_timeout, args.current_version
                    )
                require_boot_attempt(console.text(), 0, "confirmed")
                assert_guest_uses_vmx_mac(console, args.step_timeout, vmx_path)
                assert_provider_ready(console, args.step_timeout)
                run_cmd(
                    console,
                    "update-channel show",
                    args.step_timeout,
                    expect=manifest_endpoint,
                    expect_ignore_line_breaks=True,
                )
                if not args.production:
                    assert_http_endpoint_reachable(
                        console, args.step_timeout, manifest_endpoint
                    )
                stage_and_arm_update(
                    console,
                    args.step_timeout,
                    expect_version=version,
                    expect_payload_url=payload_endpoint,
                    expect_payload_sha256=digest,
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

        if args.production:
            # A confirmed candidate reports the same version as Latest and must
            # not be reapplied. Exercise rollback first, then restage from the
            # restored predecessor and confirm the second attempt.
            console = start_console(
                args.vmrun, vmx_path, pipe_name, logs["boot2"],
                secrets=(recovery_key,), verbose=args.verbose,
            )
            try:
                login_shell(args, console)
                assert_production_runtime(console, args.step_timeout, version)
                require_boot_attempt(console.text(), 1, "pending")
                assert_attempt_pending(console, args.step_timeout)
                sync_and_reboot(args, console)
            finally:
                console.stop()

            console = start_console(
                args.vmrun, vmx_path, pipe_name, logs["boot3"],
                secrets=(recovery_key,), verbose=args.verbose,
            )
            try:
                login_shell(args, console)
                assert_production_runtime(
                    console, args.step_timeout, args.current_version
                )
                require_boot_attempt(console.text(), 0, "rollback")
                assert_rollback_reported(console, args.step_timeout)
                assert_slot_state(console, args.step_timeout, "state=failed")
                stage_and_arm_update(
                    console,
                    args.step_timeout,
                    expect_version=version,
                    expect_payload_url=payload_endpoint,
                    expect_payload_sha256=digest,
                )
                assert_armed_attempt_state(console, args.step_timeout)
                sync_and_reboot(args, console)
            finally:
                console.stop()

            console = start_console(
                args.vmrun, vmx_path, pipe_name, logs["boot4"],
                secrets=(recovery_key,), verbose=args.verbose,
            )
            try:
                login_shell(args, console)
                assert_production_runtime(console, args.step_timeout, version)
                require_boot_attempt(console.text(), 1, "pending")
                assert_attempt_pending(console, args.step_timeout)
                confirm_boot_health(console, args.step_timeout)
                assert_slot_state(
                    console, args.step_timeout, "health=confirmed [ACTIVE]"
                )
                assert_equal_release_refused(
                    console, args.step_timeout, current_version=version
                )
                trigger_poweroff(console, args.step_timeout * 2)
            finally:
                console.stop()
        else:
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
                try:
                    restage_and_arm_update(
                        console, args.step_timeout, expect_version=version
                    )
                except Exception as exc:
                    collect_restage_failure_diagnostics(
                        console, args.step_timeout, payload_endpoint
                    )
                    raise RuntimeError(
                        "second-cycle payload restage failed; diagnostics captured"
                    ) from exc
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

        if args.production:
            evidence = {
                "format": PRODUCTION_EVIDENCE_FORMAT,
                "release_tag": release_tag,
                "track": TRACK,
                "provider": "vmware-workstation",
                "trust_anchor": PRODUCTION_TRUST_ANCHOR,
                "trust_public_key": material["trust_public_key"],
                "predecessor_version": args.current_version,
                "manifest_version": version,
                "manifest_url": manifest_endpoint,
                "payload_url": payload_endpoint,
                "payload_size": str(size),
                "payload_sha256": digest,
                "boot_media_sha256": boot_media_sha256,
                "first_attempt_slot": "1",
                "second_attempt_slot": "1",
                "boots_observed": str(boots + 3),
                "cycle_order": PRODUCTION_CYCLE_ORDER,
                "lab_override_absent": "yes",
                "public_latest_route": "yes",
                "provider_ready": "yes",
                "signed_manifest_accepted": "yes",
                "payload_verified": "yes",
                "inactive_slot_written": "yes",
                "attempt_armed": "yes",
                "loader_consumed_attempt": "yes",
                "unconfirmed_attempt_spent": "yes",
                "loader_applied_rollback": "yes",
                "rollback_reported": "yes",
                "confirmed_slot_restored": "yes",
                "second_attempt_armed": "yes",
                "health_confirmed": "yes",
                "equal_release_refused": "yes",
                "recovery_key_included": "no",
            }
            validate_production_evidence(evidence)
            rendered_evidence = render_production_evidence(evidence)
        else:
            evidence = {
                "format": EVIDENCE_FORMAT,
                "release_tag": release_tag,
                "track": TRACK,
                "provider": "vmware-workstation",
                "trust_anchor": TRUST_ANCHOR,
                "lab_public_key": args.expected_public_key_hex.lower(),
                "manifest_version": version,
                "manifest_url": manifest_endpoint,
                "payload_url": payload_endpoint,
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
            rendered_evidence = render_evidence(evidence)
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(
            sanitize_public_text(rendered_evidence, (recovery_key,)),
            encoding="ascii",
        )
        completed = True
        mode = "production" if args.production else "lab"
        print(f"[ok] VMware signed A/B {mode} gate passed: {evidence_path}")
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
