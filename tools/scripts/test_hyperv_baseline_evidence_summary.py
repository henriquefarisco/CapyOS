#!/usr/bin/env python3

from __future__ import annotations

import json
import hashlib
import sys
import tempfile
from pathlib import Path

from hyperv_baseline_evidence_summary import (
    bundle_class_status,
    ci_acceptance,
    ci_acceptance_reasons,
    classify_bundle_file,
    classify_bundle_files,
    duplicate_source_classes,
    effective_requirement_summary,
    effective_requirement_status,
    file_sha256,
    kernel_load_failure_primary_ref,
    kernel_load_failure_refs,
    kernel_load_failure_refs_sha256,
    kernel_load_failure_summary,
    kernel_load_failure_triage,
    result_summary,
    source_class_counts,
    source_log_details,
    source_manifest_entries,
    source_manifest_flags,
    source_manifest_stats,
    source_manifest_sha256,
    stable_code_for_value,
    write_summary,
)

BUNDLE_FILE_GROUPS = {
    "serial": ("serial.log", "hyperv-serial.log"),
    "commands": ("commands.log", "guest-commands.log"),
    "inspect-disk": ("inspect-disk.log", "inspect.log"),
    "preflight": ("hyperv-preflight.txt", "hyperv-preflight.json"),
}

NEXT_SLICE_ALIASES = {
    "storvsc-storage": "StorVSC storage",
    "input-promotion": "input promotion",
}

FOCUS_ALIASES = {
    "baseline-accepted": "baseline accepted",
    "host-preflight-evidence": "host preflight evidence",
}


class FakeResult:
    def __init__(self, source_logs: list[str]) -> None:
        self.source_logs = source_logs
        self.gate_profile = "baseline"
        self.strict_commands = True
        self.missing_required_commands: list[str] = []
        self.kernel_load_failure_markers: list[str] = []
        self.kernel_load_failure_details: list[dict[str, object]] = []
        self.vmbus_values = ["offers"]
        self.stage_values = ["offers", "channel"]
        self.next_slice = "StorVSC storage"
        self.failure_focus = "baseline accepted"
        self.inspect_disk_present = True
        self.host_preflight_present = True
        self.host_preflight_checks = {
            "generation_2": True,
            "secure_boot_off": True,
            "dynamic_memory_off": True,
        }
        self.missing_host_preflight_checks: list[str] = []
        self.host_preflight_profile = "gen2-ready"
        self.validation_profile = "hyperv-gen2-ready"
        self.effective_requirements = {
            "validation_profile": "hyperv-gen2-ready",
            "strict_commands": True,
            "require_inspect_disk": True,
            "required_bundle_files": ["serial", "commands", "inspect-disk", "preflight"],
            "host_preflight_profile": "gen2-ready",
            "require_host_preflight": True,
            "require_host_serial_console": True,
            "require_host_network_adapter": True,
            "require_host_storage_disk": True,
        }
        self.host_serial_console_present = True
        self.host_network_adapter_present = True
        self.host_storage_disk_present = True
        self.missing_host_requirements: list[str] = []
        self.present_bundle_files = ["serial", "commands", "inspect-disk", "preflight"]
        self.required_bundle_files = ["serial", "commands", "inspect-disk", "preflight"]
        self.missing_bundle_files: list[str] = []
        self.minimum_requirements: dict[str, str] = {}
        self.expected_next_slice = "StorVSC storage"
        self.expected_failure_focus = "baseline accepted"
        self.warnings: list[str] = []
        self.errors: list[str] = []

    def ok(self) -> bool:
        return not self.errors


def require(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)


def test_stable_codes() -> None:
    require(
        stable_code_for_value(NEXT_SLICE_ALIASES, "StorVSC storage") == "storvsc-storage",
        "known next slice should map to stable code",
    )
    require(
        stable_code_for_value(FOCUS_ALIASES, "custom focus value") == "custom-focus-value",
        "unknown label should slugify to stable code",
    )
    require(stable_code_for_value(FOCUS_ALIASES, "") == "", "empty label should remain empty")


