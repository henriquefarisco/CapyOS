from __future__ import annotations

import re
from pathlib import Path

from hyperv_baseline_evidence_summary import (
    classify_bundle_file as summary_classify_bundle_file,
    classify_bundle_files as summary_classify_bundle_files,
    result_summary as summary_result_summary,
    stable_code_for_value,
    write_summary,
)

FAILURE_MARKERS = (
    "kernel panic",
    "panic:",
    "triple fault",
    "general protection fault",
)

KERNEL_LOAD_FAILURE_PATTERNS = {
    "kernel-load-failed": (
        re.compile(r"\b(?:failed|unable|could not|cannot)\s+to\s+load\s+(?:the\s+)?kernel\b", re.IGNORECASE),
        re.compile(r"\bkernel\s+load\s+(?:failed|failure|error)\b", re.IGNORECASE),
        re.compile(r"\b(?:erro|falha)\s+(?:ao|no)\s+carregar\s+(?:o\s+)?kernel\b", re.IGNORECASE),
    ),
    "kernel-image-missing": (
        re.compile(r"\bkernel\s+(?:image\s+)?not\s+found\b", re.IGNORECASE),
        re.compile(r"\bmissing\s+kernel\s+(?:image|file)\b", re.IGNORECASE),
    ),
    "kernel-manifest-entry-missing": (
        re.compile(r"\bmanifest\s+kernel\s+entry\s+missing\b", re.IGNORECASE),
        re.compile(r"\bkernel\s+entry\s+missing\b", re.IGNORECASE),
        re.compile(r"\bkernel\s+\(gpt:manifest\)\s+missing\b", re.IGNORECASE),
    ),
    "kernel-elf-invalid": (
        re.compile(r"\binvalid\s+elf\b", re.IGNORECASE),
        re.compile(r"\belf\s+header\s+invalid\b", re.IGNORECASE),
        re.compile(r"\bunsupported\s+elf\b", re.IGNORECASE),
    ),
}

VMBUS_RE = re.compile(r"\bvmbus=([a-z0-9_-]+)", re.IGNORECASE)
STAGE_RE = re.compile(r"\bstage=([a-z0-9_-]+)", re.IGNORECASE)
EVIDENCE_HEADER_RE = re.compile(r"^--- evidence: (.+) ---$")

MIN_REQUIRED_TOKENS = (
    "runtime-native show",
    "net-status",
    "net-dump-runtime",
    "recovery-storage",
)

BOOT_TOKENS = (
    "[vmbus]",
    "runtime-native show",
    "net-status",
    "net-dump-runtime",
)

SHELL_TOKENS = (
    "runtime-native show",
    "net-status",
    "net-dump-runtime",
    "recovery-storage",
    "capysh",
    "login",
    "maintenance",
)

STORAGE_TOKENS = (
    "recovery-storage",
    "storvsc",
    "storage",
    "ramdisk",
    "persistent",
    "firmware",
)

INSPECT_DISK_TOKENS = (
    "[*] GPT:",
    "GPT[",
    "BOOT:",
    "Manifest @ LBA",
    "Kernel (gpt:manifest)",
)

HOST_PREFLIGHT_TOKENS = (
    "Hyper-V preflight",
    "hyperv_module=",
    '"hyperv_module"',
)

HOST_PREFLIGHT_CHECKS = {
    "generation_2": (
        re.compile(r"\bgeneration\s*=\s*2\b", re.IGNORECASE),
        re.compile(r'"generation"\s*:\s*2\b', re.IGNORECASE),
    ),
    "secure_boot_off": (
        re.compile(r"\bsecure_boot\s*=\s*False\b", re.IGNORECASE),
        re.compile(r'"secure_boot"\s*:\s*false\b', re.IGNORECASE),
    ),
    "dynamic_memory_off": (
        re.compile(r"\bdynamic_memory_enabled\s*=\s*False\b", re.IGNORECASE),
        re.compile(r'"dynamic_memory_enabled"\s*:\s*false\b', re.IGNORECASE),
    ),
}

