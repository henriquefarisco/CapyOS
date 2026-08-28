from __future__ import annotations

import copy
import unittest
from typing import Any

from tools.scripts.verify_release_repository_policy import (
    RepositoryPolicyError,
    SOLO_MAINTAINER_REQUIRED_STATUS_CHECKS,
    verify_repository_policy,
)


def valid_tag_ruleset() -> dict[str, Any]:
    return {
        "target": "tag",
        "enforcement": "active",
        "bypass_actors": [],
        "conditions": {
            "ref_name": {"include": ["refs/tags/v*"], "exclude": []}
        },
        "rules": [{"type": "deletion"}, {"type": "update"}],
    }


def valid_main_ruleset() -> dict[str, Any]:
    return {
        "target": "branch",
        "enforcement": "active",
        "bypass_actors": [],
        "conditions": {
            "ref_name": {"include": ["refs/heads/main"], "exclude": []}
        },
        "rules": [
            {"type": "deletion"},
            {"type": "non_fast_forward"},
            {
                "type": "pull_request",
                "parameters": {
                    "required_approving_review_count": 1,
                    "dismiss_stale_reviews_on_push": True,
                    "require_last_push_approval": True,
                },
            },
        ],
    }


def valid_solo_main_ruleset() -> dict[str, Any]:
    ruleset = valid_main_ruleset()
    pull_request = ruleset["rules"][2]["parameters"]
    pull_request.update(
        {
            "allowed_merge_methods": ["squash"],
            "dismiss_stale_reviews_on_push": False,
            "require_code_owner_review": False,
            "require_extra_approval_for_unattributed_changes": False,
            "require_last_push_approval": False,
            "required_approving_review_count": 0,
            "required_review_thread_resolution": True,
            "required_reviewers": [],
        }
    )
    ruleset["rules"].append(
        {
            "type": "required_status_checks",
            "parameters": {
                "do_not_enforce_on_create": False,
                "required_status_checks": [
                    {"context": context, "integration_id": integration_id}
                    for context, integration_id in sorted(
                        SOLO_MAINTAINER_REQUIRED_STATUS_CHECKS.items()
                    )
                ],
                "strict_required_status_checks_policy": True,
            },
        }
    )
    return ruleset


