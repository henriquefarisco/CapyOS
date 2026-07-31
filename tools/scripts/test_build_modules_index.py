#!/usr/bin/env python3
"""Adversarial unit tests for the official modules-index builder."""

from __future__ import annotations

import hashlib
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import build_modules_index as builder  # noqa: E402
import build_local_capypkg_bundle as local_bundle  # noqa: E402
import modules_index_catalog as catalog  # noqa: E402

RELEASE_TAG = "v0.8.0-alpha.320+20260730"


def _manifest_text(
    spec: catalog.ModuleSpec, payload: bytes, **overrides: str
) -> str:
    fields = {
        "name": spec.module_id,
        "version": spec.version,
        "summary": f"fixture for {spec.module_id}",
        "payload_url": catalog.expected_payload_url(spec, RELEASE_TAG),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "payload_size": str(len(payload)),
        "install_root": spec.install_root,
        "depends": ",".join(spec.dependencies),
    }
    fields.update(overrides)
    return "\n".join(
        [*(f"{key}={value}" for key, value in fields.items()), "---", ""]
    )


def populate_workspace(root: Path) -> tuple[Path, dict[str, Path], dict[str, Path]]:
    workspace = root / "workspace"
    manifests: dict[str, Path] = {}
    payloads: dict[str, Path] = {}
    for repo in catalog.DEFAULT_REPOS:
        (workspace / repo).mkdir(parents=True, exist_ok=True)
    for spec in catalog.MODULE_SPECS:
        if spec.repo == "CapyLang":
            # This empty directory exercises the old broken fallback case.
            (workspace / spec.repo / "build/capypkg").mkdir(parents=True)
            package_dir = workspace / spec.repo / "target/capypkg"
        else:
            package_dir = workspace / spec.repo / "build/capypkg"
        package_dir.mkdir(parents=True, exist_ok=True)
        payload = f"fixture-payload:{spec.module_id}".encode("ascii")
        payload_path = package_dir / spec.asset
        payload_path.write_bytes(payload)
        manifest_path = package_dir / f"{spec.module_id}.manifest"
        manifest_path.write_text(
            _manifest_text(spec, payload), encoding="utf-8", newline="\n"
        )
        manifests[spec.module_id] = manifest_path
        payloads[spec.module_id] = payload_path
    return workspace, manifests, payloads