HOST_SERIAL_TEXT_RE = re.compile(
    r"\bcom\s+number\s*=\s*\d+.*\bpath\s*=\s*\S+.*\bdisconnected\s*=\s*False\b",
    re.IGNORECASE,
)
HOST_SERIAL_JSON_OBJECT_PATH_FIRST_RE = re.compile(
    r'\{[^{}]*"path"\s*:\s*"[^"]+"[^{}]*"disconnected"\s*:\s*false\b[^{}]*\}',
    re.IGNORECASE,
)
HOST_SERIAL_JSON_OBJECT_CONNECTED_FIRST_RE = re.compile(
    r'\{[^{}]*"disconnected"\s*:\s*false\b[^{}]*"path"\s*:\s*"[^"]+"[^{}]*\}',
    re.IGNORECASE,
)
HOST_NETWORK_TEXT_RE = re.compile(
    r"^net\s+name=.*\bswitch\s*=\s*(?!\s*(?:status=|mac=|legacy=|$))\S.*\blegacy\s*=\s*False\b",
    re.IGNORECASE | re.MULTILINE,
)
HOST_NETWORK_JSON_OBJECT_RE = re.compile(
    r'\{(?=[^{}]*"switch_name"\s*:\s*"[^"]+")(?![^{}]*"switch_name"\s*:\s*"")(?![^{}]*"switch_name"\s*:\s*null)(?=[^{}]*"is_legacy"\s*:\s*false\b)[^{}]*\}',
    re.IGNORECASE,
)
HOST_STORAGE_TEXT_RE = re.compile(
    r"^disk\s+type\s*=\s*SCSI\b.*\bpath\s*=\s*\S+",
    re.IGNORECASE | re.MULTILINE,
)
HOST_STORAGE_JSON_OBJECT_RE = re.compile(
    r'\{(?=[^{}]*"controller_type"\s*:\s*"SCSI")(?=[^{}]*"path"\s*:\s*"[^"]+")[^{}]*\}',
    re.IGNORECASE,
)

EVIDENCE_BUNDLE_CANDIDATES = (
    "serial.log",
    "hyperv-serial.log",
    "commands.log",
    "guest-commands.log",
    "inspect-disk.log",
    "inspect.log",
    "hyperv-preflight.txt",
    "hyperv-preflight.json",
)

BUNDLE_FILE_GROUPS = {
    "serial": ("serial.log", "hyperv-serial.log"),
    "commands": ("commands.log", "guest-commands.log"),
    "inspect-disk": ("inspect-disk.log", "inspect.log"),
    "preflight": ("hyperv-preflight.txt", "hyperv-preflight.json"),
}

BUS_NOT_READY_STAGES = ("off", "hypercall", "synic", "contact")
CHANNEL_NOT_READY_STAGES = ("off", "hypercall", "synic", "contact", "offers")
GATE_PROFILES = ("baseline", "net-ready", "storage-persistent", "input-ready")
HOST_PREFLIGHT_PROFILES = ("none", "gen2-ready")
VALIDATION_PROFILES = ("none", "hyperv-gen2-ready")
NEXT_SLICE_ALIASES = {
    "boot-kernel-load": "boot/kernel load",
    "boot-vmbus-contact": "boot/VMBus contact",
    "vmbus-offers-channel": "VMBus offers/channel",
    "input-promotion": "input promotion",
    "storvsc-storage": "StorVSC storage",
    "netvsc-network": "NetVSC network",
}
FOCUS_ALIASES = {
    "baseline-accepted": "baseline accepted",
    "kernel-loader-failure": "kernel loader failure",
    "kernel-panic-fault": "kernel panic/fault",
    "spontaneous-reset": "spontaneous reset",
    "missing-vmbus-diagnostics": "missing VMBus diagnostics",
    "missing-runtime-stage-diagnostics": "missing runtime stage diagnostics",
    "boot-runtime-evidence": "boot/runtime evidence",
    "storage-fallback-evidence": "storage/fallback evidence",
    "required-command-evidence": "required command evidence",
    "disk-inspection-evidence": "disk inspection evidence",
    "host-preflight-evidence": "host preflight evidence",
    "host-serial-console-evidence": "host serial console evidence",
    "host-network-adapter-evidence": "host network adapter evidence",
    "host-storage-disk-evidence": "host storage disk evidence",
    "bundle-file-evidence": "bundle file evidence",
    "validation-profile-requirement": "validation profile requirement",
    "minimum-stage-requirement": "minimum stage requirement",
    "gate-profile-requirement": "gate profile requirement",
    "expectation-mismatch": "expectation mismatch",
}
STAGE_ORDER = {
    "off": 0,
    "hypercall": 1,
    "synic": 2,
    "contact": 3,
    "offers": 4,
    "channel": 5,
    "control": 6,
    "ready": 7,
    "failed": -1,
}


