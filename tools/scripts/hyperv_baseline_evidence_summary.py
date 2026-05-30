from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

SUMMARY_SCHEMA = "capyos.hyperv_baseline_evidence.summary"
SUMMARY_SCHEMA_VERSION = 2
SUMMARY_SCHEMA_FEATURES = [
    "summary-schema-features-digest",
    "source-manifest-portable",
    "effective-requirement-summary",
    "ci-acceptance-reasons",
    "ci-acceptance-reasons-digest",
    "ci-acceptance-primary-reason",
    "ci-acceptance-primary-reason-digest",
    "ci-acceptance-primary-action",
    "ci-acceptance-primary-action-digest",
    "ci-acceptance-actions",
    "ci-acceptance-actions-digest",
    "ci-acceptance-action-names-digest",
    "ci-acceptance-action-summary",
    "ci-acceptance-action-summary-digest",
    "ci-acceptance-reason-summary",
    "ci-acceptance-reason-summary-digest",
    "kernel-loader-markers",
    "kernel-loader-source-aware-details",
    "kernel-loader-portable-refs",
    "kernel-loader-primary-ref",
    "kernel-loader-summary-digest",
    "kernel-loader-triage",
    "kernel-loader-triage-digest",
]


def json_sha256(value: object, *, sort_keys: bool = False) -> str:
    payload = json.dumps(
        value,
        sort_keys=sort_keys,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def summary_schema_features_sha256(features: list[str]) -> str:
    return json_sha256(features)


CI_ACCEPTANCE_ACTION_BY_REASON = {
    "kernel-loader:kernel-load-failed": "review-serial-kernel-loader-lines",
    "kernel-loader:kernel-manifest-entry-missing": "review-inspect-disk-kernel-manifest",
    "kernel-loader:kernel-image-missing": "verify-installed-kernel-artifact",
    "kernel-loader:kernel-elf-invalid": "verify-kernel-elf-header",
    "missing-requirement:strict_commands": "collect-required-command-evidence",
    "missing-requirement:inspect_disk": "collect-inspect-disk-evidence",
    "missing-requirement:bundle_files": "complete-evidence-bundle",
    "missing-requirement:host_preflight": "collect-host-preflight-evidence",
    "missing-requirement:host_serial_console": "collect-host-serial-console-evidence",
    "missing-requirement:host_network_adapter": "collect-host-network-adapter-evidence",
    "missing-requirement:host_storage_disk": "collect-host-storage-disk-evidence",
    "source-manifest:no-sources": "provide-source-evidence",
    "source-manifest:missing-sources": "restore-missing-source-evidence",
    "source-manifest:unclassified-sources": "rename-or-classify-source-evidence",
    "source-manifest:duplicate-classes": "deduplicate-source-evidence-classes",
    "validator-not-accepted": "review-validator-errors",
}

CI_ACCEPTANCE_ACTION_BY_REASON_PREFIX = {
    "kernel-loader-triage:recommended-evidence-missing:": "collect-recommended-kernel-loader-evidence",
    "kernel-loader-triage:recommended-evidence-duplicate:": "deduplicate-recommended-kernel-loader-evidence",
}


def stable_code_for_value(aliases: dict[str, str], value: str) -> str:
    normalized = value.strip().lower()
    if not normalized:
        return ""
    for code, label in aliases.items():
        if label.lower() == normalized:
            return code
    return re.sub(r"[^a-z0-9]+", "-", normalized).strip("-")


def classify_bundle_file(
    bundle_file_groups: dict[str, tuple[str, ...]],
    path: Path,
) -> list[str]:
    lower_name = path.name.lower()
    return [
        group
        for group, names in bundle_file_groups.items()
        if lower_name in names
    ]


def classify_bundle_files(
    bundle_file_groups: dict[str, tuple[str, ...]],
    paths: list[Path],
) -> list[str]:
    present: list[str] = []
    for group in bundle_file_groups:
        if any(group in classify_bundle_file(bundle_file_groups, path) for path in paths):
            present.append(group)
    return present


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as fp:
            while True:
                chunk = fp.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    except OSError:
        return ""
    return digest.hexdigest()


def source_log_details(
    bundle_file_groups: dict[str, tuple[str, ...]],
    source_logs: list[str],
) -> list[dict[str, object]]:
    details: list[dict[str, object]] = []
    for source in source_logs:
        path = Path(source)
        item: dict[str, object] = {
            "path": source,
            "name": path.name,
            "classes": classify_bundle_file(bundle_file_groups, path),
        }
        try:
            stat = path.stat()
        except OSError:
            item["exists"] = False
            item["size_bytes"] = 0
            item["sha256"] = ""
        else:
            item["exists"] = True
            item["size_bytes"] = stat.st_size
            item["sha256"] = file_sha256(path)
        details.append(item)
    return details


def source_manifest_entries(source_details: list[dict[str, object]]) -> list[dict[str, object]]:
    return [
        {
            "name": item["name"],
            "classes": item["classes"],
            "exists": item["exists"],
            "size_bytes": item["size_bytes"],
            "sha256": item["sha256"],
        }
        for item in source_details
    ]


def source_manifest_sha256(source_details: list[dict[str, object]]) -> str:
    entries = source_manifest_entries(source_details)
    return json_sha256(entries, sort_keys=True)


def source_manifest_stats(source_details: list[dict[str, object]]) -> dict[str, int]:
    existing = [item for item in source_details if item["exists"]]
    return {
        "total_sources": len(source_details),
        "existing_sources": len(existing),
        "missing_sources": len(source_details) - len(existing),
        "total_size_bytes": sum(int(item["size_bytes"]) for item in existing),
    }


def source_manifest_flags(
    source_details: list[dict[str, object]],
    duplicate_classes: dict[str, list[str]],
) -> dict[str, bool]:
    has_sources = bool(source_details)
    has_missing_sources = any(not item["exists"] for item in source_details)
    has_unclassified_sources = any(not item["classes"] for item in source_details)
    return {
        "has_sources": has_sources,
        "all_sources_exist": has_sources and not has_missing_sources,
        "all_sources_classified": has_sources and not has_unclassified_sources,
        "has_missing_sources": has_missing_sources,
        "has_duplicate_classes": bool(duplicate_classes),
        "has_unclassified_sources": has_unclassified_sources,
    }


def source_class_counts(
    bundle_file_groups: dict[str, tuple[str, ...]],
    source_logs: list[str],
) -> dict[str, int]:
    counts = {group: 0 for group in bundle_file_groups}
    for source in source_logs:
        for group in classify_bundle_file(bundle_file_groups, Path(source)):
            counts[group] += 1
    return counts


def duplicate_source_classes(
    bundle_file_groups: dict[str, tuple[str, ...]],
    source_logs: list[str],
) -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = {group: [] for group in bundle_file_groups}
    for source in source_logs:
        for group in classify_bundle_file(bundle_file_groups, Path(source)):
            grouped[group].append(source)
    return {
        group: paths
        for group, paths in grouped.items()
        if len(paths) > 1
    }


def ordered_unique_strings(values: list[str]) -> list[str]:
    seen: dict[str, None] = {}
    for value in values:
        if value:
            seen.setdefault(value, None)
    return list(seen)


def bundle_class_status(
    bundle_file_groups: dict[str, tuple[str, ...]],
    source_logs: list[str],
    required_bundle_files: list[str],
) -> dict[str, dict[str, object]]:
    counts = source_class_counts(bundle_file_groups, source_logs)
    required = {item.lower() for item in required_bundle_files}
    return {
        group: {
            "count": count,
            "present": count > 0,
            "required": group in required,
            "missing": group in required and count == 0,
            "duplicate": count > 1,
        }
        for group, count in counts.items()
    }


def kernel_load_failure_summary(details: list[dict[str, object]]) -> dict[str, object]:
    markers = ordered_unique_strings([str(item["marker"]) for item in details])
    source_names = ordered_unique_strings([
        str(item.get("source_name", ""))
        for item in details
    ])
    first = details[0] if details else {}
    return {
        "has_failure": bool(details),
        "detail_count": len(details),
        "marker_count": len(markers),
        "markers": markers,
        "source_names": source_names,
        "first_marker": str(first.get("marker", "")),
        "first_line": int(first.get("line", 0) or 0),
        "first_source": str(first.get("source", "")),
        "first_source_name": str(first.get("source_name", "")),
        "first_source_line": int(first.get("source_line", 0) or 0),
    }


def kernel_load_failure_summary_sha256(summary: dict[str, object]) -> str:
    return json_sha256(summary, sort_keys=True)


def kernel_load_failure_refs(details: list[dict[str, object]]) -> list[dict[str, object]]:
    return [
        {
            "marker": str(item.get("marker", "")),
            "source_name": str(item.get("source_name", "")),
            "source_line": int(item.get("source_line", 0) or 0),
            "snippet": str(item.get("snippet", "")),
        }
        for item in details
    ]


def kernel_load_failure_refs_sha256(details: list[dict[str, object]]) -> str:
    return json_sha256(kernel_load_failure_refs(details), sort_keys=True)


def kernel_load_failure_primary_ref(details: list[dict[str, object]]) -> dict[str, object]:
    refs = kernel_load_failure_refs(details)
    if refs:
        return refs[0]
    return {
        "marker": "",
        "source_name": "",
        "source_line": 0,
        "snippet": "",
    }


def kernel_load_failure_triage(
    summary: dict[str, object],
    class_status: dict[str, dict[str, object]] | None = None,
) -> dict[str, object]:
    markers = list(summary["markers"])
    if not markers:
        return {
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
        }
    focus_by_marker = {
        "kernel-manifest-entry-missing": "inspect-disk-manifest",
        "kernel-image-missing": "installed-kernel-artifact",
        "kernel-elf-invalid": "kernel-image-format",
        "kernel-load-failed": "kernel-loader-path",
    }
    evidence_by_marker = {
        "kernel-manifest-entry-missing": ["serial", "inspect-disk"],
        "kernel-image-missing": ["serial", "inspect-disk"],
        "kernel-elf-invalid": ["serial", "inspect-disk"],
        "kernel-load-failed": ["serial"],
    }
    action_by_marker = {
        "kernel-manifest-entry-missing": "review-inspect-disk-kernel-manifest",
        "kernel-image-missing": "verify-installed-kernel-artifact",
        "kernel-elf-invalid": "verify-kernel-elf-header",
        "kernel-load-failed": "review-serial-kernel-loader-lines",
    }
    primary = markers[0]
    evidence = ordered_unique_strings([
        item
        for marker in markers
        for item in evidence_by_marker.get(marker, ["serial"])
    ])
    actions = ordered_unique_strings([
        action_by_marker.get(marker, "collect-loader-debug-lines")
        for marker in markers
    ])
    evidence_status: dict[str, dict[str, object]] = {}
    missing_evidence: list[str] = []
    duplicate_evidence: list[str] = []
    if class_status is not None:
        for item in evidence:
            status = class_status.get(item, {
                "count": 0,
                "present": False,
                "required": False,
                "missing": True,
                "duplicate": False,
            })
            evidence_status[item] = dict(status)
            if not bool(status.get("present")):
                missing_evidence.append(item)
            if bool(status.get("duplicate")):
                duplicate_evidence.append(item)
    blocking_reasons = [
        f"recommended-evidence-missing:{item}"
        for item in missing_evidence
    ] + [
        f"recommended-evidence-duplicate:{item}"
        for item in duplicate_evidence
    ]
    return {
        "active": True,
        "primary_marker": primary,
        "primary_focus": focus_by_marker.get(primary, "kernel-loader"),
        "recommended_evidence": evidence,
        "recommended_evidence_status": evidence_status,
        "missing_recommended_evidence": missing_evidence,
        "duplicate_recommended_evidence": duplicate_evidence,
        "evidence_complete": not missing_evidence,
        "evidence_unambiguous": not duplicate_evidence,
        "blocking_reasons": blocking_reasons,
        "stable_actions": actions,
        "host_only": True,
    }


def kernel_load_failure_triage_sha256(triage: dict[str, object]) -> str:
    return json_sha256(triage, sort_keys=True)


def requirement_status(required: bool, satisfied: bool) -> dict[str, bool]:
    return {
        "required": required,
        "satisfied": (not required) or satisfied,
        "missing": required and not satisfied,
    }


def effective_requirement_status(result: object) -> dict[str, object]:
    requirements = result.effective_requirements
    strict_commands = bool(requirements.get("strict_commands"))
    require_inspect_disk = bool(requirements.get("require_inspect_disk"))
    require_host_preflight = bool(requirements.get("require_host_preflight"))
    require_host_serial_console = bool(requirements.get("require_host_serial_console"))
    require_host_network_adapter = bool(requirements.get("require_host_network_adapter"))
    require_host_storage_disk = bool(requirements.get("require_host_storage_disk"))
    required_bundle_files = list(requirements.get("required_bundle_files", []))
    return {
        "strict_commands": requirement_status(
            strict_commands,
            not result.missing_required_commands,
        ),
        "inspect_disk": requirement_status(
            require_inspect_disk,
            result.inspect_disk_present,
        ),
        "bundle_files": {
            **requirement_status(
                bool(required_bundle_files),
                not result.missing_bundle_files,
            ),
            "required_classes": required_bundle_files,
            "missing_classes": result.missing_bundle_files,
        },
        "host_preflight": {
            **requirement_status(
                require_host_preflight,
                result.host_preflight_present and not result.missing_host_preflight_checks,
            ),
            "missing_checks": result.missing_host_preflight_checks,
        },
        "host_serial_console": requirement_status(
            require_host_serial_console,
            result.host_serial_console_present,
        ),
        "host_network_adapter": requirement_status(
            require_host_network_adapter,
            result.host_network_adapter_present,
        ),
        "host_storage_disk": requirement_status(
            require_host_storage_disk,
            result.host_storage_disk_present,
        ),
    }


def effective_requirement_summary(status: dict[str, object]) -> dict[str, object]:
    missing = [
        name
        for name, detail in status.items()
        if isinstance(detail, dict) and bool(detail.get("missing"))
    ]
    return {
        "all_satisfied": not missing,
        "missing_requirements": missing,
    }


def ci_acceptance(
    result: object,
    requirement_summary: dict[str, object],
    manifest_flags: dict[str, bool],
) -> dict[str, bool]:
    requirements_satisfied = bool(requirement_summary["all_satisfied"])
    sources_healthy = (
        manifest_flags["has_sources"]
        and manifest_flags["all_sources_exist"]
        and manifest_flags["all_sources_classified"]
    )
    accepted = result.ok()
    return {
        "accepted": accepted,
        "requirements_satisfied": requirements_satisfied,
        "sources_healthy": sources_healthy,
        "sources_unambiguous": not manifest_flags["has_duplicate_classes"],
        "has_warnings": bool(result.warnings),
        "has_errors": bool(result.errors),
        "ready": accepted and requirements_satisfied and sources_healthy,
    }


def ci_acceptance_reasons(
    acceptance: dict[str, bool],
    requirement_summary: dict[str, object],
    manifest_flags: dict[str, bool],
    kernel_load_failure_markers: list[str] | None = None,
    kernel_load_failure_triage_result: dict[str, object] | None = None,
) -> list[str]:
    reasons: list[str] = []
    if not acceptance["accepted"]:
        reasons.append("validator-not-accepted")
    for marker in kernel_load_failure_markers or []:
        reasons.append(f"kernel-loader:{marker}")
    if kernel_load_failure_triage_result:
        for reason in kernel_load_failure_triage_result.get("blocking_reasons", []):
            reasons.append(f"kernel-loader-triage:{reason}")
    for requirement in requirement_summary["missing_requirements"]:
        reasons.append(f"missing-requirement:{requirement}")
    if not manifest_flags["has_sources"]:
        reasons.append("source-manifest:no-sources")
    if manifest_flags["has_missing_sources"]:
        reasons.append("source-manifest:missing-sources")
    if manifest_flags["has_unclassified_sources"]:
        reasons.append("source-manifest:unclassified-sources")
    if manifest_flags["has_duplicate_classes"]:
        reasons.append("source-manifest:duplicate-classes")
    return reasons


def ci_acceptance_reason_category(reason: str) -> str:
    if reason == "validator-not-accepted":
        return "validator"
    if reason.startswith("kernel-loader-triage:"):
        return "kernel_loader_triage"
    if reason.startswith("kernel-loader:"):
        return "kernel_loader"
    if reason.startswith("missing-requirement:"):
        return "missing_requirement"
    if reason.startswith("source-manifest:"):
        return "source_manifest"
    return "other"


def ci_acceptance_primary_reason(reasons: list[str]) -> dict[str, object]:
    for reason in reasons:
        category = ci_acceptance_reason_category(reason)
        if category != "validator":
            return {
                "reason": reason,
                "category": category,
                "actionable": True,
            }
    if reasons:
        return {
            "reason": reasons[0],
            "category": ci_acceptance_reason_category(reasons[0]),
            "actionable": False,
        }
    return {
        "reason": "",
        "category": "",
        "actionable": False,
    }


def ci_acceptance_primary_action(primary_reason: dict[str, object]) -> dict[str, object]:
    reason = str(primary_reason.get("reason", ""))
    action = CI_ACCEPTANCE_ACTION_BY_REASON.get(reason, "")
    for prefix, prefix_action in CI_ACCEPTANCE_ACTION_BY_REASON_PREFIX.items():
        if reason.startswith(prefix):
            action = prefix_action
            break
    return {
        "action": action,
        "reason": reason,
        "category": str(primary_reason.get("category", "")),
        "host_only": bool(action),
    }


def ci_acceptance_primary_reason_sha256(primary_reason: dict[str, object]) -> str:
    return json_sha256(primary_reason, sort_keys=True)


def ci_acceptance_primary_action_sha256(primary_action: dict[str, object]) -> str:
    return json_sha256(primary_action, sort_keys=True)


def ci_acceptance_actions(reasons: list[str]) -> list[dict[str, object]]:
    actions: list[dict[str, object]] = []
    seen: set[str] = set()
    for reason in reasons:
        item = ci_acceptance_primary_action({
            "reason": reason,
            "category": ci_acceptance_reason_category(reason),
        })
        action = str(item["action"])
        if not action:
            continue
        if action in seen:
            continue
        seen.add(action)
        actions.append(item)
    return actions


def ci_acceptance_actions_sha256(actions: list[dict[str, object]]) -> str:
    return json_sha256(actions, sort_keys=True)


def ci_acceptance_action_names_sha256(actions: list[dict[str, object]]) -> str:
    return json_sha256([str(item.get("action", "")) for item in actions])


def ci_acceptance_action_summary(actions: list[dict[str, object]]) -> dict[str, object]:
    categories = {
        "validator": 0,
        "kernel_loader": 0,
        "kernel_loader_triage": 0,
        "missing_requirement": 0,
        "source_manifest": 0,
        "other": 0,
    }
    action_names: list[str] = []
    for item in actions:
        category = str(item.get("category", ""))
        if category not in categories:
            category = "other"
        categories[category] += 1
        action_names.append(str(item.get("action", "")))
    active_categories = [
        name
        for name, count in categories.items()
        if count
    ]
    return {
        "total": len(actions),
        "has_actions": bool(actions),
        "host_only": bool(actions) and all(bool(item.get("host_only")) for item in actions),
        "categories": categories,
        "active_categories": active_categories,
        "action_names": action_names,
    }


def ci_acceptance_action_summary_sha256(summary: dict[str, object]) -> str:
    return json_sha256(summary, sort_keys=True)


def ci_acceptance_reason_summary(reasons: list[str]) -> dict[str, object]:
    categories = {
        "validator": 0,
        "kernel_loader": 0,
        "kernel_loader_triage": 0,
        "missing_requirement": 0,
        "source_manifest": 0,
        "other": 0,
    }
    for reason in reasons:
        categories[ci_acceptance_reason_category(reason)] += 1
    active_categories = [
        name
        for name, count in categories.items()
        if count
    ]
    return {
        "total": len(reasons),
        "has_reasons": bool(reasons),
        "categories": categories,
        "active_categories": active_categories,
    }


def ci_acceptance_reasons_sha256(reasons: list[str]) -> str:
    return json_sha256(reasons)


def ci_acceptance_reason_summary_sha256(summary: dict[str, object]) -> str:
    return json_sha256(summary, sort_keys=True)


def result_summary(
    result: object,
    bundle_file_groups: dict[str, tuple[str, ...]],
    next_slice_aliases: dict[str, str],
    focus_aliases: dict[str, str],
    log_path: Path | None = None,
) -> dict[str, object]:
    source_logs = result.source_logs if result.source_logs else ([str(log_path)] if log_path else [])
    details = source_log_details(bundle_file_groups, source_logs)
    duplicate_classes = duplicate_source_classes(bundle_file_groups, source_logs)
    manifest_flags = source_manifest_flags(details, duplicate_classes)
    requirement_statuses = effective_requirement_status(result)
    requirement_summary = effective_requirement_summary(requirement_statuses)
    acceptance = ci_acceptance(result, requirement_summary, manifest_flags)
    kernel_failure_summary = kernel_load_failure_summary(result.kernel_load_failure_details)
    class_statuses = bundle_class_status(
        bundle_file_groups,
        source_logs,
        result.required_bundle_files,
    )
    kernel_failure_triage = kernel_load_failure_triage(kernel_failure_summary, class_statuses)
    acceptance_reasons = ci_acceptance_reasons(
        acceptance,
        requirement_summary,
        manifest_flags,
        result.kernel_load_failure_markers,
        kernel_failure_triage,
    )
    primary_reason = ci_acceptance_primary_reason(acceptance_reasons)
    primary_action = ci_acceptance_primary_action(primary_reason)
    acceptance_actions = ci_acceptance_actions(acceptance_reasons)
    acceptance_action_summary = ci_acceptance_action_summary(acceptance_actions)
    acceptance_reason_summary = ci_acceptance_reason_summary(acceptance_reasons)
    schema_features = list(SUMMARY_SCHEMA_FEATURES)
    return {
        "summary_schema": SUMMARY_SCHEMA,
        "summary_schema_version": SUMMARY_SCHEMA_VERSION,
        "summary_schema_features": schema_features,
        "summary_schema_features_sha256": summary_schema_features_sha256(schema_features),
        "ok": result.ok(),
        "source_log": str(log_path) if log_path else "",
        "source_logs": source_logs,
        "source_log_details": details,
        "source_manifest_entries": source_manifest_entries(details),
        "source_manifest_sha256": source_manifest_sha256(details),
        "source_manifest_stats": source_manifest_stats(details),
        "source_manifest_flags": manifest_flags,
        "ci_acceptance": acceptance,
        "ci_acceptance_reasons": acceptance_reasons,
        "ci_acceptance_reasons_sha256": ci_acceptance_reasons_sha256(acceptance_reasons),
        "ci_acceptance_primary_reason": primary_reason,
        "ci_acceptance_primary_reason_sha256": ci_acceptance_primary_reason_sha256(primary_reason),
        "ci_acceptance_primary_action": primary_action,
        "ci_acceptance_primary_action_sha256": ci_acceptance_primary_action_sha256(primary_action),
        "ci_acceptance_actions": acceptance_actions,
        "ci_acceptance_actions_sha256": ci_acceptance_actions_sha256(acceptance_actions),
        "ci_acceptance_action_names_sha256": ci_acceptance_action_names_sha256(acceptance_actions),
        "ci_acceptance_action_summary": acceptance_action_summary,
        "ci_acceptance_action_summary_sha256": ci_acceptance_action_summary_sha256(acceptance_action_summary),
        "ci_acceptance_reason_summary": acceptance_reason_summary,
        "ci_acceptance_reason_summary_sha256": ci_acceptance_reason_summary_sha256(acceptance_reason_summary),
        "source_class_counts": source_class_counts(bundle_file_groups, source_logs),
        "bundle_class_status": class_statuses,
        "duplicate_bundle_classes": list(duplicate_classes),
        "duplicate_source_classes": duplicate_classes,
        "gate_profile": result.gate_profile,
        "strict_commands": result.strict_commands,
        "missing_required_commands": result.missing_required_commands,
        "kernel_load_failure_markers": result.kernel_load_failure_markers,
        "kernel_load_failure_details": result.kernel_load_failure_details,
        "kernel_load_failure_refs": kernel_load_failure_refs(result.kernel_load_failure_details),
        "kernel_load_failure_primary_ref": kernel_load_failure_primary_ref(result.kernel_load_failure_details),
        "kernel_load_failure_refs_sha256": kernel_load_failure_refs_sha256(result.kernel_load_failure_details),
        "kernel_load_failure_summary": kernel_failure_summary,
        "kernel_load_failure_summary_sha256": kernel_load_failure_summary_sha256(kernel_failure_summary),
        "kernel_load_failure_triage": kernel_failure_triage,
        "kernel_load_failure_triage_sha256": kernel_load_failure_triage_sha256(kernel_failure_triage),
        "vmbus": result.vmbus_values,
        "stage": result.stage_values,
        "next_slice": result.next_slice,
        "next_slice_code": stable_code_for_value(next_slice_aliases, result.next_slice),
        "failure_focus": result.failure_focus,
        "failure_code": stable_code_for_value(focus_aliases, result.failure_focus),
        "inspect_disk_present": result.inspect_disk_present,
        "host_preflight_present": result.host_preflight_present,
        "host_preflight_checks": result.host_preflight_checks,
        "missing_host_preflight_checks": result.missing_host_preflight_checks,
        "host_preflight_profile": result.host_preflight_profile,
        "validation_profile": result.validation_profile,
        "effective_requirements": result.effective_requirements,
        "effective_requirement_status": requirement_statuses,
        "effective_requirement_summary": requirement_summary,
        "host_serial_console_present": result.host_serial_console_present,
        "host_network_adapter_present": result.host_network_adapter_present,
        "host_storage_disk_present": result.host_storage_disk_present,
        "missing_host_requirements": result.missing_host_requirements,
        "present_bundle_files": result.present_bundle_files,
        "required_bundle_files": result.required_bundle_files,
        "missing_bundle_files": result.missing_bundle_files,
        "minimum_requirements": result.minimum_requirements,
        "expected_next_slice": result.expected_next_slice,
        "expected_failure_focus": result.expected_failure_focus,
        "warnings": result.warnings,
        "errors": result.errors,
    }


def write_summary(path: Path, summary: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fp:
        json.dump(summary, fp, indent=2, sort_keys=True)
        fp.write("\n")
