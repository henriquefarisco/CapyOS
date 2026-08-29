#!/usr/bin/env python3
"""Host contract test for the Etapa 8 signed A/B update gate.

Plain script (no pytest), matching tools/scripts/test_installer_smoke_contract.py:
prints the first violation and returns 1. Wired into `make update-ab-selftest`,
which `release-check` runs, so the gate's frozen literals and the lab-only
payload-URL relaxation cannot drift without a red gate.
"""

from __future__ import annotations

import ast
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

import smoke_x64_update_ab_contract as contract  # noqa: E402
import smoke_x64_update_ab_flow as flow  # noqa: E402
import smoke_x64_helpers as helpers  # noqa: E402
import smoke_x64_vmware_update_ab as vmware_update_ab  # noqa: E402
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


def _production_evidence(**overrides: str) -> dict[str, str]:
    fields = {
        "format": contract.PRODUCTION_EVIDENCE_FORMAT,
        "release_tag": "v0.9.2+20260826",
        "track": contract.TRACK,
        "provider": "vmware-workstation",
        "trust_anchor": contract.PRODUCTION_TRUST_ANCHOR,
        "trust_public_key": "aa" * 32,
        "predecessor_version": "0.9.1+20260825",
        "manifest_version": "0.9.2+20260826",
        "manifest_url": (
            "https://github.com/henriquefarisco/CapyOS/"
            "releases/latest/download/latest.ini"
        ),
        "payload_url": (
            "https://github.com/henriquefarisco/CapyOS/releases/download/"
            "v0.9.2+20260826/capyos64.bin"
        ),
        "payload_size": "2863536",
        "payload_sha256": "bb" * 32,
        "boot_media_sha256": "cc" * 32,
        "first_attempt_slot": "1",
        "second_attempt_slot": "1",
        "boots_observed": "4",
        "cycle_order": contract.PRODUCTION_CYCLE_ORDER,
        "bootstrap_vmnet": "VMnet8",
        "bootstrap_network_mode": "static",
        "bootstrap_ipv4": "192.168.87.15",
        "bootstrap_mask": "255.255.255.0",
        "bootstrap_gateway": "192.168.87.2",
        "bootstrap_dns": "192.168.87.2",
        "lab_override_absent": "yes",
        "public_latest_route": "yes",
        "bootstrap_network_persisted": "yes",
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
        self.wait_any: list[tuple[tuple[str, ...], int]] = []

    def marker(self) -> int:
        return self.marker_value

    def send_line(self, command: str) -> None:
        self.commands.append(command)

    def tail(self, max_bytes: int = 3500) -> str:
        return self.output[-max_bytes:]

    def text_since(self, start_at: int) -> str:
        return self.output[start_at:]

    def text(self) -> str:
        return self.output

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

    def wait_for_any(
        self,
        patterns,
        *,
        timeout: float,
        start_at: int,
    ) -> str:
        _ = timeout
        choices = tuple(patterns)
        self.wait_any.append((choices, start_at))
        for pattern in choices:
            if text_contains_pattern(self.output[start_at:], pattern):
                return pattern
        raise TimeoutError(choices)


class _CommandSequenceSession(_SlotStatusSession):
    """Expose distinct console spans to consecutive command markers."""

    def __init__(self, *outputs: str) -> None:
        super().__init__("".join(outputs))
        self.offsets = [0]
        for output in outputs:
            self.offsets.append(self.offsets[-1] + len(output))

    def marker(self) -> int:
        return self.offsets[min(len(self.commands), len(self.offsets) - 1)]

    def _span(self, start_at: int) -> str:
        try:
            index = self.offsets.index(start_at)
        except ValueError as exc:
            raise AssertionError(f"unknown sequence marker {start_at}") from exc
        end = self.offsets[min(index + 1, len(self.offsets) - 1)]
        return self.output[start_at:end]

    def text_since(self, start_at: int) -> str:
        return self._span(start_at)

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
            self._span(start_at),
            expected,
            ignore_line_breaks=ignore_line_breaks,
        ):
            raise TimeoutError(expected)

    def wait_for_any(
        self,
        patterns,
        *,
        timeout: float,
        start_at: int,
    ) -> str:
        _ = timeout
        choices = tuple(patterns)
        self.wait_any.append((choices, start_at))
        span = self._span(start_at)
        for pattern in choices:
            if text_contains_pattern(span, pattern):
                return pattern
        raise TimeoutError(choices)


