"""Offline negative tests for immutable producer staging."""
from dataclasses import replace
import hashlib
from io import BytesIO
from pathlib import Path
import tempfile
import unittest

from build_modules_index import ManifestError, parse_manifest
from modules_index_catalog import MODULE_SPECS
from stage_published_modules import stage


class Response(BytesIO):
    status = 200
    url = "https://release-assets.githubusercontent.com/payload"

    def geturl(self):
        return self.url


class StageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name) / "inputs"
        self.payload = b"pinned payload"
        self.spec = replace(MODULE_SPECS[0],
                            published_payload_sha256=hashlib.sha256(self.payload).hexdigest(),
                            published_payload_size=len(self.payload))

    def run_stage(self, payload=None, *, status=200, url=None):
        def opener(request, timeout):
            self.assertEqual(timeout, 45)
            self.assertTrue(request.full_url.startswith("https://"))
            response = Response(self.payload if payload is None else payload)
            response.status = status
            if url:
                response.url = url
            return response
        return stage(self.output, "v0.11.0+20261003", (self.spec,), opener)

    def test_exact_bytes_and_complete_manifest(self):
        self.run_stage()
        root = self.output / self.spec.repo / "build/capypkg"
        self.assertEqual((root / self.spec.asset).read_bytes(), self.payload)
        fields = parse_manifest(root / (self.spec.module_id + ".manifest"))
        self.assertEqual(fields["depends"], "")
        self.assertEqual(fields["payload_sha256"], self.spec.published_payload_sha256)

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_bytes(b"original")
        with self.assertRaises(FileExistsError):
            self.run_stage()
        self.assertEqual(marker.read_bytes(), b"original")

    def test_invalid_release_tag_leaves_no_output(self):
        with self.assertRaises(ValueError):
            stage(self.output, "main", (self.spec,))
        self.assertFalse(self.output.exists())

    def test_missing_immutable_pin_leaves_no_output(self):
        self.spec = replace(self.spec, uses_capyos_release_tag=True)
        with self.assertRaises(ManifestError):
            self.run_stage()
        self.assertFalse(self.output.exists())

    def test_modified_payload_rejected(self):
        with self.assertRaises(ManifestError):
            self.run_stage(b"x" * len(self.payload))

    def test_short_payload_rejected(self):
        with self.assertRaises(ManifestError):
            self.run_stage(self.payload[:-1])

    def test_oversized_payload_rejected(self):
        with self.assertRaises(ManifestError):
            self.run_stage(self.payload + b"x")

    def test_http_downgrade_rejected(self):
        with self.assertRaises(ManifestError):
            self.run_stage(url="http://example.test/payload")

    def test_non_200_rejected(self):
        with self.assertRaises(ManifestError):
            self.run_stage(status=206)


if __name__ == "__main__":
    unittest.main()