class EvidenceResult:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self.missing_required_commands: list[str] = []
        self.kernel_load_failure_markers: list[str] = []
        self.kernel_load_failure_details: list[dict[str, object]] = []
        self.vmbus_values: list[str] = []
        self.stage_values: list[str] = []
        self.next_slice = ""
        self.failure_focus = ""
        self.inspect_disk_present = False
        self.host_preflight_present = False
        self.host_preflight_checks: dict[str, bool] = {}
        self.missing_host_preflight_checks: list[str] = []
        self.host_serial_console_present = False
        self.host_network_adapter_present = False
        self.host_storage_disk_present = False
        self.missing_host_requirements: list[str] = []
        self.host_preflight_profile = "none"
        self.validation_profile = "none"
        self.effective_requirements: dict[str, object] = {}
        self.present_bundle_files: list[str] = []
        self.required_bundle_files: list[str] = []
        self.missing_bundle_files: list[str] = []
        self.minimum_requirements: dict[str, str] = {}
        self.gate_profile = "baseline"
        self.strict_commands = False
        self.source_logs: list[str] = []
        self.expected_next_slice = ""
        self.expected_failure_focus = ""

    def ok(self) -> bool:
        return not self.errors


def _contains_any(text: str, tokens: tuple[str, ...]) -> bool:
    low = text.lower()
    return any(token.lower() in low for token in tokens)


def _missing_tokens(text: str, tokens: tuple[str, ...]) -> list[str]:
    low = text.lower()
    return [token for token in tokens if token.lower() not in low]


def _matches_any(text: str, patterns: tuple[re.Pattern[str], ...]) -> bool:
    return any(pattern.search(text) for pattern in patterns)


def _ordered_unique(values: list[str]) -> list[str]:
    seen: dict[str, None] = {}
    for value in values:
        seen.setdefault(value.lower(), None)
    return list(seen.keys())


def detect_kernel_load_failure_markers(text: str) -> list[str]:
    return _ordered_unique([
        str(detail["marker"])
        for detail in detect_kernel_load_failure_details(text)
    ])


def detect_kernel_load_failure_details(text: str) -> list[dict[str, object]]:
    details: list[dict[str, object]] = []
    source = ""
    source_line = 0
    skip_source_separator = False
    for line_number, line in enumerate(text.splitlines(), 1):
        header = EVIDENCE_HEADER_RE.match(line.strip())
        if header:
            source = header.group(1)
            source_line = 0
            skip_source_separator = True
            continue
        if source:
            if skip_source_separator and not line:
                skip_source_separator = False
                continue
            skip_source_separator = False
            source_line += 1
        stripped = line.strip()
        if not stripped:
            continue
        for marker, patterns in KERNEL_LOAD_FAILURE_PATTERNS.items():
            if _matches_any(line, patterns):
                details.append({
                    "marker": marker,
                    "line": line_number,
                    "source": source,
                    "source_name": Path(source).name if source else "",
                    "source_line": source_line if source else line_number,
                    "snippet": stripped[:160],
                })
    return details


def apply_host_preflight_profile(
    result: EvidenceResult,
    host_preflight_profile: str,
    require_host_preflight: bool,
    require_host_serial_console: bool,
    require_host_network_adapter: bool,
    require_host_storage_disk: bool,
) -> tuple[bool, bool, bool, bool]:
    profile = (host_preflight_profile or "none").strip().lower()
    result.host_preflight_profile = profile
    if profile not in HOST_PREFLIGHT_PROFILES:
        result.errors.append(f"invalid host preflight profile: {host_preflight_profile}")
        return (
            require_host_preflight,
            require_host_serial_console,
            require_host_network_adapter,
            require_host_storage_disk,
        )
    if profile == "gen2-ready":
        return True, True, True, True
    return (
        require_host_preflight,
        require_host_serial_console,
        require_host_network_adapter,
        require_host_storage_disk,
    )


