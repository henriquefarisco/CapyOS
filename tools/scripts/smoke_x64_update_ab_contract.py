#!/usr/bin/env python3
"""Pure contract for the Etapa 8 signed A/B update gate.

The gate proves the whole persistent update lifecycle on the official track:
a signed manifest is fetched over HTTP, its payload is downloaded and verified,
written to the inactive boot slot and armed for exactly one attempt; the UEFI
loader consumes that attempt; the running boot confirms health; and a second
cycle left unconfirmed is rolled back by the loader and reported by the updater.

Everything here is pure (no QEMU, no VMware, no sockets) so both drivers and the
host contract test share one source of truth for the runtime literals, the lab
key material and the evidence manifest. Runtime strings are asserted verbatim:
if a shell message changes, this file must change with it.

Trust anchor: the production update key is offline-only, so a gate run generates
a throwaway Ed25519 keypair and the kernel is built with
CAPYOS_UPDATE_LAB_TRUST_KEY_HEX. That build also accepts a plain-http payload URL
(the kernel TLS stack always verifies the peer, so a hermetic host server cannot
serve https) and is refused by `iso-uefi` outside a smoke target.
"""

from __future__ import annotations

import ipaddress
import re
import subprocess
from collections.abc import Mapping
from pathlib import Path

from smoke_x64_vmware_installer_contract import (
    contains_recovery_key,
    sanitize_public_text,
)
from update_manifest_common import ManifestError, raw_public_from_private

# Ports 18080-18082 are already claimed by the capybrowse / multifetch /
# capygfx-image gates; this gate owns 18083.
LOCAL_HTTP_PORT = 18083
LAB_MANIFEST_NAME = "latest.ini"
LAB_PAYLOAD_NAME = "capyos64.bin"

# QEMU user-net always maps the host to this gateway; VMware NAT uses the
# host's own VMnet address, resolved per lab by update_ab_lab_config.py.
QEMU_SLIRP_GATEWAY = "10.0.2.2"

EVIDENCE_FORMAT = "capyos-signed-ab-update-evidence-manifest-v1"
PRODUCTION_EVIDENCE_FORMAT = (
    "capyos-production-signed-ab-update-evidence-manifest-v2"
)
TRACK = "UEFI/GPT/x86_64"
PROVIDERS = ("qemu-ovmf", "vmware-workstation")
TRUST_ANCHOR = "lab-ed25519"
PRODUCTION_TRUST_ANCHOR = "production-ed25519"
PRODUCTION_CYCLE_ORDER = "rollback-then-confirm"

# ---- runtime literals (English locale) -------------------------------------
PROVIDER_READY_LINE = "Boot provider: ready=yes reason=ready"
LAB_BANNER = "[lab] update trust anchor overridden"
FETCH_OK = "[ok] remote manifest accepted into the local catalog"
DOWNLOAD_OK = "[ok] update payload downloaded and verified"
PREPARE_EXPLAIN_OK = "[ok] prepare preflight passed"
PREPARE_EXPLAIN_CLEAN = "failing=-"
PREPARE_OK = "[ok] verified update prepared and armed for activation"
APPLY_OK = "[ok] staged update verified and boot slot armed"
APPLY_SUMMARY = "inactive slot written and armed for one boot attempt"
ARMED_ATTEMPT_EXPECTATIONS = (
    "state=active",
    "health=pending [ACTIVE]",
    "Rollback pending: yes",
)
CONFIRM_OK = "[ok] boot health confirmed"
CONFIRM_SUMMARY = "persistent boot health confirmed"
ATTEMPT_PENDING_SUMMARY = "boot attempt pending confirmation; rollback still armed"
ROLLBACK_OK = "[ok] boot rollback completed"
ROLLBACK_SUMMARY = "boot rolled back to the confirmed slot; staged update disarmed"
NO_ROLLBACK_SUMMARY = "no boot rollback pending"
MANIFEST_NOT_NEWER_SUMMARY = "imported manifest not newer than current system"

_PRERELEASE_RE = re.compile(
    r"^(?P<base>\d+\.\d+\.\d+)-(?P<label>alpha|beta|rc)\.(?P<number>\d+)"
    r"(?:\+[0-9A-Za-z.-]+)?$"
)
_RUNTIME_VERSION_RE = re.compile(
    r"^[vV]?(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
    r"(?:-(?P<label>alpha|beta|rc)(?:\.(?P<number>\d+))?)?"
    r"(?:\+[0-9A-Za-z.-]+)?$"
)