class _LaggingDebugPromptSession(_SlotStatusSession):
    """Model a stale prompt arriving late on QEMU debugcon."""

    def __init__(self) -> None:
        super().__init__(">~> ")
        self.primary_reads = 0

    def serial_text_since(self, start_at: int) -> str:
        _ = start_at
        self.primary_reads += 1
        if self.primary_reads == 1:
            return "print-boot-slot"
        return (
            "print-boot-slot\r\n"
            "Boot provider: ready=yes reason=ready\r\n"
            "admin@smoke-node>~> "
        )


class _HttpProgressSession(_SlotStatusSession):
    """Model an optional fetch whose progress prefix contains ``> ``."""

    def __init__(self) -> None:
        super().__init__("")
        self.primary_reads = 0

    def serial_text_since(self, start_at: int) -> str:
        _ = start_at
        self.primary_reads += 1
        progress = "net-fetch https://example.com\r\n>>> https://example.com (?) ...\r\n"
        if self.primary_reads == 1:
            return progress
        return progress + "[erro] dns resolution failed\r\nadmin@smoke-node>~> "


class _LateLabBannerSession(_SlotStatusSession):
    """Expose a lab trust banner only after the runtime identity query."""

    def text(self) -> str:
        if self.commands:
            return f"{self.output}\r\n{contract.LAB_BANNER}"
        return self.output


