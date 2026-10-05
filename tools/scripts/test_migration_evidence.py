"""Production migration proof cannot omit a boot, hash, confirmation or rollback."""
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import smoke_x64_update_ab_contract as contract
from test_update_ab_contract import _production_evidence
from smoke_x64_helpers import run_cmd_expect_prompt
from smoke_x64_migration import BRIDGE_ROUTE, configure_bridge_route, prepare_bridge_material
from smoke_x64_update_ab_flow import confirm_boot_health


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
    @patch("smoke_x64_update_ab_flow.assert_slot_state")
    @patch("smoke_x64_update_ab_flow.run_cmd")
    def test_health_gate_requires_commit_receipt_and_durable_slot_state(self, command, state):
        class Console:
            def marker(self): return 17
            def text_since(self, marker): return "health=confirmed [ACTIVE]\n"
            def wait_for(self, expected, **kwargs):
                self.expected, self.kwargs = expected, kwargs
        console = Console()
        confirm_boot_health(console, 10)
        self.assertEqual(console.expected, contract.CONFIRM_SUMMARY)
        self.assertEqual(console.kwargs["start_at"], 17)
        self.assertEqual(command.call_args.args[1], "update-confirm-health")
        self.assertEqual([call.args[2] for call in state.call_args_list],
                         ["health=confirmed [ACTIVE]"])

    @patch("smoke_x64_update_ab_flow.assert_slot_state")
    @patch("smoke_x64_update_ab_flow.run_cmd")
    def test_health_gate_rejects_rollback_still_armed(self, command, state):
        class Console:
            def marker(self): return 17
            def wait_for(self, *args, **kwargs): pass
            def text_since(self, marker): return "health=confirmed [ACTIVE]\nRollback pending: yes"
        with self.assertRaisesRegex(RuntimeError, "did not disarm"):
            confirm_boot_health(Console(), 10)

    @patch("smoke_x64_update_ab_flow.assert_slot_state")
    @patch("smoke_x64_update_ab_flow.run_cmd")
    def test_health_gate_rejects_missing_durable_commit_receipt(self, command, state):
        class Console:
            def marker(self): return 17
            def wait_for(self, *args, **kwargs): raise TimeoutError("missing commit receipt")
        with self.assertRaises(TimeoutError):
            confirm_boot_health(Console(), 10)
        state.assert_not_called()

    def offline(self):
        fields = migration_evidence()
        fields["format"] = contract.PRODUCTION_OFFLINE_EVIDENCE_FORMAT
        fields["bridge_manifest_url"] = fields["payload_url"].removesuffix("capyos64.bin") + "bridge.ini"
        del fields["bridge_route_retired"]
        fields.update(offline_source_sha256="aa" * 32, offline_data_sha256="bb" * 32,
                      offline_signature_verified="yes", offline_original_unchanged="yes",
                      offline_protected_regions_unchanged="yes")
        return fields

    def test_offline_proof_does_not_claim_bridge_route_was_used(self):
        fields = self.offline()
        contract.validate_production_evidence(fields)
        self.assertEqual(contract.parse_evidence(contract.render_production_evidence(fields)), fields)
        self.assertNotIn("bridge_route_retired", fields)

    def test_offline_proof_refuses_missing_rollback_signature_and_preservation(self):
        for key in self.offline():
            with self.subTest(missing=key), self.assertRaises(ValueError):
                fields = self.offline()
                del fields[key]
                contract.validate_production_evidence(fields)
        for key, bad in (("offline_original_unchanged", "no"), ("offline_signature_verified", "no"),
                         ("offline_protected_regions_unchanged", "no"), ("offline_data_sha256", "bad"),
                         ("bridge_manifest_url", BRIDGE_ROUTE), ("rollback_reported", "no"),
                         ("boots_observed", "4"), ("lab_override_absent", "no")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                fields = self.offline()
                fields[key] = bad
                contract.validate_production_evidence(fields)

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

    def test_editor_failure_returns_immediately_without_accepting_shell_prompt(self):
        class Console:
            proc = None
            def marker(self): return 0
            def send_line(self, line): self.line = line
            def serial_text_since(self, marker):
                return (self.line + "\n[erro] could not create file\n"
                        "admin@capyos64>~> ")
        with self.assertRaisesRegex(RuntimeError, "returned to the shell before editor"):
            run_cmd_expect_prompt(Console(), "open /system/update/repository.ini", 0,
                                  "open> ", require_command_echo=True)


if __name__ == "__main__":
    unittest.main()
