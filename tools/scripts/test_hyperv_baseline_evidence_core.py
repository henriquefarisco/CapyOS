#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

from hyperv_baseline_evidence_core import (
    classify_bundle_files,
    discover_bundle_logs,
    merge_evidence_paths,
    read_evidence_paths,
    result_summary,
    validate_evidence,
)


BASE_EVIDENCE = """
[vmbus] INITIATE_CONTACT v0x50000
runtime-native show
platform=vmbus vmbus=ready storage-fw=off
net-status
driver=hyperv-netvsc runtime=ready vmbus=ready stage=ready
net-dump-runtime
vmbus=ready stage=ready relid=0x2 conn=0x14
recovery-storage
storage persistent volume=mounted storvsc=stage:ready
"""

INSPECT_EVIDENCE = """
[*] GPT: entries @ LBA 2, count=128, size=128
    GPT[00] BOOT: LBA=2048..4095
[*] Manifest @ LBA 2048: magic=0x43415059 ver=1 entries=2
[*] Kernel (gpt:manifest) rel_lba=16 abs_lba=2064 sec=128
"""

PREFLIGHT_JSON = """
{
  "hyperv_module"  :  "Hyper-V",
  "vm": {
    "generation"  :  2,
    "secure_boot"  :  false,
    "dynamic_memory_enabled"  :  false,
    "network_adapters": [
      {
        "name": "Network Adapter",
        "switch_name": "Default Switch",
        "is_legacy": false
      }
    ],
    "hard_disks": [
      {
        "controller_type": "SCSI",
        "controller_number": 0,
        "controller_location": 0,
        "path": "C:\\\\VMs\\\\CapyOS\\\\disk.vhdx"
      }
    ],
    "com_ports": [
      {
        "number": 1,
        "path": "\\\\.\\pipe\\capyos-hyperv-com1",
        "disconnected": false
      }
    ]
  }
}
"""


def require(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)


def test_accepts_json_preflight() -> None:
    result = validate_evidence(
        BASE_EVIDENCE + INSPECT_EVIDENCE + PREFLIGHT_JSON,
        require_inspect_disk=True,
        require_host_preflight=True,
        gate_profile="net-ready",
        strict_commands=True,
        min_runtime_stage="ready",
    )
    require(result.ok(), "json preflight evidence should be accepted")
    require(result.host_preflight_present, "json preflight should be detected")
    require(all(result.host_preflight_checks.values()), "json preflight checks should pass")
    require(result.failure_focus == "baseline accepted", "accepted focus should be stable")


def test_rejects_incomplete_json_preflight() -> None:
    incomplete = json.dumps({"hyperv_module": "Hyper-V", "vm": {"generation": 2}})
    result = validate_evidence(
        BASE_EVIDENCE + INSPECT_EVIDENCE + incomplete,
        require_host_preflight=True,
    )
    require(not result.ok(), "incomplete preflight should be rejected")
    require(result.failure_focus == "host preflight evidence", "preflight failure focus should be stable")
    require(
        "host preflight check missing: secure_boot_off" in result.errors,
        "secure boot preflight check should be required",
    )
    summary = result_summary(result)
    require(summary["failure_code"] == "host-preflight-evidence", "preflight failure code should be stable")
    require(summary["next_slice_code"] == "input-promotion", "preflight next slice code should be stable")


def test_expectation_aliases() -> None:
    result = validate_evidence(
        BASE_EVIDENCE,
        expect_next_slice="input-promotion",
        expect_failure_focus="baseline-accepted",
    )
    require(result.ok(), "matching expectation aliases should pass")
    require(result.expected_next_slice == "input promotion", "next alias should canonicalize")
    require(result.expected_failure_focus == "baseline accepted", "focus alias should canonicalize")

    result = validate_evidence(BASE_EVIDENCE, expect_next_slice="storvsc-storage")
    require(not result.ok(), "mismatched expectation should fail")
    require(result.failure_focus == "expectation mismatch", "expectation mismatch focus should be stable")