def main() -> int:  # noqa: PLR0911 - one early return per violated invariant
    lagging_debug_prompt = _LaggingDebugPromptSession()
    try:
        helpers.run_cmd(
            lagging_debug_prompt,
            "print-boot-slot",
            timeout=1.0,
            expect="Boot provider: ready=yes reason=ready",
        )
    except RuntimeError as exc:
        return fail(f"run_cmd accepted a stale debugcon prompt: {exc}")

    http_progress = _HttpProgressSession()
    helpers.run_cmd(
        http_progress,
        "net-fetch https://example.com",
        timeout=1.0,
        expect="status=200",
        expect_optional=True,
    )
    if http_progress.primary_reads < 2:
        return fail("run_cmd accepted the HTTP >>> progress prefix as a shell prompt")

    cwd_prompt = _SlotStatusSession("admin@smoke-node>~/.../projetos/capy> ")
    helpers.run_cmd(cwd_prompt, "mypath", timeout=1.0)

    failed_command = _SlotStatusSession(
        "payload download failed\r\nadmin@smoke-node>~> "
    )
    try:
        helpers.run_cmd(
            failed_command,
            "update-download-payload",
            timeout=60.0,
            expect=flow.DOWNLOAD_OK,
        )
    except RuntimeError:
        pass
    else:
        return fail("run_cmd waited past a completed command failure")

    successful_command = _SlotStatusSession(
        f"{flow.DOWNLOAD_OK}\r\nadmin@smoke-node>~> "
    )
    helpers.run_cmd(
        successful_command,
        "update-download-payload",
        timeout=60.0,
        expect=flow.DOWNLOAD_OK,
    )

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

    if contract.next_lab_update_version("0.9.2+20260826") != "0.9.3":
        return fail("stable lab update version must increment the patch")
    if contract.next_lab_update_version("0.8.0-alpha.321+20260821") != (
        "0.8.0-alpha.322"
    ):
        return fail("prerelease lab update version must increment its suffix")
    for rejected in ("0.9", "0.9.2-dev.1", "0.9.4294967295", ""):
        try:
            contract.next_lab_update_version(rejected)
        except ValueError:
            continue
        return fail(f"next_lab_update_version accepted {rejected!r}")

    version_cases = (
        ("0.9.2+20260826", "0.9.1+20260825", 1),
        ("0.9.1+20260826", "0.9.1+20260825", 0),
        ("0.9.1", "0.9.1-rc.9", 1),
        ("0.9.1-rc.1", "0.9.1-beta.99", 1),
        ("0.9.1-alpha.1", "0.9.1-beta.1", -1),
    )
    for candidate, current, expected in version_cases:
        actual = contract.compare_update_versions(candidate, current)
        if actual != expected:
            return fail(
                f"runtime version comparison {candidate!r}/{current!r}: {actual}"
            )
    for malformed in ("0.9", "0.9.1-dev.1", "4294967296.0.0", ""):
        try:
            contract.compare_update_versions(malformed, "0.9.1")
        except ValueError:
            continue
        return fail(f"runtime version comparison accepted {malformed!r}")

    runtime_marker_cases = (
        ("0.9.1+20260825", "CapyOS 0.9.1 [stable]"),
        ("v1.2.3", "CapyOS 1.2.3 [stable]"),
    )
    for version, expected in runtime_marker_cases:
        actual = contract.stable_runtime_identity_marker(version)
        if actual != expected:
            return fail(f"stable runtime marker {version!r}: {actual!r}")
    for rejected in ("0.9.2-rc.1", "0.9", ""):
        try:
            contract.stable_runtime_identity_marker(rejected)
        except ValueError:
            continue
        return fail(f"stable runtime marker accepted {rejected!r}")

    runtime_marker = "CapyOS 0.9.1 [stable]"
    runtime_console = _SlotStatusSession(
        f"{runtime_marker}\r\nadmin@smoke-node>~> "
    )
    vmware_update_ab.assert_production_runtime(
        runtime_console, timeout=1.0, expected_version="0.9.1+20260825"
    )
    if runtime_console.commands != ["print-version"]:
        return fail("VMware production runtime identity did not query print-version")
    if runtime_console.waits != [(runtime_marker, 0, True)]:
        return fail("VMware production runtime identity lost its exact marker assertion")

    early_lab_console = _SlotStatusSession(
        f"{contract.LAB_BANNER}\r\n{runtime_marker}\r\nadmin@smoke-node>~> "
    )
    try:
        vmware_update_ab.assert_production_runtime(
            early_lab_console, timeout=1.0, expected_version="0.9.1+20260825"
        )
    except RuntimeError as exc:
        if "lab trust override banner" not in str(exc):
            return fail(f"unexpected pre-query lab-banner failure: {exc}")
    else:
        return fail("VMware production runtime accepted a pre-query lab banner")
    if early_lab_console.commands:
        return fail("VMware production runtime queried a lab-trust boot")

    wrong_runtime_console = _SlotStatusSession(
        "CapyOS 0.9.2 [stable]\r\nadmin@smoke-node>~> "
    )
    try:
        vmware_update_ab.assert_production_runtime(
            wrong_runtime_console,
            timeout=1.0,
            expected_version="0.9.1+20260825",
        )
    except (RuntimeError, TimeoutError):
        pass
    else:
        return fail("VMware production runtime accepted a different version")
    if wrong_runtime_console.commands != ["print-version"]:
        return fail("VMware production runtime did not query the mismatched version")

    late_lab_console = _LateLabBannerSession(
        f"{runtime_marker}\r\nadmin@smoke-node>~> "
    )
    try:
        vmware_update_ab.assert_production_runtime(
            late_lab_console, timeout=1.0, expected_version="0.9.1+20260825"
        )
    except RuntimeError as exc:
        if "lab trust override banner" not in str(exc):
            return fail(f"unexpected post-query lab-banner failure: {exc}")
    else:
        return fail("VMware production runtime accepted a post-query lab banner")
    if late_lab_console.commands != ["print-version"]:
        return fail("VMware production runtime did not complete the guarded query")

    vmware_update_ab.assert_boot_reported_vmx_mac(
        "boot prefix 00:0C:29:\r\n70:F0:CF boot suffix",
        "00:0C:29:70:F0:CF",
    )
    try:
        vmware_update_ab.assert_boot_reported_vmx_mac(
            "boot prefix 00:0C:29:70:F0:D0 boot suffix",
            "00:0C:29:70:F0:CF",
        )
    except RuntimeError:
        pass
    else:
        return fail("VMware production gate accepted a different guest MAC")

    nat_config = """\
[host]
ip = 192.168.87.2/24
device = vmnet8

[dns]
autodetect = 1
"""
    dhcp_config = """\
subnet 192.168.87.0 netmask 255.255.255.0 {
range 192.168.87.128 192.168.87.254;
option domain-name-servers 192.168.87.2;
option routers 192.168.87.2;
}
"""
    production_network = vmware_update_ab.production_network_from_vmware_configs(
        nat_config, dhcp_config, "VMnet8", "192.168.87.1"
    )
    expected_network = {
        "address": "192.168.87.15",
        "mask": "255.255.255.0",
        "gateway": "192.168.87.2",
        "dns": "192.168.87.2",
    }
    if production_network != expected_network:
        return fail(f"VMware production network discovery: {production_network!r}")
    try:
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config.replace("device = vmnet8", "device = vmnet1"),
            dhcp_config,
            "VMnet8",
            "192.168.87.1",
        )
    except ValueError:
        pass
    else:
        return fail("VMware production network accepted a different vmnet")
    try:
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config + "\n[host]\nip = 192.168.87.3/24\ndevice = vmnet8\n",
            dhcp_config,
            "VMnet8",
            "192.168.87.1",
        )
    except ValueError:
        pass
    else:
        return fail("VMware production network accepted duplicate NAT host config")
    try:
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config,
            dhcp_config.replace(
                "option routers 192.168.87.2;",
                "option routers 192.168.87.3;",
            ),
            "VMnet8",
            "192.168.87.1",
        )
    except ValueError:
        pass
    else:
        return fail("VMware production network accepted a mismatched DHCP router")
    try:
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config.replace("192.168.87.2/24", "10.0.0.2/8"),
            dhcp_config,
            "VMnet8",
            "10.0.0.1",
        )
    except ValueError:
        pass
    else:
        return fail("VMware production network accepted an oversized subnet")

    collision_dhcp = dhcp_config.replace(
        "range 192.168.87.128 192.168.87.254;",
        "range 192.168.87.128 192.168.87.254;\n"
        "range 192.168.87.15 192.168.87.20;",
    ) + """\
host reserved-static {
fixed-address 192.168.87.3;
}
"""
    collision_safe_network = (
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config, collision_dhcp, "VMnet8", "192.168.87.1"
        )
    )
    if collision_safe_network["address"] != "192.168.87.4":
        return fail(
            "VMware production network ignored an additional DHCP range "
            "or fixed-address reservation"
        )
    try:
        vmware_update_ab.production_network_from_vmware_configs(
            nat_config,
            dhcp_config.replace(
                "range 192.168.87.128 192.168.87.254;",
                "range 192.168.87.1 192.168.87.254;",
            ),
            "VMnet8",
            "192.168.87.1",
        )
    except ValueError:
        pass
    else:
        return fail("VMware production network selected inside the DHCP range")

    net_set_output = (
        "[ok] network configuration applied\r\n"
        "driver=e1000 mode=static detected=yes runtime=ready ready=yes\r\n"
        "ipv4=192.168.87.15 mask=255.255.255.0 "
        "gw=192.168.87.2 dns=192.168.87.2\r\n"
        "admin@smoke-node>~> "
    )
    persisted_config_output = (
        "network_mode=static\r\n"
        "ipv4=192.168.87.15\r\n"
        "mask=255.255.255.0\r\n"
        "gateway=192.168.87.2\r\n"
        "dns=192.168.87.2\r\n"
        "admin@smoke-node>~> "
    )
    runtime_network_output = (
        "driver=e1000 mode=sta\r\ntic detected=yes runtime=ready ready=yes\r\n"
        "dhcp=off attempts=2 last_error=-3\r\n"
        "ipv4=192.168.87.15 mask=255.255.255.0 "
        "gw=192.16\r\n8.87.2 dns=192.168.87.2\r\n"
        "arp_entries=0 tx=0 rx=0 drop=0\r\n"
        "admin@smoke-node>~> "
    )
    network_console = _CommandSequenceSession(
        net_set_output, runtime_network_output, persisted_config_output
    )
    vmware_update_ab.configure_production_vmware_network(
        network_console, 1.0, production_network
    )
    if network_console.commands != [
        "net-set 192.168.87.15 255.255.255.0 192.168.87.2 192.168.87.2",
        "net-status",
        "print-file /system/config.ini",
    ]:
        return fail("VMware production network did not apply the discovered profile")
    candidate_runtime_console = _CommandSequenceSession(
        runtime_network_output.replace(
            "dhcp=off attempts=2 last_error=-3",
            "mac=00:0C:29:70:F0:CF\r\ndhcp=off attempts=0",
        ),
        persisted_config_output,
    )
    vmware_update_ab.assert_production_vmware_network_persisted(
        candidate_runtime_console, 1.0, production_network
    )
    failed_persistence_console = _CommandSequenceSession(
        net_set_output.replace(
            "driver=e1000",
            "Warning: could not save to /system/config.ini.\r\ndriver=e1000",
        ),
        runtime_network_output,
        persisted_config_output,
    )
    try:
        vmware_update_ab.configure_production_vmware_network(
            failed_persistence_console, 1.0, production_network
        )
    except RuntimeError:
        pass
    else:
        return fail("VMware production network accepted a failed persistent save")
    prefix_mismatch_console = _CommandSequenceSession(
        runtime_network_output,
        persisted_config_output.replace("192.168.87.15", "192.168.87.150")
    )
    try:
        vmware_update_ab.assert_production_vmware_network_persisted(
            prefix_mismatch_console, 1.0, production_network
        )
    except RuntimeError:
        pass
    else:
        return fail("VMware production network accepted a prefix-only config match")
    duplicate_config_console = _CommandSequenceSession(
        runtime_network_output,
        persisted_config_output.replace(
            "ipv4=192.168.87.15\r\n",
            "ipv4=192.168.87.15\r\nipv4=192.168.87.15\r\n",
        )
    )
    try:
        vmware_update_ab.assert_production_vmware_network_persisted(
            duplicate_config_console, 1.0, production_network
        )
    except RuntimeError:
        pass
    else:
        return fail("VMware production network accepted duplicate persisted keys")
    runtime_mismatch_console = _CommandSequenceSession(
        runtime_network_output.replace("192.168.87.15", "192.168.87.150"),
        persisted_config_output,
    )
    try:
        vmware_update_ab.assert_production_vmware_network_persisted(
            runtime_mismatch_console, 1.0, production_network
        )
    except (RuntimeError, TimeoutError):
        pass
    else:
        return fail("VMware production network accepted a different runtime IPv4")

    version_yaml = (
        "channels:\n"
        "  alpha:\n"
        "    extended: 0.8.0-alpha.321+20260821\n"
        "  stable:\n"
        "    current: 0.9.2\n"
        "    extended: 0.9.2+20260826\n"
    )
    if contract.channel_extended_from_version_yaml(version_yaml, "stable") != (
        "0.9.2+20260826"
    ):
        return fail("stable version lookup crossed a VERSION.yaml channel boundary")

    if contract.release_tag_from_version_yaml(
        "channels:\n  alpha:\n    current: 0.8.0-alpha.319\n    extended: 0.8.0-alpha.319+20260728\n"
    ) != "0.8.0-alpha.319+20260728":
        return fail("release tag must come from the extended alpha field")
    if contract.release_tag_from_version_yaml(
        version_yaml, "0.9.2+20260826"
    ) != "0.9.2+20260826":
        return fail("release tag must bind to the stable runtime under test")
    try:
        contract.release_tag_from_version_yaml(version_yaml, "9.9.9")
    except ValueError:
        pass
    else:
        return fail("release tag accepted a runtime absent from VERSION.yaml")

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
        "Rollback pending: yes\nadmin@smoke-node>~> "
    )
    wrapped_armed_status = (
        plain_armed_status.replace("state=active", "state=act\r\nive")
        .replace("health=pending [ACTIVE]", "health=pending \r\n[ACTIVE]")
        .replace("Rollback pending: yes", "Rollback pend\ning: yes")
    )
    stale_prefix = (
        "Slot B: version=old state=valid health=confirmed [ACTIVE]\n"
        "admin@smoke-node>~> "
    )
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
        "confirmed [ACTIVE]\nadmin@smoke-node>~> "
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

    wrapped_rollback = (
        f"{contract.ROLLBACK_OK}\n"
        "boot rolled back to the confirmed slot; staged update di\r\n"
        "sarmed\nadmin@smoke-node>~> "
    )
    rollback_session = _SlotStatusSession(wrapped_rollback)
    try:
        flow.assert_rollback_reported(rollback_session, 1.0)
    except TimeoutError as exc:
        return fail(f"rollback assertion rejected wrapped status: {exc}")
    if not rollback_session.waits or not rollback_session.waits[0][2]:
        return fail("rollback summary assertion must tolerate console wrapping")

    bound_payload_url = (
        "https://github.com/henriquefarisco/CapyOS/releases/download/"
        "v0.9.2+20260826/capyos64.bin"
    )
    bound_payload_sha256 = "ab" * 32
    bound_output = (
        f"{contract.FETCH_OK}\n"
        "current=0.9.1+20260825 available=0.9.2+20260826\n"
        f"payload={bound_payload_url}\n"
        f"{contract.DOWNLOAD_OK}\n"
        f"payload-cache=/system/update/payload.bin sha256={bound_payload_sha256}\n"
        f"{contract.PREPARE_EXPLAIN_CLEAN}\n"
        f"{contract.PREPARE_OK}\n"
        f"{contract.APPLY_OK}\n"
        f"{contract.APPLY_SUMMARY}\n"
        "configured=yes rc=0\nadmin@smoke-node>~> "
    )
    bound_session = _SlotStatusSession(bound_output)
    try:
        flow.stage_and_arm_update(
            bound_session,
            1.0,
            expect_version="0.9.2+20260826",
            expect_payload_url=bound_payload_url,
            expect_payload_sha256=bound_payload_sha256,
        )
    except TimeoutError as exc:
        return fail(f"material-bound update flow rejected matching status: {exc}")
    if bound_session.commands.count("update-status") != 5:
        return fail("material-bound update flow skipped a catalog/cache assertion")
    for expected in (
        f"payload={bound_payload_url}",
        f"sha256={bound_payload_sha256}",
    ):
        matching_waits = [wait for wait in bound_session.waits if wait[0] == expected]
        if not matching_waits or not matching_waits[0][2]:
            return fail(f"material binding lost wrapped-console match for {expected!r}")

    refusal_output = (
        f"{contract.MANIFEST_NOT_NEWER_SUMMARY}\n"
        "configured=yes rc=-20\n"
        "current=0.9.2+20260826 available=-\nadmin@smoke-node>~> "
    )
    refusal_session = _SlotStatusSession(refusal_output)
    try:
        flow.assert_equal_release_refused(
            refusal_session,
            1.0,
            current_version="0.9.2+20260826",
        )
    except TimeoutError as exc:
        return fail(f"equal-release refusal rejected matching status: {exc}")
    if refusal_session.commands != ["update-fetch", "update-status", "update-status"]:
        return fail("equal-release refusal did not prove fetch result and status")

    endpoint_session = _SlotStatusSession(
        ">>> https://192.168.87.1 (?) ...\n"
        "status=200 host=192.168.87.1\nadmin@smoke-node>~> "
    )
    try:
        flow.assert_http_endpoint_reachable(
            endpoint_session, 240.0, "http://192.168.87.1:18083/latest.ini"
        )
    except RuntimeError as exc:
        return fail(f"lab endpoint probe rejected HTTP 200: {exc}")
    retry_endpoint = _CommandSequenceSession(
        "[erro] connection failed\ndiag: arp=3 syn-out=6 syn-ack=6\n"
        "admin@smoke-node>~> ",
        "status=200 host=github.com port=443\nadmin@smoke-node>~> ",
    )
    try:
        flow.assert_http_endpoint_reachable(
            retry_endpoint,
            240.0,
            "https://github.com/example/latest.ini",
            attempts=2,
        )
    except RuntimeError as exc:
        return fail(f"production endpoint probe rejected a bounded retry: {exc}")
    if retry_endpoint.commands != [
        "net-fetch https://github.com/example/latest.ini",
        "net-fetch https://github.com/example/latest.ini",
    ]:
        return fail("production endpoint preflight did not perform exactly two probes")
    if endpoint_session.commands != [
        "net-fetch http://192.168.87.1:18083/latest.ini"
    ]:
        return fail("lab endpoint probe did not fetch the configured URL exactly")
    failing_endpoint = _SlotStatusSession(
        "[erro] tcp connect timeout\ndiag: arp=1 syn-out=2 syn-ack=0\n"
        "admin@smoke-node>~> "
    )
    try:
        flow.assert_http_endpoint_reachable(
            failing_endpoint, 240.0, "http://192.168.87.1:18083/latest.ini"
        )
    except RuntimeError as exc:
        if "syn-ack=0" not in str(exc):
            return fail("lab endpoint failure omitted the TCP diagnostic")
    else:
        return fail("lab endpoint probe accepted a failed TCP diagnostic")

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
        if driver_name == "smoke_x64_vmware_update_ab.py":
            if 'target_descriptor = run_root / "target.vmdk"' not in driver:
                return fail("VMware A/B gate lost the VMDK descriptor path")
            if "target = create_vmdk(" in driver:
                return fail("VMware A/B gate may pass the flat VMDK as a descriptor")
            if "assert_guest_uses_vmx_mac(" not in driver:
                return fail("VMware A/B gate lost the guest/VMX MAC binding assertion")
            if "assert_boot_reported_vmx_mac(console.text(), expected_mac)" not in driver:
                return fail("VMware A/B gate no longer proves the guest station MAC")
            if 'expect="driver=e1000 mode=dhcp detected=yes runtime=ready ready=yes"' not in driver:
                return fail("VMware A/B gate no longer proves the E1000 runtime")
            if driver.count("configure_production_vmware_network(") != 2:
                return fail("VMware production gate no longer applies its NAT profile")
            if driver.count("assert_http_endpoint_reachable(") != 2:
                return fail("VMware production gate lost the public-route preflight")
            if "endpoint, attempts=3" not in driver:
                return fail("VMware production gate lost its bounded HTTPS retry")
            if '"net-resolve github.com"' not in driver:
                return fail("VMware production gate lost its DNS warm-up proof")
            if driver.count("verify_production_public_route(") != 4:
                return fail(
                    "VMware production gate must prove the public route "
                    "before all three remote update phases"
                )
            driver_tree = ast.parse(driver, filename=driver_name)
            main_nodes = [
                node
                for node in driver_tree.body
                if isinstance(node, ast.FunctionDef) and node.name == "main"
            ]
            runtime_calls = []
            if len(main_nodes) == 1:
                runtime_calls = [
                    node
                    for node in ast.walk(main_nodes[0])
                    if isinstance(node, ast.Call)
                    and isinstance(node.func, ast.Name)
                    and node.func.id == "assert_production_runtime"
                ]
            if len(runtime_calls) != 4:
                return fail("VMware production gate must bind all four boots to runtime")
            if '"print-version"' not in driver:
                return fail("VMware production gate no longer queries runtime identity")
            if '[boot] Build:' in driver:
                return fail("VMware production gate requires an unobservable boot marker")

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

    production_evidence = _production_evidence()
    try:
        contract.validate_production_evidence(production_evidence)
    except ValueError as exc:
        return fail(f"production evidence rejected a passing run: {exc}")
    if contract.render_production_evidence(production_evidence).count("\n") != len(
        production_evidence
    ):
        return fail("production evidence did not render every canonical field")
    for key, bad in (
        ("lab_override_absent", "no"),
        ("public_latest_route", "no"),
        ("equal_release_refused", "no"),
        ("trust_anchor", "lab-ed25519"),
        ("provider", "qemu-ovmf"),
        ("cycle_order", "confirm-then-rollback"),
        ("predecessor_version", "0.9.2+20260826"),
        ("release_tag", "v0.9.1+20260825"),
        ("manifest_url", "http://10.0.2.2/latest.ini"),
        ("payload_url", "https://example.test/capyos64.bin"),
        ("boot_media_sha256", "not-hex"),
        ("recovery_key_included", "yes"),
    ):
        try:
            contract.validate_production_evidence(
                _production_evidence(**{key: bad})
            )
        except ValueError:
            continue
        return fail(f"production evidence accepted {key}={bad!r}")
    for bad_gateway in ("192.168.87.0", "192.168.87.15", "192.168.87.255"):
        try:
            contract.validate_production_evidence(
                _production_evidence(bootstrap_gateway=bad_gateway)
            )
        except ValueError:
            continue
        return fail(
            f"production evidence accepted unusable gateway={bad_gateway!r}"
        )
    for slot_override in (
        {"first_attempt_slot": "0"},
        {"second_attempt_slot": "0"},
    ):
        try:
            contract.validate_production_evidence(
                _production_evidence(**slot_override)
            )
        except ValueError:
            continue
        return fail(f"production evidence accepted incoherent slots={slot_override!r}")
    for bad_dns in ("127.0.0.1", "192.168.87.255", "255.255.255.255"):
        try:
            contract.validate_production_evidence(
                _production_evidence(bootstrap_dns=bad_dns)
            )
        except ValueError:
            continue
        return fail(f"production evidence accepted unusable DNS={bad_dns!r}")

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