class VerifyReleaseRepositoryPolicyTests(unittest.TestCase):
    def test_accepts_exact_fail_closed_policy(self) -> None:
        verify_repository_policy(
            {"enabled": True}, valid_tag_ruleset(), valid_main_ruleset()
        )

    def test_rejects_disabled_immutable_releases(self) -> None:
        with self.assertRaisesRegex(
            RepositoryPolicyError, "immutable releases are not enabled"
        ):
            verify_repository_policy(
                {"enabled": False}, valid_tag_ruleset(), valid_main_ruleset()
            )

    def test_rejects_tag_scope_broader_than_release_tags(self) -> None:
        ruleset = valid_tag_ruleset()
        ruleset["conditions"]["ref_name"]["include"].append("~ALL")
        with self.assertRaisesRegex(RepositoryPolicyError, "include exactly"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_ruleset_response_that_omits_exclusions(self) -> None:
        ruleset = valid_tag_ruleset()
        del ruleset["conditions"]["ref_name"]["exclude"]
        with self.assertRaisesRegex(RepositoryPolicyError, "empty exclude"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_any_excluded_release_tag(self) -> None:
        ruleset = valid_tag_ruleset()
        ruleset["conditions"]["ref_name"]["exclude"] = ["refs/tags/v0.*"]
        with self.assertRaisesRegex(RepositoryPolicyError, "empty exclude"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_tag_ruleset_bypass(self) -> None:
        ruleset = valid_tag_ruleset()
        ruleset["bypass_actors"] = [{"actor_type": "OrganizationAdmin"}]
        with self.assertRaisesRegex(RepositoryPolicyError, "bypass actors"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_ruleset_response_that_hides_bypass_actors(self) -> None:
        ruleset = valid_tag_ruleset()
        del ruleset["bypass_actors"]
        with self.assertRaisesRegex(RepositoryPolicyError, "must expose"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_missing_tag_update_rule(self) -> None:
        ruleset = valid_tag_ruleset()
        ruleset["rules"] = [{"type": "deletion"}]
        with self.assertRaisesRegex(RepositoryPolicyError, "missing rules: update"):
            verify_repository_policy({"enabled": True}, ruleset, valid_main_ruleset())

    def test_rejects_unprotected_main_branch(self) -> None:
        ruleset = valid_main_ruleset()
        ruleset["enforcement"] = "disabled"
        with self.assertRaisesRegex(RepositoryPolicyError, "must be active"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_accepts_fail_closed_solo_maintainer_policy(self) -> None:
        verify_repository_policy(
            {"enabled": True}, valid_tag_ruleset(), valid_solo_main_ruleset()
        )

    def test_rejects_zero_approvals_without_required_checks(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"].pop()
        with self.assertRaisesRegex(RepositoryPolicyError, "require status checks"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_missing_solo_maintainer_check(self) -> None:
        ruleset = valid_solo_main_ruleset()
        checks = ruleset["rules"][3]["parameters"]["required_status_checks"]
        checks[:] = [
            check for check in checks if check["context"] != "Release gates"
        ]
        with self.assertRaisesRegex(RepositoryPolicyError, "Release gates"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_wrong_solo_maintainer_check_source(self) -> None:
        ruleset = valid_solo_main_ruleset()
        checks = ruleset["rules"][3]["parameters"]["required_status_checks"]
        checks[0]["integration_id"] = 1
        with self.assertRaisesRegex(RepositoryPolicyError, "must come from"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_non_strict_solo_maintainer_checks(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][3]["parameters"][
            "strict_required_status_checks_policy"
        ] = False
        with self.assertRaisesRegex(RepositoryPolicyError, "latest main"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_solo_maintainer_check_creation_exemption(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][3]["parameters"]["do_not_enforce_on_create"] = True
        with self.assertRaisesRegex(RepositoryPolicyError, "enforced on creation"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_solo_maintainer_non_squash_merge(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][2]["parameters"]["allowed_merge_methods"] = [
            "merge",
            "squash",
        ]
        with self.assertRaisesRegex(RepositoryPolicyError, "only squash"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_unresolved_solo_maintainer_threads(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][2]["parameters"][
            "required_review_thread_resolution"
        ] = False
        with self.assertRaisesRegex(RepositoryPolicyError, "thread resolution"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_solo_maintainer_code_owner_approval(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][2]["parameters"]["require_code_owner_review"] = True
        with self.assertRaisesRegex(RepositoryPolicyError, "code-owner approval"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_solo_maintainer_required_reviewers(self) -> None:
        ruleset = valid_solo_main_ruleset()
        ruleset["rules"][2]["parameters"]["required_reviewers"] = [
            {"reviewer": {"id": 1, "type": "Team"}}
        ]
        with self.assertRaisesRegex(RepositoryPolicyError, "required reviewers"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_stale_approval_reuse(self) -> None:
        ruleset = copy.deepcopy(valid_main_ruleset())
        ruleset["rules"][2]["parameters"]["dismiss_stale_reviews_on_push"] = False
        with self.assertRaisesRegex(RepositoryPolicyError, "dismiss stale"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)

    def test_rejects_unreviewed_last_push(self) -> None:
        ruleset = copy.deepcopy(valid_main_ruleset())
        ruleset["rules"][2]["parameters"]["require_last_push_approval"] = False
        with self.assertRaisesRegex(RepositoryPolicyError, "last push"):
            verify_repository_policy({"enabled": True}, valid_tag_ruleset(), ruleset)


if __name__ == "__main__":
    unittest.main()
