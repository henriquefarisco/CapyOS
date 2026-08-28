#!/usr/bin/env python3
"""Validate the GitHub repository controls required for signed promotion."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


class RepositoryPolicyError(ValueError):
    pass


SOLO_MAINTAINER_REQUIRED_STATUS_CHECKS = {
    "Analyze (c-cpp)": 15368,
    "Analyze (python)": 15368,
    "CodeQL": 57789,
    "Lint": 15368,
    "QEMU ISO smoke": 15368,
    "Release gates": 15368,
}


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RepositoryPolicyError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json_object(path: Path, label: str) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise RepositoryPolicyError(f"{label} is not a regular file: {path}")
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
        )
    except UnicodeDecodeError as exc:
        raise RepositoryPolicyError(f"{label} is not UTF-8") from exc
    except json.JSONDecodeError as exc:
        raise RepositoryPolicyError(f"{label} is not valid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise RepositoryPolicyError(f"{label} must be a JSON object")
    return value


def _require_exact_ref_scope(
    ruleset: dict[str, Any], expected_ref: str, label: str
) -> None:
    conditions = ruleset.get("conditions")
    if not isinstance(conditions, dict):
        raise RepositoryPolicyError(f"{label} has no conditions object")
    ref_name = conditions.get("ref_name")
    if not isinstance(ref_name, dict):
        raise RepositoryPolicyError(f"{label} has no ref_name condition")
    if ref_name.get("include") != [expected_ref]:
        raise RepositoryPolicyError(
            f"{label} must include exactly {expected_ref!r}"
        )
    if ref_name.get("exclude") != []:
        raise RepositoryPolicyError(
            f"{label} must expose an empty exclude array"
        )


def _rule_map(ruleset: dict[str, Any], label: str) -> dict[str, dict[str, Any]]:
    rules = ruleset.get("rules")
    if not isinstance(rules, list):
        raise RepositoryPolicyError(f"{label} has no rules array")
    result: dict[str, dict[str, Any]] = {}
    for rule in rules:
        if not isinstance(rule, dict) or not isinstance(rule.get("type"), str):
            raise RepositoryPolicyError(f"{label} contains an invalid rule")
        rule_type = rule["type"]
        if rule_type in result:
            raise RepositoryPolicyError(f"{label} repeats rule {rule_type!r}")
        result[rule_type] = rule
    return result


def _require_common_ruleset_controls(
    ruleset: dict[str, Any], target: str, expected_ref: str, label: str
) -> dict[str, dict[str, Any]]:
    if ruleset.get("target") != target:
        raise RepositoryPolicyError(f"{label} must target {target}s")
    if ruleset.get("enforcement") != "active":
        raise RepositoryPolicyError(f"{label} must be active")
    if "bypass_actors" not in ruleset:
        raise RepositoryPolicyError(
            f"{label} response must expose bypass_actors for fail-closed review"
        )
    if ruleset["bypass_actors"] != []:
        raise RepositoryPolicyError(f"{label} must not define bypass actors")
    _require_exact_ref_scope(ruleset, expected_ref, label)
    return _rule_map(ruleset, label)


def _require_solo_maintainer_controls(
    main_rules: dict[str, dict[str, Any]],
    pull_request: dict[str, Any],
) -> None:
    if pull_request.get("dismiss_stale_reviews_on_push") is not False:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must disable stale-review handling"
        )
    if pull_request.get("require_last_push_approval") is not False:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must disable last-push approval"
        )
    if (
        pull_request.get("require_extra_approval_for_unattributed_changes")
        is not False
    ):
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must disable extra approval"
        )
    if pull_request.get("require_code_owner_review") is not False:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must disable code-owner approval"
        )
    if pull_request.get("required_reviewers") != []:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must not define required reviewers"
        )
    if pull_request.get("required_review_thread_resolution") is not True:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must require thread resolution"
        )
    if pull_request.get("allowed_merge_methods") != ["squash"]:
        raise RepositoryPolicyError(
            "solo-maintainer pull_request rule must allow only squash merge"
        )

    status_rule = main_rules.get("required_status_checks")
    if status_rule is None:
        raise RepositoryPolicyError(
            "solo-maintainer main ruleset must require status checks"
        )
    parameters = status_rule.get("parameters")
    if not isinstance(parameters, dict):
        raise RepositoryPolicyError(
            "main required_status_checks rule has no parameters"
        )
    if parameters.get("strict_required_status_checks_policy") is not True:
        raise RepositoryPolicyError(
            "solo-maintainer status checks must require the latest main"
        )
    if parameters.get("do_not_enforce_on_create") is not False:
        raise RepositoryPolicyError(
            "solo-maintainer status checks must be enforced on creation"
        )

    checks = parameters.get("required_status_checks")
    if not isinstance(checks, list):
        raise RepositoryPolicyError(
            "main required_status_checks rule has no checks array"
        )
    actual: dict[str, int] = {}
    for check in checks:
        if not isinstance(check, dict):
            raise RepositoryPolicyError(
                "main required_status_checks rule contains an invalid check"
            )
        context = check.get("context")
        integration_id = check.get("integration_id")
        if not isinstance(context, str) or not context:
            raise RepositoryPolicyError(
                "main required_status_checks rule contains an invalid context"
            )
        if not isinstance(integration_id, int) or isinstance(integration_id, bool):
            raise RepositoryPolicyError(
                f"required status check {context!r} has no integration identity"
            )
        if context in actual:
            raise RepositoryPolicyError(
                f"main required_status_checks rule repeats {context!r}"
            )
        actual[context] = integration_id

    missing = set(SOLO_MAINTAINER_REQUIRED_STATUS_CHECKS) - set(actual)
    if missing:
        raise RepositoryPolicyError(
            "solo-maintainer main ruleset is missing status checks: "
            + ", ".join(sorted(missing))
        )
    for context, expected_integration_id in (
        SOLO_MAINTAINER_REQUIRED_STATUS_CHECKS.items()
    ):
        if actual[context] != expected_integration_id:
            raise RepositoryPolicyError(
                f"required status check {context!r} must come from integration "
                f"{expected_integration_id}"
            )


def verify_repository_policy(
    immutable_settings: dict[str, Any],
    tag_ruleset: dict[str, Any],
    main_ruleset: dict[str, Any],
) -> None:
    if immutable_settings.get("enabled") is not True:
        raise RepositoryPolicyError("immutable releases are not enabled")

    tag_rules = _require_common_ruleset_controls(
        tag_ruleset,
        "tag",
        "refs/tags/v*",
        "release tag ruleset",
    )
    missing_tag_rules = {"deletion", "update"} - set(tag_rules)
    if missing_tag_rules:
        raise RepositoryPolicyError(
            "release tag ruleset is missing rules: "
            + ", ".join(sorted(missing_tag_rules))
        )

    main_rules = _require_common_ruleset_controls(
        main_ruleset,
        "branch",
        "refs/heads/main",
        "main branch ruleset",
    )
    missing_main_rules = {"deletion", "non_fast_forward", "pull_request"} - set(
        main_rules
    )
    if missing_main_rules:
        raise RepositoryPolicyError(
            "main branch ruleset is missing rules: "
            + ", ".join(sorted(missing_main_rules))
        )
    pull_request = main_rules["pull_request"].get("parameters")
    if not isinstance(pull_request, dict):
        raise RepositoryPolicyError("main pull_request rule has no parameters")
    approvals = pull_request.get("required_approving_review_count")
    if (
        not isinstance(approvals, int)
        or isinstance(approvals, bool)
        or approvals < 0
    ):
        raise RepositoryPolicyError(
            "main pull_request rule has an invalid approval count"
        )
    if approvals == 0:
        _require_solo_maintainer_controls(main_rules, pull_request)
    else:
        if pull_request.get("dismiss_stale_reviews_on_push") is not True:
            raise RepositoryPolicyError(
                "reviewed main pull_request rule must dismiss stale approvals "
                "after a push"
            )
        if pull_request.get("require_last_push_approval") is not True:
            raise RepositoryPolicyError(
                "reviewed main pull_request rule must require approval of the "
                "last push"
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate immutable releases plus the protected release-tag and "
            "main-branch rulesets required before signed promotion."
        )
    )
    parser.add_argument("--immutable-settings", type=Path, required=True)
    parser.add_argument("--tag-ruleset", type=Path, required=True)
    parser.add_argument("--main-ruleset", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        immutable = load_json_object(args.immutable_settings, "immutable settings")
        tag = load_json_object(args.tag_ruleset, "release tag ruleset")
        branch = load_json_object(args.main_ruleset, "main branch ruleset")
        verify_repository_policy(immutable, tag, branch)
    except (OSError, RepositoryPolicyError) as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 1
    print(
        "[ok] immutable releases and protected tag/main rulesets verified"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