def apply_validation_profile(
    result: EvidenceResult,
    validation_profile: str,
    require_inspect_disk: bool,
    strict_commands: bool,
    host_preflight_profile: str,
    required_bundle_files: list[str] | None,
) -> tuple[bool, bool, str, list[str]]:
    profile = (validation_profile or "none").strip().lower()
    required_files = [item.lower() for item in (required_bundle_files or [])]
    result.validation_profile = profile
    if profile not in VALIDATION_PROFILES:
        result.errors.append(f"invalid validation profile: {validation_profile}")
        return require_inspect_disk, strict_commands, host_preflight_profile, required_files
    if profile == "hyperv-gen2-ready":
        require_inspect_disk = True
        strict_commands = True
        host_preflight_profile = "gen2-ready"
        for group in ("serial", "commands", "inspect-disk", "preflight"):
            if group not in required_files:
                required_files.append(group)
    return require_inspect_disk, strict_commands, host_preflight_profile, required_files


def validate_evidence(
    text: str,
    require_inspect_disk: bool = False,
    min_vmbus_stage: str = "",
    min_runtime_stage: str = "",
    gate_profile: str = "baseline",
    strict_commands: bool = False,
    require_host_preflight: bool = False,
    require_host_serial_console: bool = False,
    require_host_network_adapter: bool = False,
    require_host_storage_disk: bool = False,
    validation_profile: str = "none",
    host_preflight_profile: str = "none",
    source_paths: list[Path] | None = None,
    required_bundle_files: list[str] | None = None,
    expect_next_slice: str = "",
    expect_failure_focus: str = "",
) -> EvidenceResult:
    result = EvidenceResult()
    (
        require_inspect_disk,
        strict_commands,
        host_preflight_profile,
        required_bundle_files,
    ) = apply_validation_profile(
        result,
        validation_profile,
        require_inspect_disk,
        strict_commands,
        host_preflight_profile,
        required_bundle_files,
    )
    low = text.lower()
    result.gate_profile = gate_profile
    result.strict_commands = strict_commands
    if source_paths:
        result.source_logs = [str(path) for path in source_paths]
    (
        require_host_preflight,
        require_host_serial_console,
        require_host_network_adapter,
        require_host_storage_disk,
    ) = apply_host_preflight_profile(
        result,
        host_preflight_profile,
        require_host_preflight,
        require_host_serial_console,
        require_host_network_adapter,
        require_host_storage_disk,
    )
    result.effective_requirements = {
        "validation_profile": result.validation_profile,
        "strict_commands": strict_commands,
        "require_inspect_disk": require_inspect_disk,
        "required_bundle_files": list(required_bundle_files or []),
        "host_preflight_profile": result.host_preflight_profile,
        "require_host_preflight": require_host_preflight,
        "require_host_serial_console": require_host_serial_console,
        "require_host_network_adapter": require_host_network_adapter,
        "require_host_storage_disk": require_host_storage_disk,
    }

    for marker in FAILURE_MARKERS:
        if marker in low:
            result.errors.append(f"failure marker found: {marker}")
    result.kernel_load_failure_details = detect_kernel_load_failure_details(text)
    result.kernel_load_failure_markers = _ordered_unique([
        str(detail["marker"])
        for detail in result.kernel_load_failure_details
    ])
    for marker in result.kernel_load_failure_markers:
        result.errors.append(f"kernel load failure marker found: {marker}")

    if not _contains_any(text, BOOT_TOKENS):
        result.errors.append("boot/runtime evidence not found")
    if not _contains_any(text, SHELL_TOKENS):
        result.errors.append("shell or maintenance evidence not found")
    if not _contains_any(text, STORAGE_TOKENS):
        result.errors.append("storage/fallback evidence not found")

    result.vmbus_values = _ordered_unique(VMBUS_RE.findall(text))
    result.stage_values = _ordered_unique(STAGE_RE.findall(text))
    if not result.vmbus_values:
        result.errors.append("missing vmbus=<stage> evidence")
    if not result.stage_values:
        result.errors.append("missing stage=<phase> evidence")

    missing = _missing_tokens(text, MIN_REQUIRED_TOKENS)
    result.missing_required_commands = missing
    for token in missing:
        result.warnings.append(f"recommended command output missing: {token}")
        if strict_commands:
            result.errors.append(f"required command output missing: {token}")

    if "reset" in low and "spontaneous" in low:
        result.errors.append("spontaneous reset evidence found")

    result.inspect_disk_present = _contains_any(text, INSPECT_DISK_TOKENS)
    if require_inspect_disk and not result.inspect_disk_present:
        result.errors.append("inspect-disk evidence not found")
    validate_host_preflight(text, result, require_host_preflight)
    validate_host_serial_console(text, result, require_host_serial_console)
    validate_host_network_adapter(text, result, require_host_network_adapter)
    validate_host_storage_disk(text, result, require_host_storage_disk)
    update_missing_host_requirements(result)
    validate_bundle_file_requirements(result, source_paths or [], required_bundle_files or [])

    validate_min_stage(result, "vmbus", min_vmbus_stage, result.vmbus_values)
    validate_min_stage(result, "stage", min_runtime_stage, result.stage_values)
    validate_gate_profile(text, result, gate_profile)

    result.next_slice = recommend_next_slice(text, result)
    result.failure_focus = classify_failure_focus(text, result)
    validate_expectations(result, expect_next_slice, expect_failure_focus)
    return result