def test_source_details_and_classes() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial = tmp / "serial.log"
        serial_alt = tmp / "hyperv-serial.log"
        commands = tmp / "commands.log"
        missing = tmp / "missing.log"
        serial.write_text("serial evidence\n", encoding="utf-8")
        serial_alt.write_text("alternate serial evidence\n", encoding="utf-8")
        commands.write_text("command evidence\n", encoding="utf-8")

        require(classify_bundle_file(BUNDLE_FILE_GROUPS, serial) == ["serial"], "serial class should match")
        require(
            classify_bundle_files(BUNDLE_FILE_GROUPS, [commands, serial]) == ["serial", "commands"],
            "bundle class order should be canonical",
        )
        require(
            source_class_counts(BUNDLE_FILE_GROUPS, [str(serial), str(serial_alt), str(commands), str(missing)])
            == {"serial": 2, "commands": 1, "inspect-disk": 0, "preflight": 0},
            "source class counts should include zeroes for absent classes",
        )
        require(
            duplicate_source_classes(BUNDLE_FILE_GROUPS, [str(serial), str(serial_alt), str(commands)])
            == {"serial": [str(serial), str(serial_alt)]},
            "duplicate source classes should preserve duplicate paths",
        )
        status = bundle_class_status(
            BUNDLE_FILE_GROUPS,
            [str(serial), str(serial_alt), str(commands), str(missing)],
            ["serial", "inspect-disk"],
        )
        require(
            status["serial"] == {
                "count": 2,
                "present": True,
                "required": True,
                "missing": False,
                "duplicate": True,
            },
            "serial class status should report required duplicate evidence",
        )
        require(
            status["commands"] == {
                "count": 1,
                "present": True,
                "required": False,
                "missing": False,
                "duplicate": False,
            },
            "commands class status should report optional present evidence",
        )
        require(
            status["inspect-disk"] == {
                "count": 0,
                "present": False,
                "required": True,
                "missing": True,
                "duplicate": False,
            },
            "inspect class status should report required missing evidence",
        )

        details = source_log_details(BUNDLE_FILE_GROUPS, [str(serial), str(commands), str(missing)])
        duplicate_classes = duplicate_source_classes(
            BUNDLE_FILE_GROUPS,
            [str(serial), str(serial_alt), str(commands)],
        )
        require(details[0]["classes"] == ["serial"], "serial detail class should match")
        require(details[0]["exists"] is True, "serial detail should exist")
        require(details[0]["size_bytes"] == serial.stat().st_size, "serial size should match stat")
        require(
            details[0]["sha256"] == hashlib.sha256(b"serial evidence\n").hexdigest(),
            "serial detail sha256 should match content",
        )
        require(file_sha256(serial) == details[0]["sha256"], "file_sha256 should match source detail")
        require(details[2]["classes"] == [], "unknown file should have no bundle class")
        require(details[2]["exists"] is False, "missing file should be reported")
        require(details[2]["size_bytes"] == 0, "missing file size should be zero")
        require(details[2]["sha256"] == "", "missing file digest should be empty")
        manifest_digest = source_manifest_sha256(details)
        entries = source_manifest_entries(details)
        require(len(manifest_digest) == 64, "source manifest digest should be sha256 hex")
        require("path" not in entries[0], "source manifest entries should omit absolute paths")
        require(entries[0]["name"] == "serial.log", "source manifest entry should include file name")
        require(entries[0]["classes"] == ["serial"], "source manifest entry should include classes")
        require(
            source_manifest_stats(details)
            == {
                "total_sources": 3,
                "existing_sources": 2,
                "missing_sources": 1,
                "total_size_bytes": serial.stat().st_size + commands.stat().st_size,
            },
            "source manifest stats should summarize existence and sizes",
        )
        require(
            source_manifest_flags(details, duplicate_classes)
            == {
                "has_sources": True,
                "all_sources_exist": False,
                "all_sources_classified": False,
                "has_missing_sources": True,
                "has_duplicate_classes": True,
                "has_unclassified_sources": True,
            },
            "source manifest flags should summarize source health",
        )

        portable_details = [dict(item) for item in details]
        portable_details[0]["path"] = "/different/root/serial.log"
        require(
            source_manifest_sha256(portable_details) == manifest_digest,
            "source manifest digest should ignore absolute paths",
        )

        changed_details = [dict(item) for item in details]
        changed_details[0]["sha256"] = "0" * 64
        require(
            source_manifest_sha256(changed_details) != manifest_digest,
            "source manifest digest should change with content digest",
        )


