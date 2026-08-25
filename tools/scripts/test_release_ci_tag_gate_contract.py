from __future__ import annotations

import argparse
import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from tools.scripts.release_ci_tag_gate import (
    parse_version_yaml,
    validate_version_contract,
)


class ReleaseCiTagGateContractTests(unittest.TestCase):
    def test_stable_channel_is_selected_instead_of_first_alpha_block(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            version_yaml = root / "VERSION.yaml"
            version_yaml.write_text(
                "channels:\n"
                "  alpha:\n"
                "    current: 0.8.0-alpha.321\n"
                "    extended: 0.8.0-alpha.321+20260821\n"
                "  stable:\n"
                "    current: 0.9.1\n"
                "    extended: 0.9.1+20260825\n",
                encoding="utf-8",
            )
            header = root / "version.h"
            header.write_text(
                '#define CAPYOS_VERSION_CHANNEL "stable"\n'
                '#define CAPYOS_VERSION_PRERELEASE ""\n'
                '#define CAPYOS_VERSION_EXTENDED "0.9.1"\n'
                '#define CAPYOS_VERSION_FULL "0.9.1+20260825"\n'
                '#define CAPYOS_VERSION_ALPHA "0.8.0-alpha.321"\n'
                '#define CAPYOS_VERSION_STABLE "0.9.1"\n',
                encoding="utf-8",
            )
            readme = root / "README.md"
            readme.write_text("Versao de referencia: `0.9.1`\n", encoding="utf-8")
            release_note = root / "release.md"
            release_note.write_text("# CapyOS 0.9.1+20260825\n", encoding="utf-8")
            args = argparse.Namespace(
                version_yaml=version_yaml,
                version_header=header,
                readme=readme,
                release_note=release_note,
                release_tag="v0.9.1+20260825",
            )

            self.assertEqual(
                parse_version_yaml(version_yaml, "stable"),
                (0, "0.9.1", "0.9.1+20260825"),
            )
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                self.assertEqual(validate_version_contract(args), 0)

            header.write_text(
                header.read_text(encoding="utf-8").replace(
                    'CAPYOS_VERSION_STABLE "0.9.1"',
                    'CAPYOS_VERSION_STABLE "0.9.0"',
                ),
                encoding="utf-8",
            )
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                self.assertNotEqual(validate_version_contract(args), 0)


if __name__ == "__main__":
    unittest.main()