def read_evidence_text(log_path: Path, extra_log_paths: list[Path]) -> str:
    return read_evidence_paths([log_path, *extra_log_paths])


def read_evidence_paths(paths: list[Path]) -> str:
    chunks: list[str] = []
    for path in paths:
        chunks.append(f"\n--- evidence: {path} ---\n")
        chunks.append(path.read_text(encoding="latin-1", errors="replace"))
    return "\n".join(chunks)


def validate_host_preflight(
    text: str,
    result: EvidenceResult,
    require_host_preflight: bool,
) -> None:
    result.host_preflight_present = _contains_any(text, HOST_PREFLIGHT_TOKENS)
    result.host_preflight_checks = {
        name: _matches_any(text, patterns)
        for name, patterns in HOST_PREFLIGHT_CHECKS.items()
    }
    result.missing_host_preflight_checks = [
        name for name, ok in result.host_preflight_checks.items() if not ok
    ]
    if not require_host_preflight:
        return
    if not result.host_preflight_present:
        result.errors.append("host preflight evidence not found")
        return
    for name, ok in result.host_preflight_checks.items():
        if not ok:
            result.errors.append(f"host preflight check missing: {name}")


def detect_host_serial_console(text: str) -> bool:
    if HOST_SERIAL_TEXT_RE.search(text):
        return True
    return bool(
        HOST_SERIAL_JSON_OBJECT_PATH_FIRST_RE.search(text)
        or HOST_SERIAL_JSON_OBJECT_CONNECTED_FIRST_RE.search(text)
    )


def validate_host_serial_console(
    text: str,
    result: EvidenceResult,
    require_host_serial_console: bool,
) -> None:
    result.host_serial_console_present = detect_host_serial_console(text)
    if require_host_serial_console and not result.host_serial_console_present:
        result.errors.append("host serial console evidence not found")


def detect_host_network_adapter(text: str) -> bool:
    if HOST_NETWORK_TEXT_RE.search(text):
        return True
    return bool(HOST_NETWORK_JSON_OBJECT_RE.search(text))


def validate_host_network_adapter(
    text: str,
    result: EvidenceResult,
    require_host_network_adapter: bool,
) -> None:
    result.host_network_adapter_present = detect_host_network_adapter(text)
    if require_host_network_adapter and not result.host_network_adapter_present:
        result.errors.append("host network adapter evidence not found")


def detect_host_storage_disk(text: str) -> bool:
    if HOST_STORAGE_TEXT_RE.search(text):
        return True
    return bool(HOST_STORAGE_JSON_OBJECT_RE.search(text))


def validate_host_storage_disk(
    text: str,
    result: EvidenceResult,
    require_host_storage_disk: bool,
) -> None:
    result.host_storage_disk_present = detect_host_storage_disk(text)
    if require_host_storage_disk and not result.host_storage_disk_present:
        result.errors.append("host storage disk evidence not found")


def update_missing_host_requirements(result: EvidenceResult) -> None:
    requirements = result.effective_requirements
    missing: list[str] = []
    if requirements.get("require_host_preflight") and not result.host_preflight_present:
        missing.append("host_preflight")
    if requirements.get("require_host_serial_console") and not result.host_serial_console_present:
        missing.append("host_serial_console")
    if requirements.get("require_host_network_adapter") and not result.host_network_adapter_present:
        missing.append("host_network_adapter")
    if requirements.get("require_host_storage_disk") and not result.host_storage_disk_present:
        missing.append("host_storage_disk")
    result.missing_host_requirements = missing


