#!/usr/bin/env python3
"""Host contract test for the Etapa 8 signed A/B update gate.

Plain script (no pytest), matching tools/scripts/test_installer_smoke_contract.py:
prints the first violation and returns 1. Wired into `make update-ab-selftest`,
which `release-check` runs, so the gate's frozen literals and the lab-only
payload-URL relaxation cannot drift without a red gate.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

import smoke_x64_update_ab_contract as contract  # noqa: E402
import smoke_x64_update_ab_flow as flow  # noqa: E402
from smoke_x64_session import text_contains_pattern  # noqa: E402
from update_manifest_common import (  # noqa: E402
    ManifestError,
    canonical_body,
    payload_url_prefixes,
    validate_fields,
)


def _evidence(**overrides: str) -> dict[str, str]:
    fields = {
        "format": contract.EVIDENCE_FORMAT,
        "release_tag": "0.8.0-alpha.319+20260728",
        "track": contract.TRACK,
        "provider": "qemu-ovmf",
        "trust_anchor": contract.TRUST_ANCHOR,
        "lab_public_key": "aa" * 32,
        "manifest_version": "0.8.0-alpha.320",
        "manifest_url": contract.manifest_url(),
        "payload_url": contract.payload_url(),
        "payload_size": "2863536",
        "payload_sha256": "bb" * 32,
        "first_attempt_slot": "1",
        "second_attempt_slot": "0",
        "boots_observed": "4",
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
    fields.update(overrides)
    return fields


def _lab_fields(url: str) -> dict[str, str]:
    return {
        "available_version": "0.8.0-alpha.320",
        "channel": "stable",
        "branch": "main",
        "source": "github:henriquefarisco/CapyOS",
        "published_at": "2026-07-28",
        "payload_url": url,
        "payload_size": "1024",
        "payload_sha256": "cc" * 32,
    }


def fail(reason: str) -> int:
    print(f"[FAIL] {reason}")
    return 1


class _SlotStatusSession:
    """Minimal console double for the shared post-apply lifecycle assertion."""

    def __init__(self, output: str, marker: int = 0) -> None:
        self.output = output
        self.marker_value = marker
        self.commands: list[str] = []
        self.waits: list[tuple[str, int, bool]] = []

    def marker(self) -> int:
        return self.marker_value

    def send_line(self, command: str) -> None:
        self.commands.append(command)

    def tail(self, max_bytes: int = 3500) -> str:
        return self.output[-max_bytes:]

    def wait_for(
        self,
        expected: str,
        *,
        timeout: float,
        start_at: int,
        ignore_line_breaks: bool = False,
    ) -> None:
        _ = timeout
        self.waits.append((expected, start_at, ignore_line_breaks))
        if not text_contains_pattern(
            self.output[start_at:],
            expected,
            ignore_line_breaks=ignore_line_breaks,
        ):
            raise TimeoutError(expected)


def main() -> int:  # noqa: PLR0911 - one early return per violated invariant
    # The runtime comparator ignores build metadata, so only the prerelease
    # number can make a manifest newer than the running system.
    if contract.next_prerelease_version("0.8.0-alpha.319+20260728") != "0.8.0-alpha.320":
        return fail("next_prerelease_version must bump the prerelease number")
    if contract.next_prerelease_version("1.2.3-rc.7") != "1.2.3-rc.8":
        return fail("next_prerelease_version must preserve the prerelease label")
    for rejected in ("0.8.0", "0.8.0+20260728", "0.8.0-dev.1", ""):
        try:
            contract.next_prerelease_version(rejected)
        except ValueError:
            continue
        return fail(f"next_prerelease_version accepted {rejected!r}")

    if contract.release_tag_from_version_yaml(
        "channels:\n  alpha:\n    current: 0.8.0-alpha.319\n    extended: 0.8.0-alpha.319+20260728\n"
    ) != "0.8.0-alpha.319+20260728":
        return fail("release tag must come from the extended alpha field")

    # The gate owns port 18083; 18080-18082 belong to the browser gates.
    if contract.LOCAL_HTTP_PORT != 18083:
        return fail("the signed A/B gate must keep port 18083")
    if contract.manifest_url() != "http://10.0.2.2:18083/latest.ini":
        return fail("QEMU manifest URL drifted from the SLIRP gateway form")
    if contract.payload_url("192.168.181.1", 18083) != (
        "http://192.168.181.1:18083/capyos64.bin"
    ):
        return fail("payload URL must honour a VMware NAT host address")

    # Attempt markers are the only cross-platform proof of which slot booted.
    if contract.attempt_marker(1, "pending") != "[boot] A/B attempt slot=1 state=pending":
        return fail("attempt marker drifted from the kernel boot line")
    try:
        contract.attempt_marker(0, "spent")
    except ValueError:
        pass
    else:
        return fail("attempt_marker accepted an unknown state")

    text = (
        "[boot] provider reason=ready\n"
        "[boot] A/B attempt slot=1 state=pending generation=0x5\n"
    )
    try:
        flow.require_boot_attempt(text, 1, "pending")
    except RuntimeError as exc:
        return fail(f"require_boot_attempt rejected a matching log: {exc}")
    for slot, state in ((0, "pending"), (1, "rollback"), (1, "confirmed")):
        try:
            flow.require_boot_attempt(text, slot, state)
        except RuntimeError:
            continue
        return fail(f"require_boot_attempt accepted slot={slot} state={state}")
    both = text + "[boot] A/B attempt slot=0 state=rollback generation=0x9\n"
    try:
        flow.require_boot_attempt(both, 1, "pending")
    except RuntimeError:
        pass
    else:
        return fail("require_boot_attempt accepted a log reporting two attempts")

    # update-apply arms the real lifecycle immediately: the candidate is
    # active, unhealthy until confirmed and protected by a pending rollback.
    expected_armed_state = (
        "state=active",
        "health=pending [ACTIVE]",
        "Rollback pending: yes",
    )
    if contract.ARMED_ATTEMPT_EXPECTATIONS != expected_armed_state:
        return fail("post-apply slot lifecycle literals drifted")
    plain_armed_status = (
        "Slot A: version=0.8.0-alpha.319 state=rollback boots=1 ok=1 fail=0 "
        "health=confirmed\n"
        "Slot B: version=0.8.0-alpha.320 state=active boots=0 ok=0 fail=0 "
        "health=pending [ACTIVE]\n"
        "Rollback pending: yes\n> "
    )
    wrapped_armed_status = (
        plain_armed_status.replace("state=active", "state=act\r\nive")
        .replace("health=pending [ACTIVE]", "health=pending \r\n[ACTIVE]")
        .replace("Rollback pending: yes", "Rollback pend\ning: yes")
    )
    stale_prefix = "Slot B: version=old state=valid health=confirmed [ACTIVE]\n> "
    marker = len(stale_prefix)
    session = _SlotStatusSession(stale_prefix + wrapped_armed_status, marker)
    try:
        flow.assert_armed_attempt_state(session, 1.0)
    except TimeoutError as exc:
        return fail(f"post-apply lifecycle rejected a matching snapshot: {exc}")
    if session.commands != ["print-boot-slot"]:
        return fail("post-apply lifecycle must use one print-boot-slot snapshot")
    if any(start_at != marker for _, start_at, _ in session.waits):
        return fail("post-apply lifecycle did not keep one console marker")
    invariant_waits = session.waits[: len(expected_armed_state)]
    if any(not ignore_line_breaks for _, _, ignore_line_breaks in invariant_waits):
        return fail("post-apply invariants must tolerate debugcon line wrapping")
    for missing in expected_armed_state:
        incomplete = plain_armed_status.replace(missing, "missing", 1)
        try:
            flow.assert_armed_attempt_state(_SlotStatusSession(incomplete), 1.0)
        except TimeoutError:
            continue
        return fail(f"post-apply lifecycle accepted a snapshot without {missing!r}")
    stale_valid = plain_armed_status.replace("state=active", "state=valid", 1)
    try:
        flow.assert_armed_attempt_state(_SlotStatusSession(stale_valid), 1.0)
    except TimeoutError:
        pass
    else:
        return fail("post-apply lifecycle accepted stale state=valid")

    wrapped_confirmed = (
        "Slot B: version=0.8.0-alpha.320 state=active health=\r\n"
        "confirmed [ACTIVE]\n> "
    )
    confirmed_session = _SlotStatusSession(wrapped_confirmed)
    try:
        flow.assert_slot_state(
            confirmed_session, 1.0, "health=confirmed [ACTIVE]"
        )
    except TimeoutError as exc:
        return fail(f"slot-state assertion rejected wrapped status: {exc}")
    if confirmed_session.commands != ["print-boot-slot"]:
        return fail("slot-state assertion did not request one status snapshot")
    if not confirmed_session.waits or not confirmed_session.waits[0][2]:
        return fail("slot-state assertion must tolerate debugcon line wrapping")

    for driver_name in (
        "smoke_x64_qemu_update_ab.py",
        "smoke_x64_vmware_update_ab.py",
    ):
        driver = (REPO_ROOT / "tools" / "scripts" / driver_name).read_text(
            encoding="utf-8"
        )
        if "assert_armed_attempt_state(" not in driver:
            return fail(f"{driver_name} does not use the shared lifecycle helper")
        if '"state=valid"' in driver:
            return fail(f"{driver_name} still asserts stale state=valid")

    # Production manifests must keep refusing plain http; only the lab build,
    # which swaps the trust anchor, accepts it.
    if payload_url_prefixes(False) != ("https://", "/system/update/"):
        return fail("production payload URL prefixes changed")
    if "http://" not in payload_url_prefixes(True):
        return fail("lab payload URL prefixes must add http://")
    try:
        validate_fields(_lab_fields("http://10.0.2.2:18083/capyos64.bin"), False)
    except ManifestError:
        pass
    else:
        return fail("production validation accepted a plain-http payload URL")
    try:
        canonical_body(
            _lab_fields("http://10.0.2.2:18083/capyos64.bin"), allow_lab_http=True
        )
    except ManifestError as exc:
        return fail(f"lab validation rejected the gate payload URL: {exc}")
    for malformed in ("http://", "http://host/../x", "http://ho st/x"):
        try:
            validate_fields(_lab_fields(malformed), False, allow_lab_http=True)
        except ManifestError:
            continue
        return fail(f"lab validation accepted a malformed URL: {malformed!r}")

    # Evidence manifest: canonical order, positive invariants, no recovery key.
    evidence = _evidence()
    rendered = contract.render_evidence(evidence)
    if contract.parse_evidence(rendered) != evidence:
        return fail("evidence must survive a render/parse round trip")
    try:
        contract.validate_evidence(evidence)
    except ValueError as exc:
        return fail(f"validate_evidence rejected a passing run: {exc}")
    try:
        contract.render_evidence({"format": contract.EVIDENCE_FORMAT})
    except ValueError:
        pass
    else:
        return fail("render_evidence accepted a non-canonical field set")
    for key, bad in (
        ("health_confirmed", "no"),
        ("loader_applied_rollback", "no"),
        ("rollback_reported", "partial"),
        ("recovery_key_included", "yes"),
        ("trust_anchor", "production-ed25519"),
        ("provider", "virtualbox"),
        ("track", "BIOS/MBR/x86"),
        ("boots_observed", "3"),
        ("payload_sha256", "not-hex"),
        ("second_attempt_slot", "1"),
    ):
        try:
            contract.validate_evidence(_evidence(**{key: bad}))
        except ValueError:
            continue
        return fail(f"validate_evidence accepted {key}={bad!r}")

    leaked = _evidence(manifest_version="ABCD-EFGH-IJKL-MNOP-QRST-UVWX")
    try:
        contract.validate_evidence(leaked)
    except ValueError:
        pass
    else:
        return fail("validate_evidence accepted evidence carrying a recovery key")

    # The official VMX must stay UEFI + E1000 with the kernel owning COM1.
    vmx = contract.render_update_ab_vmx(
        display_name="CapyOS signed A/B test",
        iso_path=Path("C:/build/CapyOS-Installer-UEFI.iso"),
        target_descriptor=Path("C:/build/ci/target.vmdk"),
        pipe_name="capyos-update-ab-test",
        boot_from="hdd",
    )
    for required in (
        'firmware = "efi"',
        'ethernet0.virtualDev = "e1000"',
        'ethernet0.connectionType = "nat"',
        'efi.serialConsole.enabled = "FALSE"',
        'bios.bootOrder = "hdd"',
        'serial0.fileName = "\\\\.\\pipe\\capyos-update-ab-test"',
    ):
        if required not in vmx:
            return fail(f"official VMX lost {required!r}")
    if 'e1000e' in vmx:
        return fail("official VMX must use e1000, not e1000e")
    for bad_call in (
        {"boot_from": "net"},
        {"pipe_name": 'evil" \nx'},
    ):
        kwargs = {
            "display_name": "x",
            "iso_path": Path("C:/a.iso"),
            "target_descriptor": Path("C:/b.vmdk"),
            "pipe_name": "p",
            "boot_from": "hdd",
        }
        kwargs.update(bad_call)
        try:
            contract.render_update_ab_vmx(**kwargs)
        except ValueError:
            continue
        return fail(f"render_update_ab_vmx accepted {bad_call}")

    print("[OK] signed A/B update gate contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