def test_effective_requirement_status() -> None:
    result = FakeResult([])
    status = effective_requirement_status(result)
    summary = effective_requirement_summary(status)
    flags = {
        "has_sources": True,
        "all_sources_exist": True,
        "all_sources_classified": True,
        "has_missing_sources": False,
        "has_duplicate_classes": False,
        "has_unclassified_sources": False,
    }
    require(
        summary == {"all_satisfied": True, "missing_requirements": []},
        "effective requirement summary should accept satisfied requirements",
    )
    require(
        ci_acceptance(result, summary, flags)
        == {
            "accepted": True,
            "requirements_satisfied": True,
            "sources_healthy": True,
            "sources_unambiguous": True,
            "has_warnings": False,
            "has_errors": False,
            "ready": True,
        },
        "ci acceptance should accept healthy evidence",
    )
    require(
        ci_acceptance_reasons(ci_acceptance(result, summary, flags), summary, flags) == [],
        "ci acceptance reasons should be empty for healthy evidence",
    )
    require(status["strict_commands"] == {"required": True, "satisfied": True, "missing": False}, "strict status")
    require(status["inspect_disk"] == {"required": True, "satisfied": True, "missing": False}, "inspect status")
    require(
        status["bundle_files"]["required_classes"] == ["serial", "commands", "inspect-disk", "preflight"],
        "bundle status should include required classes",
    )
    require(status["bundle_files"]["missing_classes"] == [], "bundle status should include missing classes")
    require(status["host_preflight"]["missing_checks"] == [], "host preflight status should include checks")
    require(
        status["host_network_adapter"] == {"required": True, "satisfied": True, "missing": False},
        "network adapter status should be satisfied",
    )

    result.missing_required_commands = ["net-status"]
    result.inspect_disk_present = False
    result.missing_bundle_files = ["commands"]
    result.missing_host_preflight_checks = ["dynamic_memory_off"]
    result.host_network_adapter_present = False
    result.errors = ["net-status missing"]
    status = effective_requirement_status(result)
    summary = effective_requirement_summary(status)
    require(
        summary == {
            "all_satisfied": False,
            "missing_requirements": [
                "strict_commands",
                "inspect_disk",
                "bundle_files",
                "host_preflight",
                "host_network_adapter",
            ],
        },
        "effective requirement summary should list missing requirements",
    )
    flags["has_duplicate_classes"] = True
    require(
        ci_acceptance(result, summary, flags)
        == {
            "accepted": False,
            "requirements_satisfied": False,
            "sources_healthy": True,
            "sources_unambiguous": False,
            "has_warnings": False,
            "has_errors": True,
            "ready": False,
        },
        "ci acceptance should reject failed evidence",
    )
    require(
        ci_acceptance_reasons(ci_acceptance(result, summary, flags), summary, flags)
        == [
            "validator-not-accepted",
            "missing-requirement:strict_commands",
            "missing-requirement:inspect_disk",
            "missing-requirement:bundle_files",
            "missing-requirement:host_preflight",
            "missing-requirement:host_network_adapter",
            "source-manifest:duplicate-classes",
        ],
        "ci acceptance reasons should list stable rejection reasons",
    )
    require(
        ci_acceptance_reasons(
            ci_acceptance(result, summary, flags),
            summary,
            flags,
            ["kernel-load-failed", "kernel-manifest-entry-missing"],
        )
        == [
            "validator-not-accepted",
            "kernel-loader:kernel-load-failed",
            "kernel-loader:kernel-manifest-entry-missing",
            "missing-requirement:strict_commands",
            "missing-requirement:inspect_disk",
            "missing-requirement:bundle_files",
            "missing-requirement:host_preflight",
            "missing-requirement:host_network_adapter",
            "source-manifest:duplicate-classes",
        ],
        "ci acceptance reasons should include kernel loader markers",
    )
    require(
        ci_acceptance_reasons(
            ci_acceptance(result, summary, flags),
            summary,
            flags,
            ["kernel-load-failed"],
            {"blocking_reasons": ["recommended-evidence-missing:inspect-disk"]},
        )
        == [
            "validator-not-accepted",
            "kernel-loader:kernel-load-failed",
            "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
            "missing-requirement:strict_commands",
            "missing-requirement:inspect_disk",
            "missing-requirement:bundle_files",
            "missing-requirement:host_preflight",
            "missing-requirement:host_network_adapter",
            "source-manifest:duplicate-classes",
        ],
        "ci acceptance reasons should include kernel loader triage blockers",
    )
    require(status["strict_commands"] == {"required": True, "satisfied": False, "missing": True}, "strict missing")
    require(status["inspect_disk"] == {"required": True, "satisfied": False, "missing": True}, "inspect missing")
    require(status["bundle_files"]["missing"] is True, "bundle files should report missing")
    require(status["bundle_files"]["missing_classes"] == ["commands"], "bundle files should list missing class")
    require(status["host_preflight"]["missing"] is True, "preflight should report missing check")
    require(
        status["host_preflight"]["missing_checks"] == ["dynamic_memory_off"],
        "preflight should list missing checks",
    )
    require(
        status["host_network_adapter"] == {"required": True, "satisfied": False, "missing": True},
        "network adapter status should be missing",
    )