_REQUIRED_YES = (
    "provider_ready",
    "signed_manifest_accepted",
    "payload_verified",
    "inactive_slot_written",
    "attempt_armed",
    "loader_consumed_attempt",
    "health_confirmed",
    "second_attempt_armed",
    "unconfirmed_attempt_spent",
    "loader_applied_rollback",
    "rollback_reported",
    "confirmed_slot_restored",
)

# Order is part of the contract: render_evidence refuses any other ordering so a
# published manifest is byte-comparable across runs and providers.
_REQUIRED_FIELDS = (
    "format",
    "release_tag",
    "track",
    "provider",
    "trust_anchor",
    "lab_public_key",
    "manifest_version",
    "manifest_url",
    "payload_url",
    "payload_size",
    "payload_sha256",
    "first_attempt_slot",
    "second_attempt_slot",
    "boots_observed",
    *_REQUIRED_YES,
    "recovery_key_included",
)

_PRODUCTION_REQUIRED_YES = (
    "lab_override_absent",
    "public_latest_route",
    "bootstrap_network_persisted",
    "provider_ready",
    "signed_manifest_accepted",
    "payload_verified",
    "inactive_slot_written",
    "attempt_armed",
    "loader_consumed_attempt",
    "unconfirmed_attempt_spent",
    "loader_applied_rollback",
    "rollback_reported",
    "confirmed_slot_restored",
    "second_attempt_armed",
    "health_confirmed",
    "equal_release_refused",
)

_PRODUCTION_REQUIRED_FIELDS = (
    "format",
    "release_tag",
    "track",
    "provider",
    "trust_anchor",
    "trust_public_key",
    "predecessor_version",
    "manifest_version",
    "manifest_url",
    "payload_url",
    "payload_size",
    "payload_sha256",
    "boot_media_sha256",
    "first_attempt_slot",
    "second_attempt_slot",
    "boots_observed",
    "cycle_order",
    "bootstrap_vmnet",
    "bootstrap_network_mode",
    "bootstrap_ipv4",
    "bootstrap_mask",
    "bootstrap_gateway",
    "bootstrap_dns",
    *_PRODUCTION_REQUIRED_YES,
    "recovery_key_included",
)

_HEX64_RE = re.compile(r"^[0-9a-f]{64}$")
_DECIMAL_RE = re.compile(r"^[1-9][0-9]*$")


def manifest_url(host: str = QEMU_SLIRP_GATEWAY, port: int = LOCAL_HTTP_PORT) -> str:
    return f"http://{host}:{port}/{LAB_MANIFEST_NAME}"


def payload_url(host: str = QEMU_SLIRP_GATEWAY, port: int = LOCAL_HTTP_PORT) -> str:
    return f"http://{host}:{port}/{LAB_PAYLOAD_NAME}"


def attempt_marker(slot: int, state: str) -> str:
    """Kernel boot line restating the attempt token the loader published.

    Deliberately not the loader's own `[UEFI] Kernel A/B slot=...` line: that one
    goes through the firmware console, which the official VMware contract keeps
    off COM1 (`efi.serialConsole.enabled = "FALSE"`), so it is unobservable on
    the acceptance platform.
    """
    if state not in ("confirmed", "pending", "rollback"):
        raise ValueError(f"unknown attempt state: {state!r}")
    return f"[boot] A/B attempt slot={slot} state={state}"


def next_prerelease_version(current: str) -> str:
    """Return the smallest version the running kernel accepts as newer.

    The runtime comparator ignores build metadata entirely (it stops at '+'), so
    only the prerelease number can make a manifest "available". A manifest that
    differs from the compiled version only in `+YYYYMMDD` compares equal and the
    download refuses with -40.
    """
    match = _PRERELEASE_RE.match(current.strip())
    if not match:
        raise ValueError(f"unsupported CapyOS prerelease version: {current!r}")
    return f"{match['base']}-{match['label']}.{int(match['number']) + 1}"


