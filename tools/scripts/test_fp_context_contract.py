"""The full FP gate cannot accept old busy-only or partial-register evidence."""
from pathlib import Path
import tempfile
import unittest
import re

from smoke_x64_preemptive_user_2task import (
    SUCCESS_MARKERS, FP_FULL_MARKERS, all_markers_present, poll_debugcon,
)
from smoke_x64_vmware import markers_match, wait_for_markers


class FPContextContract(unittest.TestCase):
    def test_lifecycle_requires_all_three_outcomes_and_rejects_corruption(self):
        markers = ["[fp-child] inherited+isolated", "[fp-parent] preserved",
                   "[fp-exec] reset"]
        for missing in markers:
            text = "\n".join(m for m in markers if m != missing)
            self.assertFalse(all_markers_present(text, 2, fp_lifecycle=True))
        text = "\n".join(markers)
        self.assertTrue(all_markers_present(text, 2, fp_lifecycle=True))
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "debugcon.log"
            log.write_text(text + "\n[fp-corrupt]")
            self.assertEqual(poll_debugcon(log, 0.2, 2, fp_lifecycle=True),
                             (False, "[fp-corrupt]"))

    def test_vmware_concurrent_markers_keep_missing_and_failure_guards(self):
        self.assertFalse(markers_match("beta alpha", ("alpha", "beta")))
        self.assertTrue(markers_match("beta alpha", ("alpha", "beta"), True))
        self.assertFalse(markers_match("alpha", ("alpha", "beta"), True))
        self.assertFalse(markers_match("alpha", ("alpha", "alpha"), True))
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "serial.log"
            log.write_text("beta alpha [fp-corrupt]")
            ok, _, reason = wait_for_markers(log, ("alpha", "beta"), 0.2, 0.01,
                                            ("[fp-corrupt]",), True)
            self.assertFalse(ok)
            self.assertEqual(reason, "[fp-corrupt]")

    def setUp(self):
        self.busy = SUCCESS_MARKERS[0] + "\n" + "[busyU0]\n[busyU1]\n" * 64

    def test_old_busy_gate_is_not_full_fp_evidence(self):
        self.assertTrue(all_markers_present(self.busy, 2))
        self.assertFalse(all_markers_present(self.busy, 2, True))

    def test_both_completed_profiles_are_required(self):
        self.assertFalse(all_markers_present(self.busy + FP_FULL_MARKERS[0], 2, True))
        full = self.busy + "\n".join(FP_FULL_MARKERS)
        self.assertTrue(all_markers_present(full, 2, True))
        self.assertFalse(all_markers_present(full.replace("cycles=64", "cycles=1"), 2, True))
        self.assertFalse(all_markers_present(full.replace("xmm0-15", "xmm0"), 2, True))

    def test_failure_beats_completed_profiles(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "debugcon.log"
            log.write_text(self.busy + "\n".join(FP_FULL_MARKERS) + "\n[fp-corrupt]\n")
            self.assertEqual(poll_debugcon(log, 0.2, 2, True), (False, "[fp-corrupt]"))

    def test_cooperative_completion_cannot_pass_timer_gate(self):
        full = self.busy + "\n".join(FP_FULL_MARKERS)
        required = ["[fp-timer0] no-yield", "[fp-timer1] no-yield",
                    "[fp-timer] quantum-expirations=128"]
        self.assertFalse(all_markers_present(full, 2, True, True))
        for missing in required:
            text = full + "\n".join(m for m in required if m != missing)
            self.assertFalse(all_markers_present(text, 2, True, True))
        self.assertTrue(all_markers_present(full + "\n".join(required), 2, True, True))

    def test_timer_completion_cannot_hide_corruption(self):
        text = self.busy + "\n".join(FP_FULL_MARKERS) + (
            "\n[fp-timer0] no-yield\n[fp-timer1] no-yield\n"
            "[fp-timer] quantum-expirations=128\n[fp-corrupt]\n")
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "debugcon.log"
            log.write_text(text)
            self.assertEqual(poll_debugcon(log, 0.2, 2, False, True),
                             (False, "[fp-corrupt]"))

    def test_real_assembly_entries_use_verified_boundaries(self):
        root = Path(__file__).resolve().parents[2]
        syscall = (root / "src/arch/x86_64/syscall/syscall_entry.S").read_text()
        interrupt = (root / "src/arch/x86_64/cpu/interrupts_asm.S").read_text()
        self.assertIn("call x64_syscall_dispatch_fp", syscall.split(".size syscall_entry", 1)[0])
        self.assertIn("call x64_exception_dispatch_fp", interrupt.split(".global x64_user_first_dispatch", 1)[0])

    def test_syscall_frame_rip_rsp_match_c_layout(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/arch/x86_64/syscall/syscall_entry.S").read_text()
        entry = source.split("syscall_entry:\n", 1)[1].split(".size syscall_entry", 1)[0]
        pushes = [operand.strip() for operand in re.findall(r"^\s*pushq\s+([^#\n]+)", entry, re.M)]
        self.assertEqual(pushes, ["%r11", "%gs:CPU_LOCAL_USER_RSP_SCRATCH_OFFSET", "%rcx",
            "%r11", "%rcx", "%r9", "%r8", "%r10", "%rdx", "%rsi", "%rdi", "%rax"])
        instructions = [line.split("#", 1)[0].strip() for line in entry.splitlines()]
        self.assertIn("popq %r11\naddq $8, %rsp\npopq %rsp", "\n".join(instructions))

    def test_fork_downgrades_invalidate_the_source_tlb_on_all_exits(self):
        # Structural guard; the fork/crash VM gate supplies actual CPU evidence.
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/memory/vmm.c").read_text()
        helper = source.split("static void clone_sync_source_tlb", 1)[1].split(
            "struct vmm_address_space *vmm_clone_address_space", 1)[0]
        self.assertIn("write_cr3(current)", helper)
        clone = source.split("struct vmm_address_space *vmm_clone_address_space", 1)[1].split(
            "/* Recursive helper", 1)[0]
        self.assertEqual(clone.count("clone_sync_source_tlb(src);"), 3)
        self.assertRegex(clone, r"clone_sync_source_tlb\(src\);\s+return dst;")
        self.assertEqual(len(re.findall(r"clone_sync_source_tlb\(src\);\s+"
            r"vmm_destroy_address_space\(dst\);\s+return NULL;", clone)), 2)


if __name__ == "__main__":
    unittest.main()