def test_kernel_load_failure_summary() -> None:
    details = [
        {
            "marker": "kernel-load-failed",
            "line": 20,
            "source": "/tmp/evidence/serial.log",
            "source_name": "serial.log",
            "source_line": 11,
            "snippet": "[boot] failed to load kernel from installed disk",
        },
        {
            "marker": "kernel-manifest-entry-missing",
            "line": 21,
            "source": "/tmp/evidence/serial.log",
            "source_name": "serial.log",
            "source_line": 12,
            "snippet": "kernel (gpt:manifest) missing",
        },
    ]
    require(
        kernel_load_failure_summary(details)
        == {
            "has_failure": True,
            "detail_count": 2,
            "marker_count": 2,
            "markers": ["kernel-load-failed", "kernel-manifest-entry-missing"],
            "source_names": ["serial.log"],
            "first_marker": "kernel-load-failed",
            "first_line": 20,
            "first_source": "/tmp/evidence/serial.log",
            "first_source_name": "serial.log",
            "first_source_line": 11,
        },
        "kernel load failure summary should aggregate details",
    )
    require(
        kernel_load_failure_refs(details)
        == [
            {
                "marker": "kernel-load-failed",
                "source_name": "serial.log",
                "source_line": 11,
                "snippet": "[boot] failed to load kernel from installed disk",
            },
            {
                "marker": "kernel-manifest-entry-missing",
                "source_name": "serial.log",
                "source_line": 12,
                "snippet": "kernel (gpt:manifest) missing",
            },
        ],
        "kernel load failure refs should omit absolute source paths",
    )
    require(
        kernel_load_failure_primary_ref(details)
        == {
            "marker": "kernel-load-failed",
            "source_name": "serial.log",
            "source_line": 11,
            "snippet": "[boot] failed to load kernel from installed disk",
        },
        "kernel load failure primary ref should expose first portable ref",
    )
    require(
        len(kernel_load_failure_refs_sha256(details)) == 64,
        "kernel load failure refs digest should be sha256 hex",
    )
    portable_details = [dict(item) for item in details]
    portable_details[0]["source"] = "/different/root/serial.log"
    portable_details[1]["source"] = "/different/root/serial.log"
    require(
        kernel_load_failure_refs_sha256(portable_details) == kernel_load_failure_refs_sha256(details),
        "kernel load failure refs digest should ignore absolute source paths",
    )
    summary = kernel_load_failure_summary(details)
    class_status = {
        "serial": {
            "count": 1,
            "present": True,
            "required": True,
            "missing": False,
            "duplicate": False,
        },
        "inspect-disk": {
            "count": 0,
            "present": False,
            "required": True,
            "missing": True,
            "duplicate": False,
        },
    }
    require(
        kernel_load_failure_triage(summary, class_status)
        == {
            "active": True,
            "primary_marker": "kernel-load-failed",
            "primary_focus": "kernel-loader-path",
            "recommended_evidence": ["serial", "inspect-disk"],
            "recommended_evidence_status": class_status,
            "missing_recommended_evidence": ["inspect-disk"],
            "duplicate_recommended_evidence": [],
            "evidence_complete": False,
            "evidence_unambiguous": True,
            "blocking_reasons": ["recommended-evidence-missing:inspect-disk"],
            "stable_actions": [
                "review-serial-kernel-loader-lines",
                "review-inspect-disk-kernel-manifest",
            ],
            "host_only": True,
        },
        "kernel load failure triage should recommend stable host-only actions",
    )
    require(
        kernel_load_failure_summary([])
        == {
            "has_failure": False,
            "detail_count": 0,
            "marker_count": 0,
            "markers": [],
            "source_names": [],
            "first_marker": "",
            "first_line": 0,
            "first_source": "",
            "first_source_name": "",
            "first_source_line": 0,
        },
        "empty kernel load failure summary should be stable",
    )
    require(
        kernel_load_failure_triage(kernel_load_failure_summary([]))
        == {
            "active": False,
            "primary_marker": "",
            "primary_focus": "",
            "recommended_evidence": [],
            "recommended_evidence_status": {},
            "missing_recommended_evidence": [],
            "duplicate_recommended_evidence": [],
            "evidence_complete": True,
            "evidence_unambiguous": True,
            "blocking_reasons": [],
            "stable_actions": [],
            "host_only": True,
        },
        "empty kernel load failure triage should be stable",
    )
    require(
        kernel_load_failure_primary_ref([])
        == {
            "marker": "",
            "source_name": "",
            "source_line": 0,
            "snippet": "",
        },
        "empty kernel load failure primary ref should be stable",
    )


