#!/usr/bin/env python3

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from hyperv_baseline_evidence_summary import (
    SUMMARY_SCHEMA,
    SUMMARY_SCHEMA_FEATURES,
    SUMMARY_SCHEMA_VERSION,
    ci_acceptance_action_names_sha256,
    ci_acceptance_action_summary_sha256,
    ci_acceptance_actions_sha256,
    ci_acceptance_primary_action_sha256,
    ci_acceptance_primary_reason_sha256,
    ci_acceptance_reasons_sha256,
    ci_acceptance_reason_summary_sha256,
    kernel_load_failure_refs_sha256,
    kernel_load_failure_summary_sha256,
    kernel_load_failure_triage_sha256,
    result_summary,
    summary_schema_features_sha256,
)
from test_hyperv_baseline_evidence_summary import (
    BUNDLE_FILE_GROUPS,
    FOCUS_ALIASES,
    NEXT_SLICE_ALIASES,
    FakeResult,
    require,
)


EXPECTED_SCHEMA_FEATURES = [
    "summary-schema-features-digest",
    "kernel-loader-triage",
    "kernel-loader-triage-digest",
    "kernel-loader-primary-ref",
    "kernel-loader-summary-digest",
    "ci-acceptance-primary-action",
    "ci-acceptance-primary-reason-digest",
    "ci-acceptance-primary-action-digest",
    "ci-acceptance-reasons-digest",
    "ci-acceptance-actions",
    "ci-acceptance-actions-digest",
    "ci-acceptance-action-names-digest",
    "ci-acceptance-action-summary",
    "ci-acceptance-action-summary-digest",
    "ci-acceptance-reason-summary-digest",
]


def make_summary() -> dict[str, object]:
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial = tmp / "serial.log"
        serial_alt = tmp / "hyperv-serial.log"
        serial.write_text("serial evidence\n", encoding="utf-8")
        serial_alt.write_text("alternate serial evidence\n", encoding="utf-8")
        return result_summary(
            FakeResult([str(serial), str(serial_alt)]),
            BUNDLE_FILE_GROUPS,
            NEXT_SLICE_ALIASES,
            FOCUS_ALIASES,
            serial,
        )


def test_schema_identity() -> None:
    summary = make_summary()
    require(summary["summary_schema"] == SUMMARY_SCHEMA, "summary should include schema name")
    require(summary["summary_schema_version"] == SUMMARY_SCHEMA_VERSION, "summary should include schema version")
    require(summary["summary_schema_version"] == 2, "summary schema version should track CI triage fields")
    require(summary["summary_schema_features"] == SUMMARY_SCHEMA_FEATURES, "summary should include schema features")
    require(
        summary["summary_schema_features_sha256"] == summary_schema_features_sha256(summary["summary_schema_features"]),
        "summary should include schema features digest",
    )


def test_schema_feature_immutability() -> None:
    first = make_summary()
    first["summary_schema_features"].append("mutated-feature")
    second = make_summary()
    require(
        second["summary_schema_features"] == SUMMARY_SCHEMA_FEATURES,
        "summary schema features should be isolated per summary",
    )
    require(
        second["summary_schema_features_sha256"] == summary_schema_features_sha256(second["summary_schema_features"]),
        "summary schema features digest should be isolated per summary",
    )


def test_declared_schema_features() -> None:
    summary = make_summary()
    for feature in EXPECTED_SCHEMA_FEATURES:
        require(
            feature in summary["summary_schema_features"],
            f"summary schema features should declare {feature} support",
        )


def test_digest_fields() -> None:
    summary = make_summary()
    require(
        summary["kernel_load_failure_refs_sha256"] == kernel_load_failure_refs_sha256([]),
        "summary should include empty kernel load failure refs digest",
    )
    require(
        summary["kernel_load_failure_summary_sha256"]
        == kernel_load_failure_summary_sha256(summary["kernel_load_failure_summary"]),
        "summary should include kernel load failure summary digest",
    )
    require(
        summary["kernel_load_failure_triage_sha256"]
        == kernel_load_failure_triage_sha256(summary["kernel_load_failure_triage"]),
        "summary should include kernel load failure triage digest",
    )
    require(
        summary["ci_acceptance_reasons_sha256"] == ci_acceptance_reasons_sha256(summary["ci_acceptance_reasons"]),
        "summary should include ci acceptance reasons digest",
    )
    require(
        summary["ci_acceptance_primary_reason_sha256"]
        == ci_acceptance_primary_reason_sha256(summary["ci_acceptance_primary_reason"]),
        "summary should include ci acceptance primary reason digest",
    )
    require(
        summary["ci_acceptance_primary_action_sha256"]
        == ci_acceptance_primary_action_sha256(summary["ci_acceptance_primary_action"]),
        "summary should include ci acceptance primary action digest",
    )
    require(
        summary["ci_acceptance_actions_sha256"] == ci_acceptance_actions_sha256(summary["ci_acceptance_actions"]),
        "summary should include ci acceptance action digest",
    )
    require(
        summary["ci_acceptance_action_names_sha256"]
        == ci_acceptance_action_names_sha256(summary["ci_acceptance_actions"]),
        "summary should include ci acceptance action names digest",
    )
    require(
        summary["ci_acceptance_action_summary_sha256"]
        == ci_acceptance_action_summary_sha256(summary["ci_acceptance_action_summary"]),
        "summary should include ci acceptance action summary digest",
    )
    require(
        summary["ci_acceptance_reason_summary_sha256"]
        == ci_acceptance_reason_summary_sha256(summary["ci_acceptance_reason_summary"]),
        "summary should include ci acceptance reason summary digest",
    )


def main() -> int:
    tests = (
        test_schema_identity,
        test_schema_feature_immutability,
        test_declared_schema_features,
        test_digest_fields,
    )
    for test in tests:
        test()
    print("[ok] Hyper-V baseline evidence schema self-test passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        raise SystemExit(1)