def next_lab_update_version(current: str) -> str:
    """Return a strictly newer synthetic version for either release family.

    Prerelease builds keep the historical behaviour and increment their numeric
    suffix. Stable builds increment the patch component because build metadata
    is deliberately ignored by the runtime comparator.
    """
    prerelease = _PRERELEASE_RE.match(current.strip())
    if prerelease:
        return next_prerelease_version(current)
    match = _RUNTIME_VERSION_RE.fullmatch(current.strip())
    if not match or match["label"] is not None:
        raise ValueError(f"unsupported CapyOS runtime version: {current!r}")
    components = tuple(int(match[name]) for name in ("major", "minor", "patch"))
    if any(value > 0xFFFFFFFF for value in components):
        raise ValueError(f"CapyOS version component overflows uint32: {current!r}")
    if components[2] == 0xFFFFFFFF:
        raise ValueError(f"CapyOS patch version cannot be incremented: {current!r}")
    return f"{components[0]}.{components[1]}.{components[2] + 1}"


def _runtime_version_key(version: str) -> tuple[int, int, int, int, int]:
    """Mirror the runtime update comparator, including ignored build metadata."""
    match = _RUNTIME_VERSION_RE.fullmatch(version.strip())
    if not match:
        raise ValueError(f"unsupported CapyOS runtime version: {version!r}")
    values = tuple(int(match[name] or "0") for name in ("major", "minor", "patch"))
    number = int(match["number"] or "0")
    if any(value > 0xFFFFFFFF for value in (*values, number)):
        raise ValueError(f"CapyOS version component overflows uint32: {version!r}")
    rank = {None: 4, "alpha": 1, "beta": 2, "rc": 3}[match["label"]]
    return (*values, rank, number)


def compare_update_versions(candidate: str, current: str) -> int:
    """Return -1, 0 or 1 using the exact ordering enforced by the kernel."""
    candidate_key = _runtime_version_key(candidate)
    current_key = _runtime_version_key(current)
    return (candidate_key > current_key) - (candidate_key < current_key)


def stable_runtime_identity_marker(version: str) -> str:
    """Return the canonical ``print-version`` output for a stable runtime.

    The runtime command intentionally reports the semantic version without
    build metadata. Production identity remains bound separately to the pinned
    predecessor ISO hash and the signed candidate payload hash.
    """
    major, minor, patch, rank, number = _runtime_version_key(version)
    if rank != 4 or number != 0:
        raise ValueError(f"production runtime must be stable: {version!r}")
    return f"CapyOS {major}.{minor}.{patch} [stable]"


def channel_extended_from_version_yaml(text: str, channel: str) -> str:
    """Extract one channel's extended version without accepting another block."""
    wanted_indent = None
    for line in text.splitlines():
        channel_match = re.match(r"^(\s*)([A-Za-z0-9_-]+):\s*$", line)
        if channel_match:
            if wanted_indent is not None and len(channel_match[1]) <= wanted_indent:
                break
            if channel_match[2] == channel:
                wanted_indent = len(channel_match[1])
            continue
        if wanted_indent is not None:
            match = re.match(r"^\s*extended:\s*(\S+)\s*$", line)
            if match:
                return match.group(1)
    raise ValueError(f"VERSION.yaml has no extended version for channel {channel!r}")


def release_tag_from_version_yaml(
    text: str, runtime_version: str | None = None
) -> str:
    """Extract an extended release tag, optionally bound to the running build."""
    extended = []
    for line in text.splitlines():
        match = re.match(r"^\s*extended:\s*(\S+)\s*$", line)
        if match:
            extended.append(match.group(1))
    if runtime_version is None and extended:
        return extended[0]
    if runtime_version in extended:
        return runtime_version
    if runtime_version is not None:
        raise ValueError(
            f"VERSION.yaml has no channel matching runtime {runtime_version!r}"
        )
    raise ValueError("VERSION.yaml has no extended release tag")


def generate_lab_keypair(openssl: str, private_key: Path) -> str:
    """Create a throwaway Ed25519 key and return its raw public half as hex64.

    The key never leaves the run directory and is not the production anchor, so
    a leak cannot sign anything a shipped kernel would accept.
    """
    if private_key.exists():
        raise ManifestError(f"lab private key already exists: {private_key}")
    private_key.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [openssl, "genpkey", "-algorithm", "Ed25519", "-out", str(private_key)],
        check=True,
        capture_output=True,
    )
    private_key.chmod(0o600)
    raw_public = raw_public_from_private(openssl, private_key)
    if len(raw_public) != 32:
        raise ManifestError("unexpected raw Ed25519 public key length")
    return raw_public.hex()


