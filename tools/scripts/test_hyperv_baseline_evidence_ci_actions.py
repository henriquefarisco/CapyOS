#!/usr/bin/env python3

from __future__ import annotations

import sys

from hyperv_baseline_evidence_summary import (
    CI_ACCEPTANCE_ACTION_BY_REASON,
    CI_ACCEPTANCE_ACTION_BY_REASON_PREFIX,
    ci_acceptance_action_summary,
    ci_acceptance_action_summary_sha256,
    ci_acceptance_action_names_sha256,
    ci_acceptance_actions,
    ci_acceptance_actions_sha256,
    ci_acceptance_primary_action_sha256,
    ci_acceptance_primary_action,
    ci_acceptance_primary_reason,
    ci_acceptance_primary_reason_sha256,
    ci_acceptance_reasons_sha256,
    ci_acceptance_reason_summary,
    ci_acceptance_reason_summary_sha256,
    json_sha256,
)


def require(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)


def test_json_digest_helper() -> None:
    require(
        json_sha256(["b", "a"]) == ci_acceptance_reasons_sha256(["b", "a"]),
        "json digest helper should preserve list order",
    )
    require(
        json_sha256({"b": 1, "a": 2}, sort_keys=True)
        == json_sha256({"a": 2, "b": 1}, sort_keys=True),
        "json digest helper should support sorted object keys",
    )


def test_reason_summary() -> None:
    reasons = [
        "validator-not-accepted",
        "kernel-loader:kernel-load-failed",
        "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
        "missing-requirement:inspect_disk",
        "source-manifest:duplicate-classes",
        "custom-reason",
    ]
    summary = ci_acceptance_reason_summary(reasons)
    require(
        summary == {
            "total": 6,
            "has_reasons": True,
            "categories": {
                "validator": 1,
                "kernel_loader": 1,
                "kernel_loader_triage": 1,
                "missing_requirement": 1,
                "source_manifest": 1,
                "other": 1,
            },
            "active_categories": [
                "validator",
                "kernel_loader",
                "kernel_loader_triage",
                "missing_requirement",
                "source_manifest",
                "other",
            ],
        },
        "ci acceptance reason summary should count stable categories",
    )
    require(len(ci_acceptance_reason_summary_sha256(summary)) == 64, "ci acceptance reason summary digest should be sha256 hex")
    require(
        ci_acceptance_reason_summary_sha256(dict(summary)) == ci_acceptance_reason_summary_sha256(summary),
        "ci acceptance reason summary digest should be stable for equivalent summaries",
    )
    require(len(ci_acceptance_reasons_sha256(reasons)) == 64, "ci acceptance reasons digest should be sha256 hex")
    require(
        ci_acceptance_reasons_sha256(list(reasons)) == ci_acceptance_reasons_sha256(reasons),
        "ci acceptance reasons digest should be stable for equivalent reason lists",
    )


def test_primary_reason() -> None:
    primary = ci_acceptance_primary_reason([
        "validator-not-accepted",
        "kernel-loader:kernel-load-failed",
        "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
    ])
    require(
        primary == {
            "reason": "kernel-loader:kernel-load-failed",
            "category": "kernel_loader",
            "actionable": True,
        },
        "ci acceptance primary reason should skip generic validator reason",
    )
    require(len(ci_acceptance_primary_reason_sha256(primary)) == 64, "ci acceptance primary reason digest should be sha256 hex")
    require(
        ci_acceptance_primary_reason(["validator-not-accepted"])
        == {
            "reason": "validator-not-accepted",
            "category": "validator",
            "actionable": False,
        },
        "ci acceptance primary reason should fall back to validator when no actionable reason exists",
    )
    require(
        ci_acceptance_primary_reason([])
        == {
            "reason": "",
            "category": "",
            "actionable": False,
        },
        "empty ci acceptance primary reason should be stable",
    )