def test_kernel_loader_failure_detection() -> None:
    text = BASE_EVIDENCE + INSPECT_EVIDENCE + """
[boot] failed to load kernel from installed disk
kernel (gpt:manifest) missing
"""
    result = validate_evidence(text)
    require(not result.ok(), "kernel load failure should be rejected")
    require(
        result.kernel_load_failure_markers == [
            "kernel-load-failed",
            "kernel-manifest-entry-missing",
        ],
        "kernel load failure markers should be structured",
    )
    require(
        result.kernel_load_failure_details
        == [
            {
                "marker": "kernel-load-failed",
                "line": 17,
                "source": "",
                "source_name": "",
                "source_line": 17,
                "snippet": "[boot] failed to load kernel from installed disk",
            },
            {
                "marker": "kernel-manifest-entry-missing",
                "line": 18,
                "source": "",
                "source_name": "",
                "source_line": 18,
                "snippet": "kernel (gpt:manifest) missing",
            },
        ],
        "kernel load failure details should include line snippets",
    )
    require(result.next_slice == "boot/kernel load", "kernel load failure should recommend boot/kernel load")
    require(result.failure_focus == "kernel loader failure", "kernel load failure focus should be stable")

    summary = result_summary(result)
    require(len(summary["summary_schema_features_sha256"]) == 64, "summary schema features digest should be sha256 hex")
    require(
        summary["kernel_load_failure_markers"] == [
            "kernel-load-failed",
            "kernel-manifest-entry-missing",
        ],
        "summary should include kernel load failure markers",
    )
    require(
        summary["kernel_load_failure_details"] == result.kernel_load_failure_details,
        "summary should include kernel load failure details",
    )
    require(
        summary["kernel_load_failure_refs"]
        == [
            {
                "marker": "kernel-load-failed",
                "source_name": "",
                "source_line": 17,
                "snippet": "[boot] failed to load kernel from installed disk",
            },
            {
                "marker": "kernel-manifest-entry-missing",
                "source_name": "",
                "source_line": 18,
                "snippet": "kernel (gpt:manifest) missing",
            },
        ],
        "summary should include portable kernel load failure refs",
    )
    require(
        summary["kernel_load_failure_primary_ref"]
        == {
            "marker": "kernel-load-failed",
            "source_name": "",
            "source_line": 17,
            "snippet": "[boot] failed to load kernel from installed disk",
        },
        "summary should include portable primary kernel load failure ref",
    )
    require(len(summary["kernel_load_failure_refs_sha256"]) == 64, "kernel load refs digest should be stable hex")
    require(
        summary["kernel_load_failure_summary"]["markers"]
        == ["kernel-load-failed", "kernel-manifest-entry-missing"],
        "summary should aggregate kernel load failure markers",
    )
    require(
        summary["kernel_load_failure_summary"]["first_marker"] == "kernel-load-failed",
        "summary should report first kernel load failure marker",
    )
    require(
        len(summary["kernel_load_failure_summary_sha256"]) == 64,
        "kernel load summary digest should be sha256 hex",
    )
    require(
        summary["kernel_load_failure_triage"]["stable_actions"]
        == [
            "review-serial-kernel-loader-lines",
            "review-inspect-disk-kernel-manifest",
        ],
        "summary should include kernel load failure triage actions",
    )
    require(
        len(summary["kernel_load_failure_triage_sha256"]) == 64,
        "kernel load triage digest should be sha256 hex",
    )
    require(
        summary["kernel_load_failure_triage"]["recommended_evidence_status"]["serial"]["present"] is False,
        "kernel load triage should expose missing serial source when no bundle path is present",
    )
    require(
        summary["kernel_load_failure_triage"]["missing_recommended_evidence"] == ["serial", "inspect-disk"],
        "kernel load triage should list missing recommended evidence",
    )
    require(
        summary["kernel_load_failure_triage"]["blocking_reasons"]
        == [
            "recommended-evidence-missing:serial",
            "recommended-evidence-missing:inspect-disk",
        ],
        "kernel load triage should list missing evidence blockers",
    )
    require(summary["next_slice_code"] == "boot-kernel-load", "kernel load next code should be stable")
    require(summary["failure_code"] == "kernel-loader-failure", "kernel load failure code should be stable")
    require(
        summary["ci_acceptance_reasons"][:5]
        == [
            "validator-not-accepted",
            "kernel-loader:kernel-load-failed",
            "kernel-loader:kernel-manifest-entry-missing",
            "kernel-loader-triage:recommended-evidence-missing:serial",
            "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
        ],
        "summary CI reasons should include kernel loader markers and triage blockers",
    )
    require(
        summary["ci_acceptance_primary_reason"]
        == {
            "reason": "kernel-loader:kernel-load-failed",
            "category": "kernel_loader",
            "actionable": True,
        },
        "summary CI primary reason should report actionable kernel loader marker",
    )
    require(
        len(summary["ci_acceptance_primary_reason_sha256"]) == 64,
        "summary CI primary reason digest should be sha256 hex",
    )
    require(
        summary["ci_acceptance_primary_action"]
        == {
            "action": "review-serial-kernel-loader-lines",
            "reason": "kernel-loader:kernel-load-failed",
            "category": "kernel_loader",
            "host_only": True,
        },
        "summary CI primary action should map actionable kernel loader marker",
    )
    require(
        len(summary["ci_acceptance_primary_action_sha256"]) == 64,
        "summary CI primary action digest should be sha256 hex",
    )
    require(
        [item["action"] for item in summary["ci_acceptance_actions"][:3]]
        == [
            "review-validator-errors",
            "review-serial-kernel-loader-lines",
            "review-inspect-disk-kernel-manifest",
        ],
        "summary CI actions should include stable action list",
    )
    require(len(summary["ci_acceptance_reasons_sha256"]) == 64, "summary CI reasons digest should be sha256 hex")
    require(len(summary["ci_acceptance_actions_sha256"]) == 64, "summary CI actions digest should be sha256 hex")
    require(
        len(summary["ci_acceptance_action_names_sha256"]) == 64,
        "summary CI action names digest should be sha256 hex",
    )
    require(
        summary["ci_acceptance_action_summary"]["categories"]["kernel_loader"] == 2,
        "summary CI action aggregate should count kernel loader actions",
    )
    require(
        len(summary["ci_acceptance_action_summary_sha256"]) == 64,
        "summary CI action summary digest should be sha256 hex",
    )
    require(
        summary["ci_acceptance_reason_summary"]["categories"]["kernel_loader"] == 2,
        "summary CI reason aggregate should count kernel loader reasons",
    )
    require(
        summary["ci_acceptance_reason_summary"]["categories"]["kernel_loader_triage"] == 2,
        "summary CI reason aggregate should count kernel loader triage blockers",
    )
    require(
        len(summary["ci_acceptance_reason_summary_sha256"]) == 64,
        "summary CI reason summary digest should be sha256 hex",
    )

    with tempfile.TemporaryDirectory() as tmpdir:
        serial = Path(tmpdir) / "serial.log"
        serial.write_text(text, encoding="utf-8")
        result = validate_evidence(read_evidence_paths([serial]), source_paths=[serial])
        detail = result.kernel_load_failure_details[0]
        require(detail["source"] == str(serial), "kernel load detail should include source path")
        require(detail["source_name"] == "serial.log", "kernel load detail should include source name")
        require(detail["source_line"] == 17, "kernel load detail should include source-local line")


