#!/usr/bin/env python3
"""Unit and repository contract tests for audit_version_manifest.py."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

import audit_version_manifest as audit  # noqa: E402


STABLE_EXTENDED = "0.9.2+20260826"
PIN = f"v{STABLE_EXTENDED}"
CANONICAL_URL = audit.canonical_modules_index_url(PIN)


def version_yaml(*, pin: str = PIN, url: str = CANONICAL_URL) -> str:
    return (
        "channels:\n"
        "  alpha:\n"
        "    current: 0.8.0-alpha.999\n"
        "    extended: 0.8.0-alpha.999+20990101\n"
        "  stable:\n"
        "    current: 0.9.2\n"
        f"    extended: {STABLE_EXTENDED}\n"
        "modules_index:\n"
        f'  pin: "{pin}"\n'
        f'  url: "{url}"\n'
    )


def modules_c(url: str = CANONICAL_URL) -> str:
    return (
        "/* CAPYOS_DEFAULT_MODULES_INDEX_URL in comments is not a field. */\n"
        "#ifndef CAPYOS_DEFAULT_MODULES_INDEX_URL\n"
        "#define CAPYOS_DEFAULT_MODULES_INDEX_URL \\\n"
        f'    "{url}"\n'
        "#endif\n"
    )


def makefile(url: str = CANONICAL_URL) -> str:
    return (
        "# SMOKE_X64_MODULES_INDEX_URL ?= https://invalid.example/comment\n"
        f"SMOKE_X64_MODULES_INDEX_URL ?= {url}\n"
    )


class ModulesIndexAuditTests(unittest.TestCase):
    def assert_contract_error(
        self, expected: str, yaml_text: str, c_text: str, make_text: str
    ) -> None:
        errors = audit.audit_modules_index_contract(yaml_text, c_text, make_text)
        self.assertTrue(
            any(expected in error for error in errors),
            f"expected {expected!r} in {errors!r}",
        )

    def test_positive_contract(self) -> None:
        self.assertEqual(
            [],
            audit.audit_modules_index_contract(
                version_yaml(), modules_c(), makefile()
            ),
        )

    def test_current_repository_contract(self) -> None:
        self.assertEqual(
            [],
            audit.audit_modules_index_contract(
                (REPO_ROOT / "VERSION.yaml").read_text(encoding="utf-8"),
                (REPO_ROOT / "src/config/first_boot/modules.c").read_text(
                    encoding="utf-8"
                ),
                (REPO_ROOT / "Makefile").read_text(encoding="utf-8"),
            ),
        )

    def test_pin_must_equal_stable_extended(self) -> None:
        self.assert_contract_error(
            "modules_index.pin",
            version_yaml(pin="v0.9.1+20260825"),
            modules_c(),
            makefile(),
        )

    def test_yaml_url_must_be_derived_from_pin(self) -> None:
        self.assert_contract_error(
            "modules_index.url",
            version_yaml(url="https://example.invalid/modules-index.txt"),
            modules_c(),
            makefile(),
        )

    def test_runtime_url_must_be_canonical(self) -> None:
        self.assert_contract_error(
            "CAPYOS_DEFAULT_MODULES_INDEX_URL",
            version_yaml(),
            modules_c("https://example.invalid/runtime.txt"),
            makefile(),
        )

    def test_smoke_url_must_be_canonical(self) -> None:
        self.assert_contract_error(
            "SMOKE_X64_MODULES_INDEX_URL",
            version_yaml(),
            modules_c(),
            makefile("https://example.invalid/smoke.txt"),
        )

    def test_comments_cannot_satisfy_runtime_field(self) -> None:
        self.assert_contract_error(
            "campo ausente: CAPYOS_DEFAULT_MODULES_INDEX_URL",
            version_yaml(),
            "// #define CAPYOS_DEFAULT_MODULES_INDEX_URL \\\n"
            f'//     "{CANONICAL_URL}"\n',
            makefile(),
        )

    def test_comments_cannot_satisfy_smoke_field(self) -> None:
        self.assert_contract_error(
            "campo ausente: SMOKE_X64_MODULES_INDEX_URL",
            version_yaml(),
            modules_c(),
            f"# SMOKE_X64_MODULES_INDEX_URL ?= {CANONICAL_URL}\n",
        )

    def test_nested_pin_cannot_satisfy_direct_field(self) -> None:
        yaml_text = version_yaml().replace(
            f'  pin: "{PIN}"\n', f'  nested:\n    pin: "{PIN}"\n', 1
        )
        self.assert_contract_error(
            "campo ausente: modules_index.pin", yaml_text, modules_c(), makefile()
        )

    def test_duplicate_smoke_field_fails_closed(self) -> None:
        self.assert_contract_error(
            "campo duplicado: SMOKE_X64_MODULES_INDEX_URL",
            version_yaml(),
            modules_c(),
            makefile() + f"SMOKE_X64_MODULES_INDEX_URL ?= {CANONICAL_URL}\n",
        )

    def test_missing_stable_channel_fails_closed(self) -> None:
        yaml_text = version_yaml().replace("  stable:\n", "  preview:\n", 1)
        self.assert_contract_error(
            "canal ausente: channels.stable", yaml_text, modules_c(), makefile()
        )


if __name__ == "__main__":
    unittest.main()
