#!/usr/bin/env python3
"""Offline adversarial tests for the official modules-index verifier."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from io import BytesIO
from pathlib import Path
from unittest import mock
from urllib.error import HTTPError

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import modules_index_catalog as catalog  # noqa: E402
import verify_modules_index_assets as verifier  # noqa: E402

RELEASE_TAG = "v0.8.0-alpha.320+20260730"
AI_INDEX = next(
    index
    for index, spec in enumerate(catalog.MODULE_SPECS)
    if spec.uses_capyos_release_tag
)


class FakeResponse:
    def __init__(
        self,
        payload: bytes,
        *,
        status: int = 200,
        effective_url: str = "https://cdn.example.test/module.bin",
    ) -> None:
        self._stream = BytesIO(payload)
        self.status = status
        self._effective_url = effective_url

    def read(self, size: int = -1) -> bytes:
        return self._stream.read(size)

    def geturl(self) -> str:
        return self._effective_url

    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *args: object) -> None:
        self._stream.close()


class FakeOpener:
    def __init__(self, outcomes: dict[str, list[object]]) -> None:
        self.outcomes = {url: list(items) for url, items in outcomes.items()}
        self.calls: list[str] = []

    def __call__(self, request: object, *, timeout: float) -> object:
        del timeout
        url = request.full_url  # type: ignore[attr-defined]
        self.calls.append(url)
        queue = self.outcomes[url]
        outcome = queue.pop(0)
        if isinstance(outcome, BaseException):
            raise outcome
        return outcome


def make_entry(
    spec: catalog.ModuleSpec,
    payload: bytes,
    **overrides: str,
) -> str:
    pinned_metadata = catalog.pinned_payload_metadata(spec)
    if pinned_metadata is None:
        payload_sha256 = hashlib.sha256(payload).hexdigest()
        payload_size = len(payload)
    else:
        payload_sha256, payload_size = pinned_metadata
    fields = {
        "name": spec.module_id,
        "version": spec.version,
        "summary": f"fixture for {spec.module_id}",
        "official": "1",
        "payload_url": catalog.expected_payload_url(spec, RELEASE_TAG),
        "payload_sha256": payload_sha256,
        "payload_size": str(payload_size),
        "install_root": spec.install_root,
        "depends": ",".join(spec.dependencies),
    }
    fields.update(overrides)
    return "\n".join(
        [*(f"{key}={value}" for key, value in fields.items()), "---"]
    )


def make_index(
    payloads: list[bytes],
    *,
    overrides: dict[int, dict[str, str]] | None = None,
) -> str:
    changes = overrides or {}
    entries = [
        make_entry(spec, payload, **changes.get(index, {}))
        for index, (spec, payload) in enumerate(
            zip(catalog.MODULE_SPECS, payloads), start=1
        )
    ]
    return "# offline official fixture\n" + "\n".join(entries) + "\n"


class VerifyModulesIndexAssetsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.payloads = [
            f"payload-{index}".encode("ascii")
            for index in range(1, catalog.EXPECTED_MODULE_COUNT + 1)
        ]

    def test_success_verifies_exact_official_inventory(self) -> None:
        def verified(entry: verifier.ModuleEntry, **_: object) -> verifier.VerificationResult:
            return verifier.VerificationResult(entry.name, entry.payload_size, 1)

        with mock.patch.object(
            verifier, "verify_payload", side_effect=verified
        ) as verify_remote:
            results = verifier.verify_modules_index_assets(
                make_index(self.payloads),
                sleeper=lambda _: None,
                release_tag=RELEASE_TAG,
            )
        self.assertEqual(len(results), catalog.EXPECTED_MODULE_COUNT)
        self.assertEqual(verify_remote.call_count, catalog.EXPECTED_MODULE_COUNT)
        self.assertTrue(all(result.attempts == 1 for result in results))

        entries = verifier.parse_modules_index(
            make_index(self.payloads), release_tag=RELEASE_TAG
        )
        ai_entry = entries[AI_INDEX]
        ai_payload = self.payloads[AI_INDEX]
        opener = FakeOpener(
            {ai_entry.payload_url: [FakeResponse(ai_payload)]}
        )
        result = verifier.verify_payload(ai_entry, opener=opener)
        self.assertEqual(result.payload_size, len(ai_payload))

    def test_duplicate_name_is_rejected_before_network(self) -> None:
        opener = FakeOpener({})
        duplicate = catalog.MODULE_SPECS[0].module_id
        with self.assertRaisesRegex(
            verifier.ModulesIndexError, "duplicate module name"
        ):
            verifier.verify_modules_index_assets(
                make_index(
                    self.payloads,
                    overrides={catalog.EXPECTED_MODULE_COUNT: {"name": duplicate}},
                ),
                opener=opener,
                release_tag=RELEASE_TAG,
            )
        self.assertEqual(opener.calls, [])

    def test_catalog_drift_is_rejected_before_network(self) -> None:
        cases = {
            "version": "99.0.0",
            "official": "0",
            "install_root": "/var/capypkg/wrong",
            "depends": "org.capyos.agent.core",
            "payload_url": (
                "https://github.com/henriquefarisco/Wrong/releases/"
                "download/v1/wrong.bin"
            ),
            "payload_sha256": "0" * 64,
            "payload_size": "133121",
        }
        for field, value in cases.items():
            with self.subTest(field=field):
                opener = FakeOpener({})
                with self.assertRaises(verifier.ModulesIndexError):
                    verifier.verify_modules_index_assets(
                        make_index(
                            self.payloads, overrides={1: {field: value}}
                        ),
                        opener=opener,
                        release_tag=RELEASE_TAG,
                    )
                self.assertEqual(opener.calls, [])

    def test_unknown_and_duplicate_fields_are_rejected(self) -> None:
        base = make_index(self.payloads)
        cases = (
            base.replace("version=0.0.10", "unknown=x\nversion=0.0.10", 1),
            base.replace(
                "version=0.0.10", "version=0.0.10\nversion=0.0.10", 1
            ),
        )
        for text in cases:
            with self.subTest():
                with self.assertRaises(verifier.ModulesIndexError):
                    verifier.parse_modules_index(text, release_tag=RELEASE_TAG)

    def test_noncanonical_and_oversize_payload_sizes_are_rejected(self) -> None:
        for size in ("0", "01", str(verifier.MAX_PAYLOAD_BYTES + 1)):
            with self.subTest(size=size):
                with self.assertRaisesRegex(
                    verifier.ModulesIndexError, "payload_size"
                ):
                    verifier.parse_modules_index(
                        make_index(
                            self.payloads,
                            overrides={1: {"payload_size": size}},
                        ),
                        release_tag=RELEASE_TAG,
                    )

    def test_sha_mismatch_does_not_expose_payload(self) -> None:
        secret = b"TOP-SECRET-PAYLOAD-CONTENT"
        payloads = list(self.payloads)
        payloads[AI_INDEX] = secret
        entry = verifier.parse_modules_index(
            make_index(
                payloads,
                overrides={AI_INDEX + 1: {"payload_sha256": "0" * 64}},
            ),
            release_tag=RELEASE_TAG,
        )[AI_INDEX]
        opener = FakeOpener({entry.payload_url: [FakeResponse(secret)]})
        with self.assertRaises(verifier.ModulesIndexError) as raised:
            verifier.verify_payload(entry, opener=opener)
        self.assertIn("SHA-256 mismatch", str(raised.exception))
        self.assertNotIn("TOP-SECRET", str(raised.exception))

    def test_size_mismatch_fails_without_retry(self) -> None:
        payload = self.payloads[AI_INDEX]
        entry = verifier.parse_modules_index(
            make_index(
                self.payloads,
                overrides={
                    AI_INDEX + 1: {"payload_size": str(len(payload) + 1)}
                },
            ),
            release_tag=RELEASE_TAG,
        )[AI_INDEX]
        opener = FakeOpener({entry.payload_url: [FakeResponse(payload)]})
        with self.assertRaisesRegex(
            verifier.ModulesIndexError, "payload size mismatch"
        ):
            verifier.verify_payload(entry, opener=opener, attempts=4)
        self.assertEqual(opener.calls, [entry.payload_url])

    def test_http_404_is_not_retried_by_default(self) -> None:
        entry = verifier.parse_modules_index(
            make_index(self.payloads), release_tag=RELEASE_TAG
        )[AI_INDEX]
        opener = FakeOpener(
            {
                entry.payload_url: [
                    HTTPError(
                        entry.payload_url, 404, "not found", hdrs=None, fp=None
                    ),
                    AssertionError("404 must not be retried"),
                ]
            }
        )
        sleeps: list[float] = []
        with self.assertRaisesRegex(verifier.ModulesIndexError, "HTTP 404"):
            verifier.verify_payload(
                entry, opener=opener, attempts=4, sleeper=sleeps.append
            )
        self.assertEqual(opener.calls, [entry.payload_url])
        self.assertEqual(sleeps, [])

    def test_http_404_retry_requires_explicit_propagation_flag(self) -> None:
        entry = verifier.parse_modules_index(
            make_index(self.payloads), release_tag=RELEASE_TAG
        )[AI_INDEX]
        opener = FakeOpener(
            {
                entry.payload_url: [
                    HTTPError(
                        entry.payload_url, 404, "not found", hdrs=None, fp=None
                    ),
                    FakeResponse(self.payloads[AI_INDEX]),
                ]
            }
        )
        sleeps: list[float] = []
        result = verifier.verify_payload(
            entry,
            opener=opener,
            attempts=2,
            backoff_seconds=0.25,
            sleeper=sleeps.append,
            retry_404_for_propagation=True,
        )
        self.assertEqual(result.attempts, 2)
        self.assertEqual(sleeps, [0.25])

    def test_http_5xx_retries_with_bounded_backoff(self) -> None:
        entry = verifier.parse_modules_index(
            make_index(self.payloads), release_tag=RELEASE_TAG
        )[AI_INDEX]
        opener = FakeOpener(
            {
                entry.payload_url: [
                    HTTPError(
                        entry.payload_url, 503, "unavailable", hdrs=None, fp=None
                    ),
                    HTTPError(
                        entry.payload_url, 502, "bad gateway", hdrs=None, fp=None
                    ),
                    HTTPError(
                        entry.payload_url, 500, "server error", hdrs=None, fp=None
                    ),
                ]
            }
        )
        sleeps: list[float] = []
        with self.assertRaisesRegex(
            verifier.ModulesIndexError, "HTTP 500 after 3 attempt"
        ):
            verifier.verify_payload(
                entry,
                opener=opener,
                attempts=3,
                backoff_seconds=0.25,
                sleeper=sleeps.append,
            )
        self.assertEqual(sleeps, [0.25, 0.5])

    def test_local_draft_asset_overrides_only_matching_remote(self) -> None:
        ai_spec = catalog.MODULE_SPECS[AI_INDEX]
        remote_names: list[str] = []

        def verified(entry: verifier.ModuleEntry, **_: object) -> verifier.VerificationResult:
            remote_names.append(entry.name)
            return verifier.VerificationResult(entry.name, entry.payload_size, 1)

        with tempfile.TemporaryDirectory() as directory:
            local_dir = Path(directory)
            (local_dir / ai_spec.asset).write_bytes(self.payloads[AI_INDEX])
            with mock.patch.object(
                verifier, "verify_payload", side_effect=verified
            ):
                results = verifier.verify_modules_index_assets(
                    make_index(self.payloads),
                    sleeper=lambda _: None,
                    local_assets_dir=local_dir,
                    release_tag=RELEASE_TAG,
                )
        local = [result for result in results if result.source == "local"]
        self.assertEqual([result.name for result in local], [ai_spec.module_id])
        self.assertEqual(len(remote_names), catalog.EXPECTED_MODULE_COUNT - 1)
        self.assertNotIn(ai_spec.module_id, remote_names)


if __name__ == "__main__":
    unittest.main()