def test_result_summary_and_write() -> None:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial = tmp / "serial.log"
        serial_alt = tmp / "hyperv-serial.log"
        serial.write_text("serial evidence\n", encoding="utf-8")
        serial_alt.write_text("alternate serial evidence\n", encoding="utf-8")
        result = FakeResult([str(serial), str(serial_alt)])
        summary = result_summary(
            result,
            BUNDLE_FILE_GROUPS,
            NEXT_SLICE_ALIASES,
            FOCUS_ALIASES,
            serial,
        )
        require(summary["ok"] is True, "summary should report accepted result")
        require(
            summary["source_class_counts"] == {"serial": 2, "commands": 0, "inspect-disk": 0, "preflight": 0},
            "summary should include source class counts",
        )
        require(summary["bundle_class_status"]["serial"]["duplicate"] is True, "summary should flag duplicate class")
        require(summary["bundle_class_status"]["commands"]["missing"] is True, "summary should flag required missing class")
        require(summary["duplicate_bundle_classes"] == ["serial"], "summary should include duplicate classes")
        require(
            summary["duplicate_source_classes"] == {"serial": [str(serial), str(serial_alt)]},
            "summary should include duplicate source paths",
        )
        require(summary["next_slice_code"] == "storvsc-storage", "summary should include next code")
        require(summary["failure_code"] == "baseline-accepted", "summary should include failure code")
        require(summary["source_log_details"][0]["classes"] == ["serial"], "summary should include source details")
        require(
            summary["kernel_load_failure_summary"]["has_failure"] is False,
            "summary should include empty kernel load failure summary",
        )
        require(summary["kernel_load_failure_refs"] == [], "summary should include empty kernel load failure refs")
        require(
            summary["kernel_load_failure_primary_ref"]
            == {
                "marker": "",
                "source_name": "",
                "source_line": 0,
                "snippet": "",
            },
            "summary should include empty kernel load failure primary ref",
        )
        require(
            summary["kernel_load_failure_triage"]["active"] is False,
            "summary should include inactive kernel load failure triage",
        )
        require(
            summary["kernel_load_failure_triage"]["recommended_evidence_status"] == {},
            "inactive triage should include empty recommended evidence status",
        )
        require(
            summary["effective_requirement_status"]["inspect_disk"]
            == {"required": True, "satisfied": True, "missing": False},
            "summary should include effective requirement status",
        )
        require(
            summary["effective_requirement_summary"] == {"all_satisfied": True, "missing_requirements": []},
            "summary should include effective requirement aggregate",
        )
        require(
            summary["ci_acceptance"]
            == {
                "accepted": True,
                "requirements_satisfied": True,
                "sources_healthy": True,
                "sources_unambiguous": False,
                "has_warnings": False,
                "has_errors": False,
                "ready": True,
            },
            "summary should include ci acceptance",
        )
        require(
            summary["ci_acceptance_reasons"] == ["source-manifest:duplicate-classes"],
            "summary should include ci acceptance reasons",
        )
        require(
            summary["ci_acceptance_primary_reason"]
            == {
                "reason": "source-manifest:duplicate-classes",
                "category": "source_manifest",
                "actionable": True,
            },
            "summary should include ci acceptance primary reason",
        )
        require(
            summary["ci_acceptance_primary_action"]
            == {
                "action": "deduplicate-source-evidence-classes",
                "reason": "source-manifest:duplicate-classes",
                "category": "source_manifest",
                "host_only": True,
            },
            "summary should include ci acceptance primary action",
        )
        require(
            summary["ci_acceptance_actions"] == [summary["ci_acceptance_primary_action"]],
            "summary should include ci acceptance action list",
        )
        require(
            summary["ci_acceptance_action_summary"]["action_names"] == ["deduplicate-source-evidence-classes"],
            "summary should include ci acceptance action summary",
        )
        require(
            summary["ci_acceptance_reason_summary"]
            == {
                "total": 1,
                "has_reasons": True,
                "categories": {
                    "validator": 0,
                    "kernel_loader": 0,
                    "kernel_loader_triage": 0,
                    "missing_requirement": 0,
                    "source_manifest": 1,
                    "other": 0,
                },
                "active_categories": ["source_manifest"],
            },
            "summary should include ci acceptance reason summary",
        )
        require(
            summary["source_log_details"][0]["sha256"] == hashlib.sha256(b"serial evidence\n").hexdigest(),
            "summary should include source digest",
        )
        require(len(summary["source_manifest_sha256"]) == 64, "summary should include manifest digest")
        require(
            summary["source_manifest_sha256"] == source_manifest_sha256(summary["source_log_details"]),
            "summary manifest digest should match source details",
        )
        require(
            summary["source_manifest_entries"] == source_manifest_entries(summary["source_log_details"]),
            "summary should include portable manifest entries",
        )
        require("path" not in summary["source_manifest_entries"][0], "portable manifest entries should omit paths")
        require(
            summary["source_manifest_stats"]
            == {
                "total_sources": 2,
                "existing_sources": 2,
                "missing_sources": 0,
                "total_size_bytes": serial.stat().st_size + serial_alt.stat().st_size,
            },
            "summary should include source manifest stats",
        )
        require(
            summary["source_manifest_flags"]
            == {
                "has_sources": True,
                "all_sources_exist": True,
                "all_sources_classified": True,
                "has_missing_sources": False,
                "has_duplicate_classes": True,
                "has_unclassified_sources": False,
            },
            "summary should include source manifest flags",
        )

        output = tmp / "summary.json"
        write_summary(output, summary)
        written = json.loads(output.read_text(encoding="utf-8"))
        require(written["failure_code"] == "baseline-accepted", "written summary should round trip")


def main() -> int:
    tests = (
        test_stable_codes,
        test_source_details_and_classes,
        test_effective_requirement_status,
        test_kernel_load_failure_summary,
        test_result_summary_and_write,
    )
    for test in tests:
        test()
    print("[ok] Hyper-V baseline evidence summary self-test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        raise SystemExit(1)
