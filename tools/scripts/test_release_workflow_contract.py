from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILDER = ROOT / ".github" / "workflows" / "release-artifacts.yml"
PROMOTER = ROOT / ".github" / "workflows" / "release-promote-signed.yml"
PUBLICATION_GATE = ROOT / "tools" / "scripts" / "release_publication_gate.py"
PUBLICATION_VERIFY = (
    ROOT / "tools" / "scripts" / "verify_release_publication_manifest.py"
)


class ReleaseWorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.builder = BUILDER.read_text(encoding="utf-8")
        cls.promoter = PROMOTER.read_text(encoding="utf-8")
        cls.publication_gate = PUBLICATION_GATE.read_text(encoding="utf-8")
        cls.publication_verify = PUBLICATION_VERIFY.read_text(encoding="utf-8")

    def test_builder_can_only_leave_an_unpublished_draft(self) -> None:
        self.assertIn("draft: true", self.builder)
        self.assertIn("make_latest: false", self.builder)
        self.assertNotIn("-F draft=false", self.builder)
        self.assertNotIn("-f make_latest=true", self.builder)
        self.assertIn("retention-days: 14", self.builder)
        self.assertIn("Record the offline signing boundary", self.builder)

    def test_builder_serializes_with_promotion_and_protects_signed_assets(self) -> None:
        self.assertIn("group: release-publication\n", self.builder)
        self.assertNotIn("group: release-publication-", self.builder)
        self.assertIn("cancel-in-progress: false", self.builder)
        self.assertIn('gh release view "$TAG"', self.builder)
        self.assertIn("databaseId,tagName,isDraft,isPrerelease,isImmutable,assets", self.builder)
        self.assertNotIn("/releases/tags/${TAG}", self.builder)
        self.assertIn("grep -Fxq 'release not found'", self.builder)
        self.assertNotIn("|not found)", self.builder)
        self.assertEqual(self.builder.count("if jq -e 'any(.assets[];"), 1)
        self.assertNotIn("| grep -Eq", self.builder)
        for signed_asset in (
            "latest.ini",
            "release-artifacts.sha256.sig",
            "release-ed25519.pub.pem",
            "release-public-key.manifest",
            "release-publication.manifest",
        ):
            self.assertIn(f'.name == "{signed_asset}"', self.builder)
        self.assertIn('if [ "$JQ_STATUS" -ne 1 ]', self.builder)
        self.assertIn(
            "Unable to inspect the existing draft asset inventory", self.builder
        )
        self.assertIn(
            "Refusing to replace payloads after offline signed materials were attached",
            self.builder,
        )
        self.assertNotIn('TAG="${{ inputs.release_tag }}"', self.builder)

    def test_promoter_is_manual_main_only_and_uses_a_pinned_fingerprint(self) -> None:
        self.assertIn("workflow_dispatch:", self.promoter)
        self.assertNotIn("\n  push:", self.promoter)
        self.assertNotIn("\n  workflow_run:", self.promoter)
        self.assertIn("if: github.ref == 'refs/heads/main'", self.promoter)
        self.assertNotIn("vars.CAPYOS_RELEASE_PUBLIC_KEY_SHA256", self.promoter)
        self.assertIn(
            ".github/release-policy/release-checksum-ed25519.sha256",
            self.promoter,
        )
        self.assertIn(
            'git show "${RELEASE_TAG_COMMIT}:${POLICY_PATH}"', self.promoter
        )
        self.assertIn('git ls-tree "$RELEASE_TAG_COMMIT"', self.promoter)
        self.assertIn('if [ "$POLICY_MODE" != "100644" ]', self.promoter)
        self.assertIn("mapfile -t FINGERPRINT_LINES", self.promoter)
        self.assertIn("must contain exactly one line", self.promoter)
        self.assertIn('POLICY_SIZE="$(wc -c < "$POLICY_FILE")"', self.promoter)
        self.assertIn('tail -c 1 "$POLICY_FILE"', self.promoter)
        self.assertIn("exactly 64 hex bytes followed by LF", self.promoter)
        self.assertIn("one lowercase SHA-256 hex value", self.promoter)
        self.assertIn("group: release-publication\n", self.promoter)
        self.assertNotIn("group: release-publication-", self.promoter)
        self.assertNotIn('TAG="${{ inputs.release_tag }}"', self.promoter)
        self.assertIn("git merge-base --is-ancestor", self.promoter)
        self.assertIn(
            'if [ "$RELEASE_STATE" = "draft" ] \\\n'
            '            && [ "$RELEASE_TAG_COMMIT" != "$GITHUB_SHA" ]',
            self.promoter,
        )

    def test_promoter_requires_immutable_releases_and_draft_aware_lookup(self) -> None:
        self.assertIn(
            '"repos/${GITHUB_REPOSITORY}/immutable-releases"', self.promoter
        )
        self.assertIn(
            "GH_TOKEN: ${{ secrets.CAPYOS_RELEASE_POLICY_AUDIT_TOKEN }}",
            self.promoter,
        )
        self.assertIn(
            "if: steps.release_object.outputs.release_state == 'draft'",
            self.promoter,
        )
        self.assertIn("CAPYOS_RELEASE_POLICY_AUDIT_TOKEN is absent", self.promoter)
        self.assertIn("CAPYOS_RELEASE_MAIN_RULESET_ID", self.promoter)
        self.assertIn("CAPYOS_RELEASE_TAG_RULESET_ID", self.promoter)
        self.assertIn("verify_release_repository_policy.py", self.promoter)
        self.assertIn("--immutable-settings", self.promoter)
        self.assertIn("--tag-ruleset", self.promoter)
        self.assertIn("--main-ruleset", self.promoter)
        self.assertIn('gh release view "$TAG"', self.promoter)
        self.assertIn("databaseId,tagName,isDraft,isPrerelease,isImmutable,assets", self.promoter)
        self.assertNotIn("/releases/tags/${", self.promoter)

    def test_asset_snapshot_jq_filters_are_shell_safe(self) -> None:
        self.assertNotIn("state}] \\\n            | sort_by", self.promoter)
        self.assertEqual(self.promoter.count("| sort_by(.name)' \\"), 5)

    def test_all_fail_closed_gates_precede_the_first_remote_mutation(self) -> None:
        first_patch = self.promoter.index("gh api --method PATCH")
        required_before_patch = (
            "verify_release_promotion_bundle.py",
            "release_publication_gate.py",
            "verify_update_manifest.py",
            "verify_modules_index_assets.py",
            "verify_release_repository_policy.py",
            "Refuse release mutation after verification",
            "Require repository release protections",
        )
        for marker in required_before_patch:
            self.assertLess(self.promoter.index(marker), first_patch, marker)
        self.assertEqual(
            self.promoter.count("verify_release_repository_policy.py"), 2
        )
        final_policy = self.promoter.rindex("verify_release_repository_policy.py")
        self.assertLess(final_policy, first_patch)
        self.assertNotIn("\n      - name:", self.promoter[final_policy:first_patch])
        final_policy_prefix = self.promoter[final_policy - 1800:final_policy]
        self.assertIn("unset POLICY_AUDIT_TOKEN", final_policy_prefix)
        self.assertIn("env -u RELEASE_TOKEN python3", final_policy_prefix)
        self.assertIn("unset GH_TOKEN", self.promoter[:final_policy])
        for final_snapshot in (
            "immutable-release-settings-final.json",
            "release-tag-ruleset-final.json",
            "release-main-ruleset-final.json",
        ):
            self.assertIn(final_snapshot, self.promoter[final_policy - 1800:first_patch])

    def test_promotion_is_one_atomic_and_idempotent_mutation(self) -> None:
        publish = self.promoter.index("gh api --method PATCH")
        immutable_state = self.promoter.index(
            "Verify immutable publication state and Latest convergence"
        )
        public_verify = self.promoter.index(
            "Download publicly and repeat every verification"
        )
        latest_route = self.promoter.index("Verify runtime Latest download route")
        self.assertLess(publish, immutable_state)
        self.assertLess(immutable_state, public_verify)
        self.assertLess(public_verify, latest_route)
        self.assertEqual(self.promoter.count("gh api --method PATCH"), 1)
        self.assertIn("-F draft=false", self.promoter)
        self.assertIn("-f make_latest=true", self.promoter)
        self.assertNotIn("-f make_latest=false", self.promoter)
        self.assertIn(
            "if: steps.release_object.outputs.release_state == 'draft'",
            self.promoter,
        )
        self.assertIn("RELEASE_STATE=published", self.promoter)
        self.assertIn("refusing non-idempotent recovery", self.promoter)
        self.assertGreaterEqual(
            self.promoter.count("git ls-remote --exit-code origin"), 4
        )
        self.assertIn("Remote release tag changed after checkout", self.promoter)
        self.assertGreaterEqual(self.promoter.count("curl --fail"), 2)
        self.assertIn("releases/latest/download/$asset", self.promoter)
        self.assertIn("Latest release changed before workflow completion", self.promoter)

    def test_promotion_repeats_crypto_and_asset_gates_after_publication(self) -> None:
        self.assertEqual(
            self.promoter.count("verify_release_promotion_bundle.py"), 2
        )
        self.assertEqual(self.promoter.count("release_publication_gate.py"), 2)
        self.assertEqual(self.promoter.count("verify_update_manifest.py"), 3)
        self.assertEqual(self.promoter.count("verify_modules_index_assets.py"), 2)
        self.assertIn("release-assets-before.json", self.promoter)
        self.assertIn("release-assets-public.json", self.promoter)
        self.assertIn("release-assets-latest.json", self.promoter)

    def test_release_id_keeps_the_existing_extended_version_semantics(self) -> None:
        self.assertIn('--expected-release-id "$RELEASE_VERSION"', self.promoter)
        self.assertIn('parser.add_argument("--expected-release-id")', self.publication_gate)
        self.assertIn(
            'publication_command.extend(["--expected-release-id", args.expected_release_id])',
            self.publication_gate,
        )
        self.assertNotIn("expected_release_id.isdigit()", self.publication_verify)


if __name__ == "__main__":
    unittest.main()
