from __future__ import annotations

import hashlib
import re
from pathlib import Path
from typing import Mapping

RECOVERY_KEY_RE = re.compile(
    r"(?<![A-Z0-9])[A-Z0-9]{4}(?:-[A-Z0-9]{4}){5}(?![A-Z0-9])"
)
EVIDENCE_FORMAT = "capyos-installer-wizard-evidence-manifest-v3"
_REQUIRED_YES = (
    "iso_unchanged",
    "target_selected_explicitly",
    "target_identity_revalidated",
    "erase_token_confirmed",
    "target_changed",
    "guard_unchanged",
    "fresh_install_completed",
    "first_boot_completed",
    "login_completed",
    "persistence_marker_written",
    "persistence_marker_read_after_reboot",
    "recovery_key_redacted",
)
_REQUIRED_FIELDS = (
    "format",
    "release_tag",
    "track",
    "provider",
    "firmware",
    "secure_boot",
    "vcpu_count",
    "memory_mib",
    "network",
    "iso_artifact",
    "iso_sha256",
    "iso_sha256_before",
    "iso_sha256_after",
    "iso_unchanged",
    "eligible_target_count",
    "target_selected_explicitly",
    "target_selected_index",
    "target_path_id",
    "target_size_mib",
    "guard_size_mib",
    "target_identity_revalidated",
    "erase_token_confirmed",
    "target_sha256_before",
    "target_sha256_after",
    "target_changed",
    "guard_sha256_before",
    "guard_sha256_after",
    "guard_unchanged",
    "fresh_install_completed",
    "first_boot_completed",
    "login_completed",
    "persistence_marker_written",
    "persistence_marker_read_after_reboot",
    "recovery_key_redacted",
    "recovery_key_included",
    "marker_session_used",
    "installer_log",
    "installer_log_sha256",
    "boot1_log",
    "boot1_log_sha256",
    "marker_log",
    "marker_log_sha256",
    "boot2_log",
    "boot2_log_sha256",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def sanitize_public_text(text: str, secrets: tuple[str, ...] = ()) -> str:
    sanitized = text
    for secret in secrets:
        if secret:
            sanitized = sanitized.replace(secret, "[REDACTED-VOLUME-RECOVERY-KEY]")
    sanitized = RECOVERY_KEY_RE.sub("[REDACTED-VOLUME-RECOVERY-KEY]", sanitized)
    return sanitized


def contains_recovery_key(text: str) -> bool:
    return RECOVERY_KEY_RE.search(text) is not None


def render_scratch_vmx(
    *,
    display_name: str,
    iso_path: Path,
    target_descriptor: Path,
    guard_descriptor: Path,
    pipe_name: str,
    boot_from: str,
) -> str:
    if boot_from not in {"cdrom", "hdd"}:
        raise ValueError("boot_from must be cdrom or hdd")
    values = (display_name, str(iso_path), str(target_descriptor), str(guard_descriptor), pipe_name)
    if any(not value or "\n" in value or "\r" in value or '"' in value for value in values):
        raise ValueError("VMX values contain unsupported characters")
    boot_order = "cdrom" if boot_from == "cdrom" else "hdd"
    pipe_path = "\\\\.\\pipe\\" + pipe_name
    lines = (
        '.encoding = "UTF-8"',
        'config.version = "8"',
        'virtualHW.version = "22"',
        f'displayName = "{display_name}"',
        'guestOS = "other-64"',
        'firmware = "efi"',
        'uefi.secureBoot.enabled = "FALSE"',
        # The loader owns COM1 directly. Firmware serial-console redirection
        # would feed each pipe byte into both ConIn and the UART, splitting a
        # line across two independent consumers.
        'efi.serialConsole.enabled = "FALSE"',
        f'bios.bootOrder = "{boot_order}"',
        'memsize = "1024"',
        'numvcpus = "2"',
        'sata0.present = "TRUE"',
        'sata0:0.present = "TRUE"',
        f'sata0:0.fileName = "{target_descriptor}"',
        'sata0:0.redo = ""',
        'sata0:1.present = "TRUE"',
        f'sata0:1.fileName = "{guard_descriptor}"',
        'sata0:1.redo = ""',
        'sata0:2.present = "TRUE"',
        'sata0:2.deviceType = "cdrom-image"',
        f'sata0:2.fileName = "{iso_path}"',
        'sata0:2.startConnected = "TRUE"',
        'ethernet0.present = "TRUE"',
        'ethernet0.connectionType = "nat"',
        'ethernet0.virtualDev = "e1000"',
        'ethernet0.addressType = "generated"',
        'serial0.present = "TRUE"',
        'serial0.startConnected = "TRUE"',
        'serial0.fileType = "pipe"',
        f'serial0.fileName = "{pipe_path}"',
        'serial0.pipe.endPoint = "server"',
        'serial0.tryNoRxLoss = "TRUE"',
        'serial0.yieldOnMsrRead = "TRUE"',
        'sound.present = "FALSE"',
        'usb.present = "FALSE"',
        'floppy0.present = "FALSE"',
        'tools.syncTime = "FALSE"',
    )
    return "\n".join(lines) + "\n"


def parse_flat_extent(descriptor: Path) -> Path:
    text = descriptor.read_text(encoding="utf-8", errors="strict")
    matches = re.findall(r'^RW\s+\d+\s+FLAT\s+"([^"]+)"\s+\d+\s*$', text, flags=re.MULTILINE)
    if len(matches) != 1:
        raise ValueError("VMDK descriptor must contain exactly one FLAT extent")
    descriptor_root = descriptor.parent.resolve()
    extent = (descriptor_root / matches[0]).resolve()
    try:
        extent.relative_to(descriptor_root)
    except ValueError as exc:
        raise ValueError("VMDK extent must stay beside its descriptor") from exc
    if extent.parent != descriptor_root:
        raise ValueError("VMDK extent must be a direct descriptor sibling")
    if not extent.is_file():
        raise FileNotFoundError(extent)
    return extent


def exact_extent_size_mib(extent: Path) -> int:
    size = extent.stat().st_size
    mib = 1024 * 1024
    if size <= 0 or size % mib != 0:
        raise ValueError("VMDK extent size must be a positive whole MiB")
    return size // mib


def render_evidence(fields: Mapping[str, str]) -> str:
    keys = tuple(fields)
    if keys != _REQUIRED_FIELDS:
        raise ValueError("installer evidence fields or order are invalid")
    validate_evidence(fields)
    return "".join(f"{key}={fields[key]}\n" for key in _REQUIRED_FIELDS)


def parse_evidence(text: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for raw in text.splitlines():
        if not raw or "=" not in raw:
            raise ValueError("installer evidence contains malformed line")
        key, value = raw.split("=", 1)
        if key in parsed or key not in _REQUIRED_FIELDS or not value:
            raise ValueError("installer evidence contains duplicate, unknown or empty field")
        parsed[key] = value
    if tuple(parsed) != _REQUIRED_FIELDS:
        raise ValueError("installer evidence fields or order are invalid")
    validate_evidence(parsed)
    return parsed


def validate_evidence(fields: Mapping[str, str]) -> None:
    if fields.get("format") != EVIDENCE_FORMAT:
        raise ValueError("installer evidence format is invalid")
    if fields.get("track") != "UEFI/GPT/x86_64" or fields.get("provider") != "vmware-workstation":
        raise ValueError("installer evidence platform is invalid")
    if (
        fields.get("firmware") != "uefi"
        or fields.get("secure_boot") != "disabled"
        or fields.get("vcpu_count") != "2"
        or fields.get("memory_mib") != "1024"
        or fields.get("network") != "nat-e1000"
    ):
        raise ValueError("installer evidence VM topology is invalid")
    if fields.get("recovery_key_included") != "no":
        raise ValueError("installer evidence cannot include recovery key")
    for key in _REQUIRED_YES:
        if fields.get(key) != "yes":
            raise ValueError(f"installer evidence requires {key}=yes")
    try:
        eligible = int(fields.get("eligible_target_count", "0"), 10)
    except ValueError as exc:
        raise ValueError("eligible target count is invalid") from exc
    if eligible < 2:
        raise ValueError("installer evidence requires at least two eligible targets")
    try:
        selected_index = int(fields.get("target_selected_index", "0"), 10)
        target_size_mib = int(fields.get("target_size_mib", "0"), 10)
        guard_size_mib = int(fields.get("guard_size_mib", "0"), 10)
    except ValueError as exc:
        raise ValueError("installer evidence disk identity is invalid") from exc
    if selected_index < 1 or selected_index > eligible:
        raise ValueError("installer evidence selected index is outside target inventory")
    if target_size_mib <= 0 or guard_size_mib <= target_size_mib:
        raise ValueError("installer evidence disk capacities are invalid")
    if not re.fullmatch(r"[0-9a-f]{16}", fields.get("target_path_id", "")):
        raise ValueError("installer evidence PathId is invalid")
    if fields.get("marker_session_used") not in ("yes", "no"):
        raise ValueError("installer evidence marker session state is invalid")
    for key in ("installer_log", "boot1_log", "marker_log", "boot2_log"):
        value = fields.get(key, "")
        if not re.fullmatch(r"[A-Za-z0-9._-]+\.log", value):
            raise ValueError(f"installer evidence artifact name is invalid: {key}")
    digest_fields = tuple(key for key in _REQUIRED_FIELDS if key.endswith("sha256"))
    for key in digest_fields:
        if not re.fullmatch(r"[0-9a-f]{64}", fields.get(key, "")):
            raise ValueError(f"installer evidence digest is invalid: {key}")
    if not (
        fields["iso_sha256"]
        == fields["iso_sha256_before"]
        == fields["iso_sha256_after"]
    ):
        raise ValueError("installer ISO changed during validation")
    if fields["target_sha256_before"] == fields["target_sha256_after"]:
        raise ValueError("installer target did not change")
    if fields["guard_sha256_before"] != fields["guard_sha256_after"]:
        raise ValueError("installer guard changed")
    rendered = "\n".join(f"{key}={value}" for key, value in fields.items())
    if contains_recovery_key(rendered):
        raise ValueError("installer evidence contains recovery key material")