def test_bundle_discovery_and_merge() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial = tmp / "serial.log"
        commands = tmp / "commands.log"
        inspect = tmp / "inspect-disk.log"
        preflight = tmp / "hyperv-preflight.json"
        serial.write_text(BASE_EVIDENCE, encoding="utf-8")
        commands.write_text("net-dump-runtime\nvmbus=ready stage=ready\n", encoding="utf-8")
        inspect.write_text(INSPECT_EVIDENCE, encoding="utf-8")
        preflight.write_text(PREFLIGHT_JSON, encoding="utf-8")

        discovered = discover_bundle_logs(tmp)
        require(discovered == [serial, commands, inspect, preflight], "bundle order should be stable")
        merged = merge_evidence_paths(serial, [commands], discovered)
        require(merged == [serial, commands, inspect, preflight], "merge should deduplicate paths")

        text = read_evidence_paths(merged)
        result = validate_evidence(
            text,
            require_inspect_disk=True,
            require_host_preflight=True,
            source_paths=merged,
            required_bundle_files=["serial", "commands", "inspect-disk", "preflight"],
        )
        summary = result_summary(result, merged[0])
        require(summary["ok"] is True, "bundle summary should be accepted")
        require(summary["source_logs"] == [str(path) for path in merged], "source logs should be preserved")
        require(summary["failure_code"] == "baseline-accepted", "accepted bundle failure code should be stable")
        require(summary["next_slice_code"] == "input-promotion", "accepted bundle next code should be stable")
        require(
            classify_bundle_files(merged) == ["serial", "commands", "inspect-disk", "preflight"],
            "bundle classes should be detected in stable order",
        )
        require(
            summary["present_bundle_files"] == ["serial", "commands", "inspect-disk", "preflight"],
            "bundle summary should include present classes",
        )

        result = validate_evidence(
            read_evidence_paths([serial, commands]),
            source_paths=[serial, commands],
            required_bundle_files=["inspect-disk"],
        )
        require(not result.ok(), "missing required bundle class should fail")
        require(result.failure_focus == "bundle file evidence", "bundle class failure focus should be stable")