def discover_bundle_logs(bundle_dir: Path) -> list[Path]:
    if not bundle_dir.is_dir():
        raise ValueError(f"evidence bundle directory not found: {bundle_dir}")
    found: list[Path] = []
    for name in EVIDENCE_BUNDLE_CANDIDATES:
        path = bundle_dir / name
        if path.is_file():
            found.append(path)
    return found


def merge_evidence_paths(
    log_path: Path | None,
    extra_log_paths: list[Path],
    bundle_paths: list[Path],
) -> list[Path]:
    merged: list[Path] = []
    seen: set[str] = set()
    for path in ([log_path] if log_path else []) + extra_log_paths + bundle_paths:
        key = str(path)
        if key not in seen:
            seen.add(key)
            merged.append(path)
    return merged


def classify_bundle_file(path: Path) -> list[str]:
    return summary_classify_bundle_file(BUNDLE_FILE_GROUPS, path)


def classify_bundle_files(paths: list[Path]) -> list[str]:
    return summary_classify_bundle_files(BUNDLE_FILE_GROUPS, paths)


def validate_bundle_file_requirements(
    result: EvidenceResult,
    source_paths: list[Path],
    required_bundle_files: list[str],
) -> None:
    result.present_bundle_files = classify_bundle_files(source_paths)
    result.required_bundle_files = [item.lower() for item in required_bundle_files]
    result.missing_bundle_files = []
    for group in result.required_bundle_files:
        if group not in BUNDLE_FILE_GROUPS:
            result.errors.append(f"invalid bundle file requirement: {group}")
        elif group not in result.present_bundle_files:
            result.missing_bundle_files.append(group)
            result.errors.append(f"bundle file requirement missing: {group}")


def canonical_next_slice(value: str) -> str:
    key = value.strip().lower()
    return NEXT_SLICE_ALIASES.get(key, value.strip())


def canonical_focus(value: str) -> str:
    key = value.strip().lower()
    return FOCUS_ALIASES.get(key, value.strip())


def next_slice_code(value: str) -> str:
    return stable_code_for_value(NEXT_SLICE_ALIASES, value)


def failure_code(value: str) -> str:
    return stable_code_for_value(FOCUS_ALIASES, value)


def validate_expectations(
    result: EvidenceResult,
    expect_next_slice: str,
    expect_failure_focus: str,
) -> None:
    was_ok = result.ok()
    if expect_next_slice:
        expected = canonical_next_slice(expect_next_slice)
        result.expected_next_slice = expected
        if result.next_slice != expected:
            result.errors.append(
                f"expected next slice mismatch: expected={expected} actual={result.next_slice}"
            )
    if expect_failure_focus:
        expected = canonical_focus(expect_failure_focus)
        result.expected_failure_focus = expected
        if result.failure_focus != expected:
            result.errors.append(
                f"expected focus mismatch: expected={expected} actual={result.failure_focus}"
            )
    if was_ok and not result.ok():
        result.failure_focus = "expectation mismatch"


def stage_rank(stage: str) -> int | None:
    return STAGE_ORDER.get(stage.lower())


def validate_min_stage(
    result: EvidenceResult,
    field: str,
    required_stage: str,
    observed_values: list[str],
) -> None:
    if not required_stage:
        return
    required_rank = stage_rank(required_stage)
    result.minimum_requirements[field] = required_stage.lower()
    if required_rank is None or required_stage.lower() == "failed":
        result.errors.append(f"invalid minimum {field} stage: {required_stage}")
        return
    if not observed_values:
        result.errors.append(f"missing {field}=<stage> evidence for minimum check")
        return
    observed = observed_values[-1].lower()
    observed_rank = stage_rank(observed)
    if observed_rank is None:
        result.errors.append(f"unknown observed {field} stage: {observed}")
        return
    if observed_rank < required_rank:
        result.errors.append(
            f"{field} below minimum: observed={observed} required={required_stage.lower()}"
        )


