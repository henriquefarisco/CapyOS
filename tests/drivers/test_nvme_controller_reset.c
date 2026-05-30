/* Host tests for the NVMe controller-reset pure logic
 * (Sub-slice 3E.5.B). Exercises src/drivers/nvme/nvme_reset.c —
 * no MMIO, no kernel state, runnable under the standard host
 * runner.
 *
 * The tests lock the BUG #2 fix from alpha.252: after the CC.EN
 * toggle the driver MUST recreate I/O CQ first and then I/O SQ.
 * The live driver (`nvme_controller_reset`) drives the planner in
 * a loop; these tests pin the planner contract directly so any
 * regression that inverts the order or skips a step shows up
 * before VMware smoke. */

#include "drivers/nvme/nvme_reset.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[nvme-reset] FAIL: %s\n", msg);
  g_failures++;
}

/* === Queue-state reprime ============================================ */

static void test_reprime_zeros_heads_and_tails(void) {
  struct nvme_reset_queue_state qs;
  memset(&qs, 0xAA, sizeof(qs));
  nvme_reset_reprime_queue_state(&qs);
  if (qs.admin_sq_tail != 0u || qs.admin_cq_head != 0u ||
      qs.io_sq_tail != 0u || qs.io_cq_head != 0u) {
    fail("reprime must zero heads and tails");
  }
}

static void test_reprime_sets_phases_to_one(void) {
  struct nvme_reset_queue_state qs;
  memset(&qs, 0xAA, sizeof(qs));
  nvme_reset_reprime_queue_state(&qs);
  if (qs.admin_cq_phase != 1u || qs.io_cq_phase != 1u) {
    fail("reprime must set CQ phase trackers to 1");
  }
}

static void test_reprime_null_safe(void) {
  /* Must not crash. The only observable is the absence of a
   * segfault during this test. */
  nvme_reset_reprime_queue_state(NULL);
}

/* === CSTS.RDY predicates ============================================ */

static void test_csts_rdy_cleared(void) {
  if (!nvme_reset_csts_rdy_cleared(0u)) {
    fail("csts=0 must report RDY cleared");
  }
  if (nvme_reset_csts_rdy_cleared(0x1u)) {
    fail("csts with RDY bit set must NOT report cleared");
  }
  /* Other bits set, RDY clear -> cleared. */
  if (!nvme_reset_csts_rdy_cleared(0xFFFFFFFEu)) {
    fail("csts with other bits set but RDY clear must report cleared");
  }
}

static void test_csts_rdy_set(void) {
  if (nvme_reset_csts_rdy_set(0u)) {
    fail("csts=0 must NOT report RDY set");
  }
  if (!nvme_reset_csts_rdy_set(0x1u)) {
    fail("csts with RDY bit set must report set");
  }
  if (nvme_reset_csts_rdy_set(0xFFFFFFFEu)) {
    fail("csts with other bits set but RDY clear must NOT report set");
  }
}

static void test_csts_predicates_are_complementary(void) {
  /* For any 32-bit csts value, exactly one of cleared/set is true. */
  uint32_t samples[] = {0u, 1u, 0x1234u, 0x80000000u, 0xFFFFFFFFu};
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
    int cleared = nvme_reset_csts_rdy_cleared(samples[i]);
    int set = nvme_reset_csts_rdy_set(samples[i]);
    if (cleared == set) {
      fail("csts predicates must be complementary (XOR=1) for every input");
      return;
    }
  }
}

/* === CSTS.CFS (Controller Fatal Status) predicate ================== */

static void test_csts_fatal_clear_when_zero(void) {
  /* csts=0 -> not fatal, not RDY, RDY-cleared. */
  if (nvme_reset_csts_fatal(0u) != 0) {
    fail("csts=0 must NOT report fatal");
  }
}

static void test_csts_fatal_set_when_bit1_set(void) {
  /* csts=0x2 -> CFS bit set, RDY bit clear. Fatal state. */
  if (nvme_reset_csts_fatal(0x2u) != 1) {
    fail("csts with CFS bit must report fatal");
  }
}

static void test_csts_fatal_independent_of_rdy(void) {
  /* csts=0x3 -> both RDY and CFS set. Still fatal. */
  if (nvme_reset_csts_fatal(0x3u) != 1) {
    fail("csts with CFS+RDY must report fatal");
  }
  /* csts=0x1 -> only RDY, not fatal. */
  if (nvme_reset_csts_fatal(0x1u) != 0) {
    fail("csts with only RDY must NOT report fatal");
  }
}

static void test_csts_fatal_ignores_other_bits(void) {
  /* csts with many bits but CFS clear -> not fatal. SHST_MASK is
   * bits 2-3, NSSRO is bit 4, PP is bit 5, etc. None of those
   * should flip the fatal predicate. */
  uint32_t non_cfs = 0xFFFFFFFCu; /* every bit except 0 (RDY) and 1 (CFS) */
  if (nvme_reset_csts_fatal(non_cfs) != 0) {
    fail("csts with non-CFS upper bits must NOT report fatal");
  }
}

static void test_csts_fatal_msb_clear_does_not_imply(void) {
  /* csts=0x80000000 (some hypothetical high bit) -> not fatal. */
  if (nvme_reset_csts_fatal(0x80000000u) != 0) {
    fail("csts with high bit but no CFS must NOT report fatal");
  }
}