def test_host_serial_console_requirement() -> None:
    text_preflight = """
Hyper-V preflight
hyperv_module=Hyper-V
generation=2
secure_boot=False
dynamic_memory_enabled=False
com number=1 path=\\\\.\\pipe\\capyos-hyperv-com1 disconnected=False
"""
    result = validate_evidence(
        BASE_EVIDENCE + text_preflight,
        require_host_preflight=True,
        require_host_serial_console=True,
    )
    require(result.ok(), "text serial console preflight should pass")
    require(result.host_serial_console_present, "text serial console should be detected")

    result = validate_evidence(
        BASE_EVIDENCE + PREFLIGHT_JSON,
        require_host_preflight=True,
        require_host_serial_console=True,
    )
    require(result.ok(), "json serial console preflight should pass")
    require(result.host_serial_console_present, "json serial console should be detected")

    result = validate_evidence(
        BASE_EVIDENCE + PREFLIGHT_JSON.replace('"disconnected": false', '"disconnected": true'),
        require_host_preflight=True,
        require_host_serial_console=True,
    )
    require(not result.ok(), "disconnected serial console should fail")
    require(
        result.failure_focus == "host serial console evidence",
        "serial console failure focus should be stable",
    )

    ambiguous_json = """
{
  "hyperv_module": "Hyper-V",
  "vm": {
    "generation": 2,
    "secure_boot": false,
    "dynamic_memory_enabled": false,
    "com_ports": [
      {
        "number": 1,
        "path": "\\\\.\\pipe\\capyos-hyperv-com1",
        "disconnected": true
      },
      {
        "number": 2,
        "path": "",
        "disconnected": false
      }
    ]
  }
}
"""
    result = validate_evidence(
        BASE_EVIDENCE + ambiguous_json,
        require_host_preflight=True,
        require_host_serial_console=True,
    )
    require(not result.ok(), "serial path and connected state must be in the same COM object")
    require(
        result.failure_focus == "host serial console evidence",
        "ambiguous serial console failure focus should be stable",
    )


def test_host_network_adapter_requirement() -> None:
    text_preflight = """
Hyper-V preflight
hyperv_module=Hyper-V
generation=2
secure_boot=False
dynamic_memory_enabled=False
net name=Network Adapter switch=DefaultSwitch status=Ok mac=00155D010203 legacy=False
"""
    result = validate_evidence(
        BASE_EVIDENCE + text_preflight,
        require_host_preflight=True,
        require_host_network_adapter=True,
    )
    require(result.ok(), "text synthetic network adapter should pass")
    require(result.host_network_adapter_present, "text synthetic network adapter should be detected")

    result = validate_evidence(
        BASE_EVIDENCE + PREFLIGHT_JSON,
        require_host_preflight=True,
        require_host_network_adapter=True,
    )
    require(result.ok(), "json synthetic network adapter should pass")
    require(result.host_network_adapter_present, "json synthetic network adapter should be detected")

    legacy_json = PREFLIGHT_JSON.replace('"is_legacy": false', '"is_legacy": true')
    result = validate_evidence(
        BASE_EVIDENCE + legacy_json,
        require_host_preflight=True,
        require_host_network_adapter=True,
    )
    require(not result.ok(), "legacy network adapter should fail")
    require(
        result.failure_focus == "host network adapter evidence",
        "network adapter failure focus should be stable",
    )

    no_switch_json = PREFLIGHT_JSON.replace('"switch_name": "Default Switch"', '"switch_name": ""')
    result = validate_evidence(
        BASE_EVIDENCE + no_switch_json,
        require_host_preflight=True,
        require_host_network_adapter=True,
    )
    require(not result.ok(), "missing switch name should fail")
    require(
        result.failure_focus == "host network adapter evidence",
        "missing switch failure focus should be stable",
    )


