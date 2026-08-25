from __future__ import annotations

import copy
import unittest
from typing import Any

from tools.scripts.verify_release_repository_policy import (
    RepositoryPolicyError,
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

    def test_rejects_zero_required_approvals(self) -> None:
        ruleset = copy.deepcopy(valid_main_ruleset())
        ruleset["rules"][2]["parameters"]["required_approving_review_count"] = 0
        with self.assertRaisesRegex(RepositoryPolicyError, "at least one approval"):
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
