from __future__ import annotations

import argparse
import io
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "tools" / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import verify_release_publication_manifest as publication  # noqa: E402
import verify_update_manifest as update  # noqa: E402
from update_manifest_common import ManifestError, PINNED_PUBLIC_KEY_HEX  # noqa: E402


class ReleaseManifestExpectationTests(unittest.TestCase):
    def test_update_trust_anchor_matches_runtime_and_operator_documentation(
        self,
    ) -> None:
        source = (ROOT / "src" / "services" / "update_agent_parse.c").read_text(
            encoding="utf-8"
        )
        initializer = re.search(
            r"update_agent_release_public_key\[ED25519_PUBLIC_KEY_SIZE\]\s*=\s*\{([^}]*)\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(initializer)
        assert initializer is not None
        runtime_hex = "".join(
            match.group(1).lower()
            for match in re.finditer(r"0x([0-9a-fA-F]{2})", initializer.group(1))
        )
        self.assertEqual(runtime_hex, PINNED_PUBLIC_KEY_HEX)
        self.assertEqual(len(runtime_hex), 64)

        signing_doc = (ROOT / "docs" / "security" / "release-signing.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(f"```text\n{PINNED_PUBLIC_KEY_HEX}\n```", signing_doc)

    def test_versioned_release_key_policy_is_canonical(self) -> None:
        policy = (
            ROOT
            / ".github"
            / "release-policy"
            / "release-checksum-ed25519.sha256"
        ).read_bytes()
        self.assertRegex(policy.decode("ascii"), r"^[0-9a-f]{64}\n$")

    def test_update_published_at_must_match_exactly(self) -> None:
        fields = {"published_at": "2026-08-24"}
        update.require_expected(fields, "published_at", "2026-08-24")
        with self.assertRaisesRegex(ManifestError, "published_at mismatch"):
            update.require_expected(fields, "published_at", "2026-08-25")

    def test_update_main_applies_expected_published_at(self) -> None:
        fields = {
            "available_version": "0.9.1+20260824",
            "channel": "stable",
            "branch": "main",
            "source": "github:CapyOS/CapyOS",
            "published_at": "2026-08-24",
            "payload_url": (
                "https://github.com/CapyOS/CapyOS/releases/download/"
                "v0.9.1+20260824/capyos64.bin"
            ),
            "payload_sha256": "00" * 32,
            "signature_ed25519": "00" * 64,
        }
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "latest.ini"
            manifest.write_bytes(b"manifest")
            args = argparse.Namespace(
                self_test=False,
                openssl="openssl",
                manifest=manifest,
                payload=None,
                public_key=None,
                expected_public_key_hex="00" * 32,
                expected_version=None,
                expected_channel=None,
                expected_branch=None,
                expected_source=None,
                expected_published_at="2026-08-25",
                expected_payload_url=None,
                allow_lab_http_payload_url=False,
            )
            with (
                patch.object(update, "parse_args", return_value=args),
                patch.object(update, "parse_manifest", return_value=(fields, b"body")),
                patch.object(update, "verify_signature"),
            ):
                stderr = io.StringIO()
                with redirect_stderr(stderr):
                    rc = update.main()
        self.assertEqual(rc, 1)
        self.assertIn("published_at mismatch", stderr.getvalue())

    def test_publication_release_id_mismatch_fails_before_material_access(
        self,
    ) -> None:
        with patch.object(
            publication,
            "load_publication_manifest",
            return_value=(0, {"release_id": "0.9.1+20260824"}, []),
        ):
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                rc = publication.verify_publication_manifest(
                    Path("unused.manifest"),
                    Path("unused-materials"),
                    Path("unused-artifacts"),
                    "ab" * 32,
                    "openssl",
                    "0.9.2+20260824",
                )
        self.assertEqual(rc, 1)
        self.assertIn("diverge do release id", stderr.getvalue())

    def test_publication_release_id_exact_match_reaches_crypto_contract(
        self,
    ) -> None:
        fingerprint = "ab" * 32
        release_id = "0.9.1+20260824"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, data in (
                ("release-artifacts.sha256", b"checksums\n"),
                ("release-artifacts.sha256.sig", b"s" * 64),
                ("release-ed25519.pub.pem", b"public\n"),
                ("release-public-key.manifest", b"manifest\n"),
            ):
                (root / name).write_bytes(data)

            data = {
                "release_id": release_id,
                "checksums_file": "release-artifacts.sha256",
                "checksums_sha256": "00" * 32,
                "signature_file": "release-artifacts.sha256.sig",
                "signature_sha256": "11" * 32,
                "public_key_file": "release-ed25519.pub.pem",
                "public_key_sha256": fingerprint,
                "expected_public_key_sha256": fingerprint,
                "public_key_manifest_file": "release-public-key.manifest",
                "public_key_manifest_sha256": "22" * 32,
            }
            key_data = {
                "format": publication.KEY_MANIFEST_FORMAT,
                "algorithm": "Ed25519",
                "public_key_encoding": "PEM/SPKI",
                "public_key_file": "release-ed25519.pub.pem",
                "public_key_sha256": fingerprint,
                "expected_public_key_sha256": fingerprint,
                "private_key_included": "no",
            }
            with (
                patch.object(
                    publication,
                    "load_publication_manifest",
                    return_value=(0, data, []),
                ),
                patch.object(publication, "verify_material_hash", return_value=0),
                patch.object(
                    publication,
                    "public_key_sha256_hex",
                    return_value=fingerprint,
                ),
                patch.object(
                    publication,
                    "load_public_key_manifest",
                    return_value=(0, key_data),
                ),
                patch.object(publication, "load_checksums", return_value=(0, [])),
                patch.object(publication, "signature_verify_ok", return_value=True),
            ):
                stdout = io.StringIO()
                with redirect_stdout(stdout):
                    rc = publication.verify_publication_manifest(
                        root / "release-publication.manifest",
                        root,
                        root,
                        fingerprint,
                        "openssl",
                        release_id,
                    )
        self.assertEqual(rc, 0)
        self.assertIn("manifesto publico de publicacao conferido", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