def test_host_storage_disk_requirement() -> None:
    text_preflight = """
Hyper-V preflight
hyperv_module=Hyper-V
generation=2
secure_boot=False
dynamic_memory_enabled=False
disk type=SCSI number=0 location=0 path=C:\\VMs\\CapyOS\\disk.vhdx
"""
    result = validate_evidence(
        BASE_EVIDENCE + text_preflight,
        require_host_preflight=True,
        require_host_storage_disk=True,
    )
    require(result.ok(), "text SCSI disk preflight should pass")
    require(result.host_storage_disk_present, "text SCSI disk should be detected")

    result = validate_evidence(
        BASE_EVIDENCE + PREFLIGHT_JSON,
        require_host_preflight=True,
        require_host_storage_disk=True,
    )
    require(result.ok(), "json SCSI disk preflight should pass")
    require(result.host_storage_disk_present, "json SCSI disk should be detected")

    ide_json = PREFLIGHT_JSON.replace('"controller_type": "SCSI"', '"controller_type": "IDE"')
    result = validate_evidence(
        BASE_EVIDENCE + ide_json,
        require_host_preflight=True,
        require_host_storage_disk=True,
    )
    require(not result.ok(), "non-SCSI disk should fail")
    require(
        result.failure_focus == "host storage disk evidence",
        "storage disk failure focus should be stable",
    )

    no_path_json = PREFLIGHT_JSON.replace('"path": "C:\\\\VMs\\\\CapyOS\\\\disk.vhdx"', '"path": ""')
    result = validate_evidence(
        BASE_EVIDENCE + no_path_json,
        require_host_preflight=True,
        require_host_storage_disk=True,
    )
    require(not result.ok(), "missing disk path should fail")
    require(
        result.failure_focus == "host storage disk evidence",
        "missing disk path focus should be stable",
    )


def test_host_preflight_profile() -> None:
    result = validate_evidence(
        BASE_EVIDENCE + PREFLIGHT_JSON,
        host_preflight_profile="gen2-ready",
    )
    require(result.ok(), "gen2-ready host preflight profile should pass")
    require(result.host_preflight_profile == "gen2-ready", "host preflight profile should be reported")
    require(result.host_serial_console_present, "gen2-ready profile should require serial console")
    require(result.host_network_adapter_present, "gen2-ready profile should require network adapter")
    require(result.host_storage_disk_present, "gen2-ready profile should require storage disk")

    summary = result_summary(result)
    require(summary["host_preflight_profile"] == "gen2-ready", "summary should include host profile")

    no_disk_json = PREFLIGHT_JSON.replace('"controller_type": "SCSI"', '"controller_type": "IDE"')
    result = validate_evidence(
        BASE_EVIDENCE + no_disk_json,
        host_preflight_profile="gen2-ready",
    )
    require(not result.ok(), "gen2-ready profile should reject missing SCSI disk")
    require(
        result.failure_focus == "host storage disk evidence",
        "gen2-ready missing disk focus should be stable",
    )