def validate_gate_profile(text: str, result: EvidenceResult, gate_profile: str) -> None:
    profile = gate_profile.lower()
    low = text.lower()
    if profile not in GATE_PROFILES:
        result.errors.append(f"invalid gate profile: {gate_profile}")
        return
    if profile == "baseline":
        return
    if profile == "net-ready":
        if "netvsc" not in low:
            result.errors.append("net-ready profile requires NetVSC evidence")
        if "runtime=ready" not in low and "stage=ready" not in low:
            result.errors.append("net-ready profile requires ready network runtime")
        if (
            "runtime=degraded" in low
            or "runtime=down" in low
            or "runtime=unsupported" in low
        ):
            result.errors.append("net-ready profile rejects degraded network runtime")
        return
    if profile == "storage-persistent":
        if "ramdisk" in low or "fallback=ramdisk" in low:
            result.errors.append("storage-persistent profile rejects ramdisk fallback")
        if (
            "persistent" not in low
            and "volume=mounted" not in low
            and "storage mounted" not in low
        ):
            result.errors.append("storage-persistent profile requires persistent storage evidence")
        return
    if profile == "input-ready":
        if (
            "input=ready" not in low
            and "keyboard=ready" not in low
            and "hyperv=ready" not in low
            and "vmbus keyboard ready" not in low
        ):
            result.errors.append("input-ready profile requires keyboard/input ready evidence")


def result_summary(result: EvidenceResult, log_path: Path | None = None) -> dict[str, object]:
    return summary_result_summary(
        result,
        BUNDLE_FILE_GROUPS,
        NEXT_SLICE_ALIASES,
        FOCUS_ALIASES,
        log_path,
    )


def recommend_next_slice(text: str, result: EvidenceResult) -> str:
    low = text.lower()
    last_vmbus = result.vmbus_values[-1] if result.vmbus_values else ""
    last_stage = result.stage_values[-1] if result.stage_values else ""

    if result.kernel_load_failure_markers:
        return "boot/kernel load"
    if not result.vmbus_values or last_vmbus in BUS_NOT_READY_STAGES:
        return "boot/VMBus contact"
    if not result.stage_values or last_stage in CHANNEL_NOT_READY_STAGES:
        return "VMBus offers/channel"
    if (
        "input unavailable" in low
        or "keyboard inconsistent" in low
        or "no input" in low
        or "vmbus keyboard deferred" in low
    ):
        return "input promotion"
    if (
        "recovery-storage" not in low
        or "ramdisk" in low
        or "fallback=ramdisk" in low
        or "storage fallback" in low
    ):
        return "StorVSC storage"
    if "netvsc" in low and (
        "runtime=degraded" in low
        or "runtime=down" in low
        or "runtime=unsupported" in low
        or "dhcp=off" in low
    ):
        return "NetVSC network"
    return "input promotion"


def classify_failure_focus(text: str, result: EvidenceResult) -> str:
    low = text.lower()
    if result.ok():
        return "baseline accepted"
    if result.kernel_load_failure_markers:
        return "kernel loader failure"
    if any(marker in low for marker in FAILURE_MARKERS):
        return "kernel panic/fault"
    if "reset" in low and "spontaneous" in low:
        return "spontaneous reset"
    if not result.vmbus_values:
        return "missing VMBus diagnostics"
    if not result.stage_values:
        return "missing runtime stage diagnostics"
    if any(error == "boot/runtime evidence not found" for error in result.errors):
        return "boot/runtime evidence"
    if any(error == "storage/fallback evidence not found" for error in result.errors):
        return "storage/fallback evidence"
    if any(error.startswith("required command output missing") for error in result.errors):
        return "required command evidence"
    if any(error == "inspect-disk evidence not found" for error in result.errors):
        return "disk inspection evidence"
    if any(error.startswith("host preflight") for error in result.errors):
        return "host preflight evidence"
    if any(error.startswith("host serial console") for error in result.errors):
        return "host serial console evidence"
    if any(error.startswith("host network adapter") for error in result.errors):
        return "host network adapter evidence"
    if any(error.startswith("host storage disk") for error in result.errors):
        return "host storage disk evidence"
    if any("bundle file requirement" in error for error in result.errors):
        return "bundle file evidence"
    if any(error.startswith("invalid validation profile") for error in result.errors):
        return "validation profile requirement"
    if any("below minimum" in error for error in result.errors):
        return "minimum stage requirement"
    if any(error.startswith("invalid minimum") for error in result.errors):
        return "minimum stage requirement"
    if any("profile" in error for error in result.errors):
        return "gate profile requirement"
    if any("expected " in error for error in result.errors):
        return "expectation mismatch"
    return result.next_slice