def test_primary_action() -> None:
    require(
        CI_ACCEPTANCE_ACTION_BY_REASON["kernel-loader:kernel-load-failed"]
        == "review-serial-kernel-loader-lines",
        "ci action table should include kernel load failure",
    )
    require(
        CI_ACCEPTANCE_ACTION_BY_REASON_PREFIX["kernel-loader-triage:recommended-evidence-missing:"]
        == "collect-recommended-kernel-loader-evidence",
        "ci action prefix table should include missing evidence triage",
    )
    primary = ci_acceptance_primary_action({
        "reason": "kernel-loader:kernel-load-failed",
        "category": "kernel_loader",
        "actionable": True,
    })
    require(
        primary == {
            "action": "review-serial-kernel-loader-lines",
            "reason": "kernel-loader:kernel-load-failed",
            "category": "kernel_loader",
            "host_only": True,
        },
        "ci acceptance primary action should map kernel loader reason",
    )
    require(len(ci_acceptance_primary_action_sha256(primary)) == 64, "ci acceptance primary action digest should be sha256 hex")
    require(
        ci_acceptance_primary_action({
            "reason": "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
            "category": "kernel_loader_triage",
            "actionable": True,
        })["action"] == "collect-recommended-kernel-loader-evidence",
        "ci acceptance primary action should map triage missing evidence",
    )


def test_action_list() -> None:
    actions = ci_acceptance_actions([
        "kernel-loader:kernel-load-failed",
        "kernel-loader:kernel-load-failed",
        "kernel-loader-triage:recommended-evidence-missing:serial",
        "kernel-loader-triage:recommended-evidence-missing:inspect-disk",
        "missing-requirement:inspect_disk",
    ])
    require(
        [item["action"] for item in actions]
        == [
            "review-serial-kernel-loader-lines",
            "collect-recommended-kernel-loader-evidence",
            "collect-inspect-disk-evidence",
        ],
        "ci acceptance actions should deduplicate stable actions",
    )
    require(len(ci_acceptance_actions_sha256(actions)) == 64, "ci acceptance action digest should be sha256 hex")
    require(
        ci_acceptance_actions_sha256(actions) == ci_acceptance_actions_sha256(list(actions)),
        "ci acceptance action digest should be stable for equivalent action lists",
    )
    require(len(ci_acceptance_action_names_sha256(actions)) == 64, "ci acceptance action names digest should be sha256 hex")
    renamed_actions = [dict(item) for item in actions]
    renamed_actions[0]["reason"] = "different-reason"
    require(
        ci_acceptance_action_names_sha256(renamed_actions) == ci_acceptance_action_names_sha256(actions),
        "ci acceptance action names digest should ignore non-action metadata",
    )
    summary = ci_acceptance_action_summary(actions)
    require(
        summary == {
            "total": 3,
            "has_actions": True,
            "host_only": True,
            "categories": {
                "validator": 0,
                "kernel_loader": 1,
                "kernel_loader_triage": 1,
                "missing_requirement": 1,
                "source_manifest": 0,
                "other": 0,
            },
            "active_categories": ["kernel_loader", "kernel_loader_triage", "missing_requirement"],
            "action_names": [
                "review-serial-kernel-loader-lines",
                "collect-recommended-kernel-loader-evidence",
                "collect-inspect-disk-evidence",
            ],
        },
        "ci acceptance action summary should aggregate stable action metadata",
    )
    require(len(ci_acceptance_action_summary_sha256(summary)) == 64, "ci acceptance action summary digest should be sha256 hex")
    require(
        ci_acceptance_action_summary_sha256(dict(summary)) == ci_acceptance_action_summary_sha256(summary),
        "ci acceptance action summary digest should be stable for equivalent summaries",
    )
    require(
        ci_acceptance_action_summary([])
        == {
            "total": 0,
            "has_actions": False,
            "host_only": False,
            "categories": {
                "validator": 0,
                "kernel_loader": 0,
                "kernel_loader_triage": 0,
                "missing_requirement": 0,
                "source_manifest": 0,
                "other": 0,
            },
            "active_categories": [],
            "action_names": [],
        },
        "empty ci acceptance action summary should not report host-only work",
    )


def main() -> int:
    tests = (
        test_json_digest_helper,
        test_reason_summary,
        test_primary_reason,
        test_primary_action,
        test_action_list,
    )
    for test in tests:
        test()
    print("[ok] Hyper-V baseline evidence CI actions self-test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        raise SystemExit(1)