def test_validation_profile() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial = tmp / "serial.log"
        commands = tmp / "commands.log"
        inspect = tmp / "inspect-disk.log"
        preflight = tmp / "hyperv-preflight.json"
        serial.write_text(BASE_EVIDENCE, encoding="utf-8")
        commands.write_text("net-dump-runtime\nvmbus=ready stage=ready\n", encoding="utf-8")
        inspect.write_text(INSPECT_EVIDENCE, encoding="utf-8")
        preflight.write_text(PREFLIGHT_JSON, encoding="utf-8")
        paths = [serial, commands, inspect, preflight]

        result = validate_evidence(
            read_evidence_paths(paths),
            source_paths=paths,
            validation_profile="hyperv-gen2-ready",
        )
        require(result.ok(), "hyperv-gen2-ready validation profile should pass")
        require(result.strict_commands, "validation profile should enable strict commands")
        require(result.inspect_disk_present, "validation profile should require inspect-disk")
        require(result.host_preflight_profile == "gen2-ready", "validation profile should enable host profile")
        require(
            result.required_bundle_files == ["serial", "commands", "inspect-disk", "preflight"],
            "validation profile should require canonical bundle classes",
        )

        summary = result_summary(result)
        require(summary["validation_profile"] == "hyperv-gen2-ready", "summary should include validation profile")
        require(summary["missing_bundle_files"] == [], "accepted profile should not report missing bundle files")
        require(summary["missing_host_requirements"] == [], "accepted profile should not report missing host requirements")
        require(
            summary["missing_host_preflight_checks"] == [],
            "accepted profile should not report missing preflight checks",
        )
        source_details = summary["source_log_details"]
        require(len(source_details) == 4, "summary should include one detail record per source log")
        require(source_details[0]["path"] == str(serial), "source detail path should preserve order")
        require(source_details[0]["classes"] == ["serial"], "serial source class should be reported")
        require(source_details[1]["classes"] == ["commands"], "commands source class should be reported")
        require(source_details[2]["classes"] == ["inspect-disk"], "inspect source class should be reported")
        require(source_details[3]["classes"] == ["preflight"], "preflight source class should be reported")
        require(source_details[0]["exists"] is True, "source detail should report existing files")
        require(source_details[0]["size_bytes"] == serial.stat().st_size, "source detail size should match stat")
        effective = summary["effective_requirements"]
        require(effective["validation_profile"] == "hyperv-gen2-ready", "effective requirements should include profile")
        require(effective["strict_commands"] is True, "effective requirements should include strict mode")
        require(effective["require_inspect_disk"] is True, "effective requirements should include inspect-disk")
        require(effective["host_preflight_profile"] == "gen2-ready", "effective requirements should include host profile")
        require(
            effective["required_bundle_files"] == ["serial", "commands", "inspect-disk", "preflight"],
            "effective requirements should include bundle classes",
        )
        require(effective["require_host_preflight"] is True, "effective requirements should include host preflight")
        require(
            effective["require_host_serial_console"] is True,
            "effective requirements should include serial console",
        )
        require(
            effective["require_host_network_adapter"] is True,
            "effective requirements should include network adapter",
        )
        require(effective["require_host_storage_disk"] is True, "effective requirements should include storage disk")

        result = validate_evidence(
            read_evidence_paths([serial, commands, preflight]),
            source_paths=[serial, commands, preflight],
            validation_profile="hyperv-gen2-ready",
        )
        require(not result.ok(), "validation profile should reject missing inspect-disk file")
        require(result.missing_bundle_files == ["inspect-disk"], "missing inspect bundle class should be reported")
        require(result.failure_focus == "disk inspection evidence", "missing inspect evidence focus should be stable")

        result = validate_evidence(
            read_evidence_paths([serial, inspect, preflight]),
            source_paths=[serial, inspect, preflight],
            validation_profile="hyperv-gen2-ready",
        )
        require(not result.ok(), "validation profile should reject missing command file")
        require(result.missing_bundle_files == ["commands"], "missing commands bundle class should be reported")
        require(result.failure_focus == "bundle file evidence", "missing command file focus should be stable")

        bad_preflight = PREFLIGHT_JSON.replace('"dynamic_memory_enabled"  :  false', '"dynamic_memory_enabled"  :  true')
        preflight.write_text(bad_preflight, encoding="utf-8")
        result = validate_evidence(
            read_evidence_paths(paths),
            source_paths=paths,
            validation_profile="hyperv-gen2-ready",
        )
        require(not result.ok(), "validation profile should reject incomplete preflight checks")
        require(
            result.missing_host_preflight_checks == ["dynamic_memory_off"],
            "missing host preflight check should be structured",
        )


def main() -> int:
    tests = (
        test_accepts_json_preflight,
        test_rejects_incomplete_json_preflight,
        test_expectation_aliases,
        test_kernel_loader_failure_detection,
        test_bundle_discovery_and_merge,
        test_host_serial_console_requirement,
        test_host_network_adapter_requirement,
        test_host_storage_disk_requirement,
        test_host_preflight_profile,
        test_validation_profile,
    )
    for test in tests:
        test()
    print("[ok] Hyper-V baseline evidence core self-test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        raise SystemExit(1)