def render_update_ab_vmx(
    *,
    display_name: str,
    iso_path: Path,
    target_descriptor: Path,
    pipe_name: str,
    boot_from: str,
) -> str:
    """Scratch VMX for the official gate: one UEFI disk, E1000 on NAT, COM1 pipe.

    Deliberately a separate renderer from the installer wizard's: that gate needs
    a second guard disk to prove non-target disks stay byte-identical, while this
    one needs working NAT so the guest can reach the host's signed endpoint.
    `efi.serialConsole.enabled` stays FALSE because the kernel owns COM1; the
    firmware redirecting into the same port would interleave the boot lines this
    gate asserts.
    """
    if boot_from not in ("cdrom", "hdd"):
        raise ValueError("boot_from must be 'cdrom' or 'hdd'")
    values = {
        "display_name": display_name,
        "iso": str(iso_path),
        "target": str(target_descriptor),
        "pipe": pipe_name,
    }
    for key, value in values.items():
        if not value or any(char in value for char in ('"', "\n", "\r")):
            raise ValueError(f"unsafe vmx value for {key}: {value!r}")
    return (
        '.encoding = "UTF-8"\n'
        'config.version = "8"\n'
        'virtualHW.version = "22"\n'
        f'displayName = "{values["display_name"]}"\n'
        'guestOS = "other-64"\n'
        'firmware = "efi"\n'
        'efi.serialConsole.enabled = "FALSE"\n'
        f'bios.bootOrder = "{boot_from}"\n'
        'memsize = "1024"\n'
        'numvcpus = "2"\n'
        'sata0.present = "TRUE"\n'
        'sata0:0.present = "TRUE"\n'
        f'sata0:0.fileName = "{values["target"]}"\n'
        'sata0:0.redo = ""\n'
        'sata0:1.present = "TRUE"\n'
        'sata0:1.deviceType = "cdrom-image"\n'
        f'sata0:1.fileName = "{values["iso"]}"\n'
        'sata0:1.startConnected = "TRUE"\n'
        'ethernet0.present = "TRUE"\n'
        'ethernet0.connectionType = "nat"\n'
        'ethernet0.virtualDev = "e1000"\n'
        'ethernet0.addressType = "generated"\n'
        'serial0.present = "TRUE"\n'
        'serial0.startConnected = "TRUE"\n'
        'serial0.fileType = "pipe"\n'
        f'serial0.fileName = "\\\\.\\pipe\\{values["pipe"]}"\n'
        'serial0.pipe.endPoint = "server"\n'
        'serial0.tryNoRxLoss = "TRUE"\n'
        'serial0.yieldOnMsrRead = "TRUE"\n'
        'sound.present = "FALSE"\n'
        'usb.present = "FALSE"\n'
        'floppy0.present = "FALSE"\n'
        'tools.syncTime = "FALSE"\n'
    )


def render_evidence(fields: Mapping[str, str]) -> str:
    if tuple(fields) != _REQUIRED_FIELDS:
        raise ValueError("evidence fields must use the canonical contract order")
    return "".join(f"{key}={fields[key]}\n" for key in _REQUIRED_FIELDS)


