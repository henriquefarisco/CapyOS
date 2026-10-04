"""Production migration proof cannot omit a boot, hash, confirmation or rollback."""
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import smoke_x64_update_ab_contract as contract
from test_update_ab_contract import _production_evidence
from smoke_x64_helpers import run_cmd_expect_prompt
from smoke_x64_migration import BRIDGE_ROUTE, configure_bridge_route, prepare_bridge_material


def migration_evidence():
    fields = _production_evidence(
        format=contract.PRODUCTION_MIGRATION_EVIDENCE_FORMAT,
        predecessor_version="0.10.0+20260904", manifest_version="0.11.3+20261004",
        release_tag="v0.11.3+20261004", payload_size="7400000",
        payload_url="https://github.com/henriquefarisco/CapyOS/releases/download/v0.11.3+20261004/capyos64.bin",
        first_attempt_slot="0", second_attempt_slot="0", boots_observed="5")
    fields.update(bridge_version="0.11.2+20261004", bridge_manifest_url=BRIDGE_ROUTE,
                  bridge_payload_url=fields["payload_url"].replace("capyos64.bin", "capyos-bridge64.bin"),
                  bridge_payload_size="4067152", bridge_payload_sha256="dd" * 32,
                  bridge_attempt_slot="1", bridge_health_confirmed="yes", bridge_route_retired="yes")
    return fields


class MigrationEvidenceTests(unittest.TestCase):
    def test_complete_proof_roundtrip(self):
        fields = migration_evidence()
        contract.validate_production_evidence(fields)
        self.assertEqual(contract.parse_evidence(contract.render_production_evidence(fields)), fields)

    def test_every_required_field_is_mandatory(self):
        for key in migration_evidence():
            with self.subTest(key=key), self.assertRaises(ValueError):
                fields = migration_evidence()
                del fields[key]
                contract.validate_production_evidence(fields)

    def test_bad_slots_order_routes_hash_and_health_fail_closed(self):
        for key, bad in (("first_attempt_slot", "1"), ("second_attempt_slot", "1"),
                         ("bridge_attempt_slot", "0"), ("boots_observed", "4"),
                         ("bridge_version", "0.10.0+20260904"),
                         ("bridge_version", "0.11.3+20261004"),
                         ("bridge_payload_size", "4243457"), ("bridge_payload_sha256", "bad"),
                         ("bridge_manifest_url", "https://evil.example/bridge.ini"),
                         ("bridge_payload_url", "https://evil.example/capyos-bridge64.bin"),
                         ("bridge_health_confirmed", "no"), ("bridge_route_retired", "no"),
                         ("rollback_reported", "no"), ("health_confirmed", "no"),
                         ("lab_override_absent", "no")):
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                fields = migration_evidence()
                fields[key] = bad
                contract.validate_production_evidence(fields)

    @patch("smoke_x64_migration.run_cmd")
    @patch("smoke_x64_migration.run_cmd_expect_prompt")
    def test_guest_route_uses_existing_editor_with_per_line_ack(self, editor, command):
        configure_bridge_route(object(), 10)
        self.assertEqual(editor.call_count, 6)
        self.assertTrue(all(call.kwargs["require_command_echo"] for call in editor.call_args_list))
        self.assertEqual(editor.call_args_list[-1].args[1], f"remote_manifest={BRIDGE_ROUTE}")
        self.assertEqual(command.call_args_list[0].args[1], ".wq")
        self.assertEqual(command.call_args_list[1].args[1], "print-file /system/update/repository.ini")

    def test_no_bridge_is_backward_compatible_but_partial_or_lab_bridge_is_refused(self):
        args = SimpleNamespace(production=True)
        self.assertIsNone(prepare_bridge_material(args, None))
        args.production_bridge_manifest = Path("bridge.ini")
        with self.assertRaisesRegex(ValueError, "all three"):
            prepare_bridge_material(args, None)
        args.production = False
        args.production_bridge_payload = Path("capyos-bridge64.bin")
        args.production_bridge_version = "0.11.2+20261004"
        with self.assertRaisesRegex(ValueError, "production mode"):
            prepare_bridge_material(args, None)

    def test_editor_ack_requires_current_command_echo_not_stale_prompt(self):
        class Console:
            proc = None
            def marker(self): return 0
            def send_line(self, line): self.line = line
            def serial_text_since(self, marker): return "open> stale\n"
        with self.assertRaisesRegex(TimeoutError, "command echo"):
            run_cmd_expect_prompt(Console(), "channel=stable", 0, "open> ",
                                  require_command_echo=True)


if __name__ == "__main__":
    unittest.main()