def replace_field(path: Path, key: str, value: str) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    replaced = False
    for index, line in enumerate(lines):
        if line.startswith(f"{key}="):
            lines[index] = f"{key}={value}"
            replaced = True
            break
    if not replaced:
        lines.insert(-1, f"{key}={value}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def index_entry(text: str, module_id: str) -> str:
    marker = f"name={module_id}\n"
    if marker not in text:
        raise AssertionError(f"missing index entry for {module_id}")
    return text.split(marker, 1)[1].split("---", 1)[0]


class BuildModulesIndexTests(unittest.TestCase):
    def _build(self, workspace: Path, output: Path) -> int:
        return builder.build_index(
            workspace,
            list(catalog.DEFAULT_REPOS),
            output,
            release_tag=RELEASE_TAG,
        )

    def test_legacy_local_bundle_import_surface_is_preserved(self) -> None:
        self.assertEqual(tuple(local_bundle.DEFAULT_REPOS), catalog.DEFAULT_REPOS)
        self.assertIs(local_bundle.ManifestError, builder.ManifestError)
        self.assertTrue(callable(local_bundle.find_manifest_files))
        self.assertTrue(callable(local_bundle.parse_manifest))
        self.assertTrue(callable(local_bundle.validate_manifest))
        self.assertTrue(callable(local_bundle.emit_entry))

    def test_capyui_2_24_2_release_metadata_is_exact(self) -> None:
        expected = {
            "org.capyos.ui.widget-core": (
                "b0e7e07335f45faea42c50ead69e8f51"
                "c59fe9c96d6cf928fcef3218ab85ce04",
                1177600,
            ),
            "org.capyos.ui.desktop-session": (
                "3bcc5dda0023f417dff9292ed4a3a492"
                "48278c691973c191d0ce08dbd1f49de7",
                1413120,
            ),
        }
        for module_id, metadata in expected.items():
            with self.subTest(module_id=module_id):
                spec = catalog.MODULE_BY_ID[module_id]
                self.assertEqual(spec.version, "2.24.2")
                self.assertEqual(catalog.pinned_payload_metadata(spec), metadata)
                self.assertIn("/download/v2.24.2/", catalog.expected_payload_url(spec))

    def test_success_builds_exact_inventory_and_capylang_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, _, payloads = populate_workspace(Path(directory))
            output = Path(directory) / "modules-index.txt"
            self.assertEqual(self._build(workspace, output), 0)
            text = output.read_text(encoding="utf-8")
            self.assertEqual(text.count("\nname="), catalog.EXPECTED_MODULE_COUNT)
            self.assertEqual(text.count("\nofficial=1"), catalog.EXPECTED_MODULE_COUNT)
            self.assertIn("name=org.capyos.lang.runtime", text)

            for spec in catalog.MODULE_SPECS:
                entry = index_entry(text, spec.module_id)
                pinned_metadata = catalog.pinned_payload_metadata(spec)
                local_payload = payloads[spec.module_id].read_bytes()
                local_sha256 = hashlib.sha256(local_payload).hexdigest()
                if pinned_metadata is None:
                    self.assertIn(f"payload_sha256={local_sha256}\n", entry)
                    self.assertIn(f"payload_size={len(local_payload)}\n", entry)
                else:
                    pinned_sha256, pinned_size = pinned_metadata
                    self.assertNotEqual(local_sha256, pinned_sha256)
                    self.assertIn(f"payload_sha256={pinned_sha256}\n", entry)
                    self.assertIn(f"payload_size={pinned_size}\n", entry)
                    self.assertNotIn(f"payload_sha256={local_sha256}\n", entry)

    def test_external_local_signature_is_not_attached_to_pinned_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, manifests, _ = populate_workspace(Path(directory))
            module_id = "org.capyos.agent.core"
            replace_field(
                manifests[module_id], "signature_ed25519", "a" * 128
            )
            output = Path(directory) / "modules-index.txt"
            self.assertEqual(self._build(workspace, output), 0)
            entry = index_entry(output.read_text(encoding="utf-8"), module_id)
            self.assertNotIn("signature_ed25519=", entry)

    def test_missing_repo_or_manifest_fails_without_output(self) -> None:
        for missing in ("repo", "manifest"):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, _ = populate_workspace(Path(directory))
                if missing == "repo":
                    shutil.rmtree(workspace / "CapyAgent")
                else:
                    manifests["org.capyos.agent.core"].unlink()
                output = Path(directory) / "modules-index.txt"
                with self.assertRaises(builder.ManifestError):
                    self._build(workspace, output)
                self.assertFalse(output.exists())

    def test_unknown_duplicate_and_extra_manifests_fail(self) -> None:
        for case in ("unknown-field", "duplicate-field", "extra-manifest"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, _ = populate_workspace(Path(directory))
                manifest = manifests["org.capyos.agent.core"]
                if case == "unknown-field":
                    text = manifest.read_text(encoding="utf-8")
                    manifest.write_text(
                        text.replace("---", "surprise=value\n---"),
                        encoding="utf-8",
                    )
                elif case == "duplicate-field":
                    text = manifest.read_text(encoding="utf-8")
                    manifest.write_text(
                        text.replace(
                            "version=0.0.10",
                            "version=0.0.10\nversion=0.0.10",
                        ),
                        encoding="utf-8",
                    )
                else:
                    (manifest.parent / "unknown.manifest").write_text(
                        manifest.read_text(encoding="utf-8").replace(
                            "org.capyos.agent.core", "org.capyos.unknown"
                        ),
                        encoding="utf-8",
                    )
                with self.assertRaises(builder.ManifestError):
                    self._build(workspace, Path(directory) / "index.txt")

    def test_duplicate_manifest_across_build_and_target_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, manifests, _ = populate_workspace(Path(directory))
            source = manifests["org.capyos.lang.runtime"]
            duplicate_dir = workspace / "CapyLang/build/capypkg"
            (duplicate_dir / source.name).write_text(
                source.read_text(encoding="utf-8"), encoding="utf-8"
            )
            shutil.copy2(
                source.parent / catalog.MODULE_BY_ID[
                    "org.capyos.lang.runtime"
                ].asset,
                duplicate_dir,
            )
            with self.assertRaisesRegex(builder.ManifestError, "duplicate module"):
                self._build(workspace, Path(directory) / "index.txt")

    def test_payload_size_must_be_canonical_positive_and_bounded(self) -> None:
        values = ("0", "01", str(catalog.RUNTIME_PAYLOAD_BYTES + 1))
        for value in values:
            with self.subTest(value=value), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, _ = populate_workspace(Path(directory))
                replace_field(
                    manifests["org.capyos.agent.core"], "payload_size", value
                )
                with self.assertRaisesRegex(builder.ManifestError, "payload_size"):
                    self._build(workspace, Path(directory) / "index.txt")

    def test_runtime_field_limits_and_dependency_count_are_enforced(self) -> None:
        cases = {
            "version": "v" * (catalog.RUNTIME_VERSION_BYTES + 1),
            "depends": ",".join(
                f"org.capyos.fake.dep-{index}"
                for index in range(catalog.RUNTIME_MAX_DEPS + 1)
            ),
        }
        for field, value in cases.items():
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, _ = populate_workspace(Path(directory))
                replace_field(manifests["org.capyos.agent.core"], field, value)
                with self.assertRaises(builder.ManifestError):
                    self._build(workspace, Path(directory) / "index.txt")

    def test_catalog_version_deps_root_official_and_url_are_exact(self) -> None:
        cases = {
            "version": "0.0.11",
            "depends": "org.capyos.ui.widget-core",
            "install_root": "/var/capypkg/wrong",
            "official": "0",
            "payload_url": (
                "https://github.com/henriquefarisco/CapyAgent/releases/"
                "download/v0.0.9/wrong.bin"
            ),
            "repo": "CapyUI",
        }
        for field, value in cases.items():
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, _ = populate_workspace(Path(directory))
                replace_field(manifests["org.capyos.agent.core"], field, value)
                with self.assertRaises(builder.ManifestError):
                    self._build(workspace, Path(directory) / "index.txt")

    def test_missing_hash_or_size_mismatched_local_payload_fails(self) -> None:
        for case in ("missing", "hash", "size"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                workspace, manifests, payloads = populate_workspace(Path(directory))
                module_id = "org.capyos.agent.core"
                if case == "missing":
                    payloads[module_id].unlink()
                elif case == "hash":
                    replace_field(manifests[module_id], "payload_sha256", "0" * 64)
                else:
                    current = payloads[module_id].stat().st_size
                    replace_field(manifests[module_id], "payload_size", str(current + 1))
                with self.assertRaises(builder.ManifestError):
                    self._build(workspace, Path(directory) / "index.txt")

    def test_release_tag_controls_capyai_url(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, _, _ = populate_workspace(Path(directory))
            with self.assertRaisesRegex(builder.ManifestError, "payload_url"):
                builder.build_index(
                    workspace,
                    list(catalog.DEFAULT_REPOS),
                    Path(directory) / "index.txt",
                    release_tag="v0.8.0-alpha.999+20990101",
                )


if __name__ == "__main__":
    unittest.main()