def parse_evidence(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if not line:
            continue
        if "=" not in line:
            raise ValueError(f"evidence line without '=': {line!r}")
        key, value = line.split("=", 1)
        if key in fields:
            raise ValueError(f"duplicate evidence field: {key}")
        fields[key] = value
    return fields


def validate_evidence(fields: Mapping[str, str]) -> None:
    missing = [key for key in _REQUIRED_FIELDS if key not in fields]
    if missing:
        raise ValueError(f"evidence is missing fields: {', '.join(missing)}")
    if fields["format"] != EVIDENCE_FORMAT:
        raise ValueError("evidence format mismatch")
    if fields["track"] != TRACK:
        raise ValueError(f"evidence track must be {TRACK}")
    if fields["provider"] not in PROVIDERS:
        raise ValueError(f"evidence provider must be one of {PROVIDERS}")
    if fields["trust_anchor"] != TRUST_ANCHOR:
        raise ValueError(f"evidence trust_anchor must be {TRUST_ANCHOR}")
    for key in _REQUIRED_YES:
        if fields[key] != "yes":
            raise ValueError(f"evidence field {key} must be 'yes'")
    if fields["recovery_key_included"] != "no":
        raise ValueError("evidence must never embed a recovery key")
    if not _HEX64_RE.match(fields["payload_sha256"]):
        raise ValueError("payload_sha256 must be lower-case hex64")
    if not _HEX64_RE.match(fields["lab_public_key"]):
        raise ValueError("lab_public_key must be lower-case hex64")
    if not _DECIMAL_RE.match(fields["payload_size"]):
        raise ValueError("payload_size must be a positive decimal")
    if fields["first_attempt_slot"] == fields["second_attempt_slot"]:
        raise ValueError("the two attempts must target different slots")
    for key in ("first_attempt_slot", "second_attempt_slot"):
        if fields[key] not in ("0", "1"):
            raise ValueError(f"{key} must be slot 0 or 1")
    if not _DECIMAL_RE.match(fields["boots_observed"]) or int(
        fields["boots_observed"]
    ) < 4:
        raise ValueError("a full two-cycle proof needs at least 4 observed boots")
    if contains_recovery_key(render_evidence(fields)):
        raise ValueError("evidence text still contains a recovery key")


def render_production_evidence(fields: Mapping[str, str]) -> str:
    if tuple(fields) != _PRODUCTION_REQUIRED_FIELDS:
        raise ValueError(
            "production evidence fields must use the canonical contract order"
        )
    return "".join(f"{key}={fields[key]}\n" for key in _PRODUCTION_REQUIRED_FIELDS)


def validate_production_evidence(fields: Mapping[str, str]) -> None:
    missing = [key for key in _PRODUCTION_REQUIRED_FIELDS if key not in fields]
    if missing:
        raise ValueError(
            f"production evidence is missing fields: {', '.join(missing)}"
        )
    if fields["format"] != PRODUCTION_EVIDENCE_FORMAT:
        raise ValueError("production evidence format mismatch")
    if fields["track"] != TRACK:
        raise ValueError(f"production evidence track must be {TRACK}")
    if fields["provider"] != "vmware-workstation":
        raise ValueError("production evidence provider must be vmware-workstation")
    if fields["trust_anchor"] != PRODUCTION_TRUST_ANCHOR:
        raise ValueError(
            f"production trust_anchor must be {PRODUCTION_TRUST_ANCHOR}"
        )
    if fields["cycle_order"] != PRODUCTION_CYCLE_ORDER:
        raise ValueError(
            f"production cycle_order must be {PRODUCTION_CYCLE_ORDER}"
        )
    if not re.fullmatch(r"(?i:vmnet[0-9]+)", fields["bootstrap_vmnet"]):
        raise ValueError("production bootstrap_vmnet must identify one VMware VMnet")
    if fields["bootstrap_network_mode"] != "static":
        raise ValueError("production bootstrap network mode must be static")
    try:
        bootstrap_interface = ipaddress.IPv4Interface(
            f"{fields['bootstrap_ipv4']}/{fields['bootstrap_mask']}"
        )
        bootstrap_gateway = ipaddress.IPv4Address(fields["bootstrap_gateway"])
        bootstrap_dns = ipaddress.IPv4Address(fields["bootstrap_dns"])
    except (ipaddress.AddressValueError, ipaddress.NetmaskValueError) as exc:
        raise ValueError(f"invalid production bootstrap IPv4 evidence: {exc}") from exc
    if str(bootstrap_interface.network.netmask) != fields["bootstrap_mask"]:
        raise ValueError("production bootstrap mask must be canonical")
    if bootstrap_interface.ip in (
        bootstrap_interface.network.network_address,
        bootstrap_interface.network.broadcast_address,
    ):
        raise ValueError("production bootstrap guest IPv4 is not usable")
    if bootstrap_gateway not in bootstrap_interface.network or bootstrap_gateway in (
        bootstrap_interface.network.network_address,
        bootstrap_interface.network.broadcast_address,
        bootstrap_interface.ip,
    ):
        raise ValueError(
            "production bootstrap gateway is not a distinct usable address"
        )
    if (
        bootstrap_dns.is_unspecified
        or bootstrap_dns.is_multicast
        or bootstrap_dns.is_loopback
        or bootstrap_dns.is_reserved
        or (
            bootstrap_dns in bootstrap_interface.network
            and bootstrap_dns
            in (
                bootstrap_interface.network.network_address,
                bootstrap_interface.network.broadcast_address,
            )
        )
    ):
        raise ValueError("production bootstrap DNS is not usable")
    for key in _PRODUCTION_REQUIRED_YES:
        if fields[key] != "yes":
            raise ValueError(f"production evidence field {key} must be 'yes'")
    if fields["recovery_key_included"] != "no":
        raise ValueError("production evidence must never embed a recovery key")
    for key in ("trust_public_key", "payload_sha256", "boot_media_sha256"):
        if not _HEX64_RE.fullmatch(fields[key]):
            raise ValueError(f"{key} must be lower-case hex64")
    if not _DECIMAL_RE.fullmatch(fields["payload_size"]):
        raise ValueError("payload_size must be a positive decimal")
    for key in ("first_attempt_slot", "second_attempt_slot"):
        if fields[key] not in ("0", "1"):
            raise ValueError(f"{key} must be slot 0 or 1")
    if (fields["first_attempt_slot"], fields["second_attempt_slot"]) != ("1", "1"):
        raise ValueError(
            "production rollback-then-confirm must reapply twice to inactive slot 1"
        )
    if not _DECIMAL_RE.fullmatch(fields["boots_observed"]) or int(
        fields["boots_observed"]
    ) < 4:
        raise ValueError("a production two-cycle proof needs at least 4 boots")
    if compare_update_versions(
        fields["manifest_version"], fields["predecessor_version"]
    ) <= 0:
        raise ValueError("production manifest must be newer than its predecessor")
    if fields["release_tag"] != f"v{fields['manifest_version']}":
        raise ValueError("release_tag must identify the manifest version exactly")
    if not fields["manifest_url"].startswith("https://") or not fields[
        "manifest_url"
    ].endswith("/releases/latest/download/latest.ini"):
        raise ValueError("production manifest_url must use the public Latest route")
    if not fields["payload_url"].startswith("https://"):
        raise ValueError("production payload_url must use HTTPS")
    expected_payload_suffix = (
        f"/releases/download/{fields['release_tag']}/capyos64.bin"
    )
    if not fields["payload_url"].endswith(expected_payload_suffix):
        raise ValueError("production payload_url must identify the release exactly")
    if contains_recovery_key(render_production_evidence(fields)):
        raise ValueError("production evidence text still contains a recovery key")


__all__ = [
    "APPLY_OK",
    "APPLY_SUMMARY",
    "ARMED_ATTEMPT_EXPECTATIONS",
    "ATTEMPT_PENDING_SUMMARY",
    "CONFIRM_OK",
    "CONFIRM_SUMMARY",
    "DOWNLOAD_OK",
    "EVIDENCE_FORMAT",
    "FETCH_OK",
    "LAB_BANNER",
    "MANIFEST_NOT_NEWER_SUMMARY",
    "LAB_MANIFEST_NAME",
    "LAB_PAYLOAD_NAME",
    "LOCAL_HTTP_PORT",
    "NO_ROLLBACK_SUMMARY",
    "PREPARE_EXPLAIN_CLEAN",
    "PREPARE_EXPLAIN_OK",
    "PREPARE_OK",
    "PRODUCTION_CYCLE_ORDER",
    "PRODUCTION_EVIDENCE_FORMAT",
    "PRODUCTION_TRUST_ANCHOR",
    "PROVIDER_READY_LINE",
    "PROVIDERS",
    "QEMU_SLIRP_GATEWAY",
    "ROLLBACK_OK",
    "ROLLBACK_SUMMARY",
    "TRACK",
    "TRUST_ANCHOR",
    "attempt_marker",
    "channel_extended_from_version_yaml",
    "compare_update_versions",
    "contains_recovery_key",
    "generate_lab_keypair",
    "manifest_url",
    "next_lab_update_version",
    "next_prerelease_version",
    "parse_evidence",
    "payload_url",
    "release_tag_from_version_yaml",
    "render_evidence",
    "render_production_evidence",
    "render_update_ab_vmx",
    "sanitize_public_text",
    "validate_evidence",
    "validate_production_evidence",
]