/* === Admin-action planner ========================================== */

static void test_planner_null_progress_returns_done(void) {
  if (nvme_reset_next_admin_action(NULL) != NVME_RESET_ADMIN_DONE) {
    fail("NULL progress must return DONE so the loop terminates");
  }
}

static void test_planner_empty_returns_create_io_cq(void) {
  /* BUG #2 fix lock: the FIRST admin action after the CC.EN toggle
   * MUST be Create I/O CQ. Anything else (DONE, CREATE_IO_SQ)
   * would either skip queue restoration entirely or violate the
   * "CQ exists before SQ" invariant the controller enforces. */
  struct nvme_reset_progress p = {0u, 0u};
  if (nvme_reset_next_admin_action(&p) != NVME_RESET_ADMIN_CREATE_IO_CQ) {
    fail("empty progress MUST return CREATE_IO_CQ");
  }
}

static void test_planner_after_cq_returns_create_io_sq(void) {
  struct nvme_reset_progress p = {1u, 0u};
  if (nvme_reset_next_admin_action(&p) != NVME_RESET_ADMIN_CREATE_IO_SQ) {
    fail("progress with CQ recreated MUST return CREATE_IO_SQ");
  }
}

static void test_planner_after_cq_and_sq_returns_done(void) {
  struct nvme_reset_progress p = {1u, 1u};
  if (nvme_reset_next_admin_action(&p) != NVME_RESET_ADMIN_DONE) {
    fail("progress with both queues recreated MUST return DONE");
  }
}

static void test_planner_sq_before_cq_forces_cq_first(void) {
  /* This shape is unreachable from the live driver (the loop sets
   * io_cq_recreated before io_sq_recreated), but the planner must
   * still defend the invariant: if somehow io_sq_recreated=1 while
   * io_cq_recreated=0, the next action MUST be CREATE_IO_CQ to
   * recover, never DONE. */
  struct nvme_reset_progress p = {0u, 1u};
  if (nvme_reset_next_admin_action(&p) != NVME_RESET_ADMIN_CREATE_IO_CQ) {
    fail("inverted progress (SQ before CQ) MUST force CQ recreation");
  }
}

static void test_planner_full_sequence_drives_two_actions_then_done(void) {
  /* Reproduces the live driver's drive loop. The planner must
   * return exactly two non-DONE actions and then terminate. This
   * pins the BUG #2 contract end-to-end: order CQ → SQ → DONE. */
  struct nvme_reset_progress p = {0u, 0u};
  enum nvme_reset_admin_action act;

  act = nvme_reset_next_admin_action(&p);
  if (act != NVME_RESET_ADMIN_CREATE_IO_CQ) {
    fail("step 1 of sequence MUST be CREATE_IO_CQ");
    return;
  }
  p.io_cq_recreated = 1u;

  act = nvme_reset_next_admin_action(&p);
  if (act != NVME_RESET_ADMIN_CREATE_IO_SQ) {
    fail("step 2 of sequence MUST be CREATE_IO_SQ");
    return;
  }
  p.io_sq_recreated = 1u;

  act = nvme_reset_next_admin_action(&p);
  if (act != NVME_RESET_ADMIN_DONE) {
    fail("step 3 of sequence MUST be DONE");
    return;
  }

  /* Idempotence: once DONE, repeated calls must stay DONE so the
   * driver loop terminates cleanly even if it polls once more. */
  if (nvme_reset_next_admin_action(&p) != NVME_RESET_ADMIN_DONE) {
    fail("DONE must be stable on repeated polls");
  }
}

static void test_planner_enum_values_are_stable(void) {
  /* The values are part of the host-testable contract: any new
   * action would force a smoke marker bump and a test review.
   * Pinning the underlying ints catches accidental reorders. */
  if ((int)NVME_RESET_ADMIN_DONE != 0) {
    fail("NVME_RESET_ADMIN_DONE must be 0");
  }
  if ((int)NVME_RESET_ADMIN_CREATE_IO_CQ != 1) {
    fail("NVME_RESET_ADMIN_CREATE_IO_CQ must be 1");
  }
  if ((int)NVME_RESET_ADMIN_CREATE_IO_SQ != 2) {
    fail("NVME_RESET_ADMIN_CREATE_IO_SQ must be 2");
  }
}

int run_nvme_controller_reset_tests(void) {
  g_failures = 0;
  test_reprime_zeros_heads_and_tails();
  test_reprime_sets_phases_to_one();
  test_reprime_null_safe();
  test_csts_rdy_cleared();
  test_csts_rdy_set();
  test_csts_predicates_are_complementary();
  test_csts_fatal_clear_when_zero();
  test_csts_fatal_set_when_bit1_set();
  test_csts_fatal_independent_of_rdy();
  test_csts_fatal_ignores_other_bits();
  test_csts_fatal_msb_clear_does_not_imply();
  test_planner_null_progress_returns_done();
  test_planner_empty_returns_create_io_cq();
  test_planner_after_cq_returns_create_io_sq();
  test_planner_after_cq_and_sq_returns_done();
  test_planner_sq_before_cq_forces_cq_first();
  test_planner_full_sequence_drives_two_actions_then_done();
  test_planner_enum_values_are_stable();
  if (g_failures == 0) printf("[tests] nvme_controller_reset OK\n");
  return g_failures;
}
