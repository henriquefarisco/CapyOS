/* Host tests for the AHCI dispatch pure logic (Slice 3F initial
 * extraction). Exercises src/drivers/storage/ahci_dispatch.c —
 * no MMIO, no kernel state, runnable under the standard host
 * runner.
 *
 * The tests lock the tick-classification precedence used by
 * ahci_exec_classified in ahci.c: CI cleared has higher precedence
 * than IS.TFES (the controller already retired the slot, so the
 * spin loop reports COMPLETED and the downstream classifier reads
 * the same IS/TFD bits to assign an error class). They also pin
 * the multi-slot completion fan-in helper that future fatias of
 * Slice 3F will use to dispatch multiple commands concurrently. */

#include "drivers/storage/ahci_dispatch.h"

#include <stdint.h>
#include <stdio.h>

static int g_failures = 0;

static void fail(const char *msg) {
  printf("[ahci-dispatch] FAIL: %s\n", msg);
  g_failures++;
}

/* === Tick classifier ============================================== */

static void test_inflight_when_ci_set_no_errors(void) {
  /* Slot 0 inflight, no errors -> INFLIGHT (continue spinning). */
  if (ahci_dispatch_classify_tick(0x1u, 0u, 0u, 0x1u) !=
      AHCI_DISPATCH_INFLIGHT) {
    fail("CI set + no errors must report INFLIGHT");
  }
}

static void test_completed_when_ci_cleared(void) {
  /* CI cleared with no other bits set -> COMPLETED. */
  if (ahci_dispatch_classify_tick(0u, 0u, 0u, 0x1u) !=
      AHCI_DISPATCH_COMPLETED) {
    fail("CI cleared must report COMPLETED");
  }
}

static void test_completed_when_ci_cleared_even_with_tfes(void) {
  /* Precedence rule: CI cleared takes priority over IS.TFES. The
   * controller already retired the slot; the host must release it
   * and let the downstream classifier assign an error class. */
  uint32_t tfes = (1u << 30);
  if (ahci_dispatch_classify_tick(0u, tfes, 0u, 0x1u) !=
      AHCI_DISPATCH_COMPLETED) {
    fail("CI cleared + IS.TFES must report COMPLETED");
  }
}

static void test_completed_when_ci_cleared_even_with_tfd_err(void) {
  /* Precedence rule: CI cleared takes priority over TFD.ERR. */
  uint32_t tfd_err = 0x1u;
  if (ahci_dispatch_classify_tick(0u, 0u, tfd_err, 0x1u) !=
      AHCI_DISPATCH_COMPLETED) {
    fail("CI cleared + TFD.ERR must report COMPLETED");
  }
}

static void test_aborted_when_ci_set_and_tfes(void) {
  /* CI still set + IS.TFES -> ABORTED. The slot must be released
   * and the classifier called with the same IS/TFD bits. */
  uint32_t tfes = (1u << 30);
  if (ahci_dispatch_classify_tick(0x1u, tfes, 0u, 0x1u) !=
      AHCI_DISPATCH_ABORTED) {
    fail("CI set + IS.TFES must report ABORTED");
  }
}

static void test_aborted_when_ci_set_and_tfd_err(void) {
  /* CI still set + TFD.ERR -> ABORTED. Either error bit alone is
   * sufficient to abort the inflight command. */
  uint32_t tfd_err = 0x1u;
  if (ahci_dispatch_classify_tick(0x1u, 0u, tfd_err, 0x1u) !=
      AHCI_DISPATCH_ABORTED) {
    fail("CI set + TFD.ERR must report ABORTED");
  }
}

static void test_aborted_when_ci_set_and_both_errors(void) {
  /* Both error bits set -> still ABORTED (the host treats either
   * as fatal for the inflight command). */
  uint32_t tfes = (1u << 30);
  uint32_t tfd_err = 0x1u;
  if (ahci_dispatch_classify_tick(0x1u, tfes, tfd_err, 0x1u) !=
      AHCI_DISPATCH_ABORTED) {
    fail("CI set + both error bits must report ABORTED");
  }
}

static void test_inflight_ignores_other_is_bits(void) {
  /* IS register has many bits (DHRS, PSS, DSS, ...); only TFES at
   * bit 30 promotes to ABORTED. Other bits are spurious for the
   * dispatch decision. */
  uint32_t spurious = 0x3FFFFFFFu; /* every bit except 30 */
  if (ahci_dispatch_classify_tick(0x1u, spurious, 0u, 0x1u) !=
      AHCI_DISPATCH_INFLIGHT) {
    fail("CI set + IS bits other than TFES must report INFLIGHT");
  }
}

static void test_inflight_ignores_other_tfd_bits(void) {
  /* TFD register has BSY (0x80) and DRQ (0x08) bits that the spin
   * loop does NOT treat as fatal during dispatch (idle-wait uses
   * those bits separately). Only TFD.ERR at bit 0 promotes to
   * ABORTED. */
  uint32_t bsy_drq = 0x88u; /* BSY + DRQ but no ERR */
  if (ahci_dispatch_classify_tick(0x1u, 0u, bsy_drq, 0x1u) !=
      AHCI_DISPATCH_INFLIGHT) {
    fail("CI set + TFD.BSY/DRQ but no ERR must report INFLIGHT");
  }
}

static void test_slot_bit_isolation(void) {
  /* The dispatcher must check ONLY the bit indicated by slot_bit.
   * Other slots being inflight or completed in CI must not affect
   * this slot's classification. */
  uint32_t multi_inflight = 0xFFFFFFFFu;
  /* Slot 3 is inflight (bit 3 set in CI) -> INFLIGHT. */
  if (ahci_dispatch_classify_tick(multi_inflight, 0u, 0u, (1u << 3)) !=
      AHCI_DISPATCH_INFLIGHT) {
    fail("dispatcher must isolate slot_bit from other inflight slots");
  }
  /* Slot 3 is NOT inflight (bit 3 cleared in CI = 0xFFFFFFF7) ->
   * COMPLETED. */
  uint32_t slot3_done = 0xFFFFFFFFu & ~(1u << 3);
  if (ahci_dispatch_classify_tick(slot3_done, 0u, 0u, (1u << 3)) !=
      AHCI_DISPATCH_COMPLETED) {
    fail("dispatcher must isolate slot_bit from other slots' state");
  }
}

static void test_enum_values_are_stable(void) {
  /* Numeric values are part of the contract: a regression that
   * reorders them would silently mis-route the dispatch switch in
   * ahci.c. Pinning the values catches that drift. */
  if ((int)AHCI_DISPATCH_INFLIGHT != 0) {
    fail("AHCI_DISPATCH_INFLIGHT must be 0");
  }
  if ((int)AHCI_DISPATCH_COMPLETED != 1) {
    fail("AHCI_DISPATCH_COMPLETED must be 1");
  }
  if ((int)AHCI_DISPATCH_ABORTED != 2) {
    fail("AHCI_DISPATCH_ABORTED must be 2");
  }
}

/* === Multi-slot completion fan-in ================================= */

static void test_completed_slots_empty_when_no_inflight(void) {
  /* No slots inflight -> no completions reported, even if CI changed. */
  if (ahci_dispatch_completed_slots(0xFFu, 0u, 0u) != 0u) {
    fail("empty inflight mask must yield zero completions");
  }
}

static void test_completed_slots_empty_when_nothing_cleared(void) {
  /* Same CI on both samples -> no completions. */
  if (ahci_dispatch_completed_slots(0xFFu, 0xFFu, 0xFFu) != 0u) {
    fail("unchanged CI must yield zero completions");
  }
}

static void test_completed_slots_detects_single_clear(void) {
  /* Slot 2 was inflight, CI cleared bit 2 -> reported as completed. */
  uint32_t inflight = (1u << 2);
  uint32_t prev = (1u << 2);
  uint32_t cur = 0u;
  if (ahci_dispatch_completed_slots(prev, cur, inflight) != (1u << 2)) {
    fail("single slot transition must be detected");
  }
}

static void test_completed_slots_detects_multiple_clears(void) {
  /* Slots 0, 3, 7 inflight; CI cleared all three between samples. */
  uint32_t inflight = (1u << 0) | (1u << 3) | (1u << 7);
  uint32_t prev = inflight;
  uint32_t cur = 0u;
  if (ahci_dispatch_completed_slots(prev, cur, inflight) != inflight) {
    fail("multiple slot transitions must be detected");
  }
}

static void test_completed_slots_filters_unowned_clears(void) {
  /* Slot 5 was set in prev_ci but NOT in inflight_mask (e.g. the
   * controller had stale CI state from a reset path). The fan-in
   * must NOT report slot 5 as completed; the host never owned it. */
  uint32_t inflight = (1u << 2);
  uint32_t prev = (1u << 2) | (1u << 5);
  uint32_t cur = 0u;
  if (ahci_dispatch_completed_slots(prev, cur, inflight) != (1u << 2)) {
    fail("completions must be filtered against inflight_mask");
  }
}

static void test_completed_slots_ignores_new_dispatches(void) {
  /* If cur_ci has a bit set that prev_ci did not, that's a new
   * dispatch the host just issued. It must NOT appear as
   * completed. The bitwise difference `prev & ~cur` correctly
   * excludes this case. */
  uint32_t inflight = (1u << 1);
  uint32_t prev = (1u << 1);
  uint32_t cur = (1u << 1) | (1u << 4); /* slot 4 freshly dispatched */
  if (ahci_dispatch_completed_slots(prev, cur, inflight) != 0u) {
    fail("freshly-dispatched slots must not appear as completed");
  }
}

static void test_completed_slots_partial_completion(void) {
  /* Slots 0 and 1 inflight; only slot 0 completed between samples. */
  uint32_t inflight = (1u << 0) | (1u << 1);
  uint32_t prev = inflight;
  uint32_t cur = (1u << 1); /* slot 0 cleared, slot 1 still set */
  if (ahci_dispatch_completed_slots(prev, cur, inflight) != (1u << 0)) {
    fail("partial completion (one of N) must be detected correctly");
  }
}

/* === Inflight count (popcount) ==================================== */

static void test_inflight_count_zero(void) {
  if (ahci_dispatch_inflight_count(0u) != 0u) {
    fail("empty mask must report 0 inflight");
  }
}

static void test_inflight_count_single(void) {
  if (ahci_dispatch_inflight_count(1u) != 1u) {
    fail("single bit must report 1 inflight");
  }
  if (ahci_dispatch_inflight_count(1u << 31) != 1u) {
    fail("MSB single bit must also report 1 inflight");
  }
}

static void test_inflight_count_full(void) {
  /* All 32 slots inflight (max for AHCI 1.3.1 NCS). */
  if (ahci_dispatch_inflight_count(0xFFFFFFFFu) != 32u) {
    fail("all 32 bits set must report 32 inflight");
  }
}

static void test_inflight_count_sparse(void) {
  /* Mixed pattern with 4 bits set in non-contiguous positions. */
  uint32_t mask = (1u << 0) | (1u << 7) | (1u << 15) | (1u << 31);
  if (ahci_dispatch_inflight_count(mask) != 4u) {
    fail("4 sparse bits must report 4 inflight");
  }
}

/* === Admission gate ============================================== */

static void test_can_admit_no_limit_always_yes(void) {
  /* concurrent_limit=0 sentinel: never bounce the caller. */
  if (!ahci_dispatch_can_admit(0u, 0u)) {
    fail("no-limit + empty inflight must admit");
  }
  if (!ahci_dispatch_can_admit(0xFFFFFFFFu, 0u)) {
    fail("no-limit + full inflight must still admit");
  }
}

static void test_can_admit_below_limit(void) {
  /* 3 inflight < limit 4 -> admit. */
  uint32_t inflight = (1u << 0) | (1u << 2) | (1u << 5);
  if (!ahci_dispatch_can_admit(inflight, 4u)) {
    fail("inflight < limit must admit");
  }
}

static void test_can_admit_at_limit_rejects(void) {
  /* 4 inflight == limit 4 -> reject (strict <). */
  uint32_t inflight = 0xFu; /* slots 0..3 */
  if (ahci_dispatch_can_admit(inflight, 4u)) {
    fail("inflight == limit must NOT admit");
  }
}

static void test_can_admit_above_limit_rejects(void) {
  /* Defensive: 5 inflight > limit 4 should also reject (host state
   * is over-committed; admission must wait). */
  uint32_t inflight = 0x1Fu; /* slots 0..4 */
  if (ahci_dispatch_can_admit(inflight, 4u)) {
    fail("inflight > limit must NOT admit");
  }
}

static void test_can_admit_limit_one_serializes(void) {
  /* concurrent_limit=1 reduces dispatch to serialised (the current
   * driver behaviour, useful for backpressure debugging). */
  if (!ahci_dispatch_can_admit(0u, 1u)) {
    fail("limit=1 + empty inflight must admit");
  }
  if (ahci_dispatch_can_admit(0x1u, 1u)) {
    fail("limit=1 + one inflight must NOT admit");
  }
}

/* === First-set-bit (slot picker) ================================ */

static void test_first_slot_zero_returns_minus_one(void) {
  if (ahci_dispatch_first_slot(0u) != -1) {
    fail("empty mask must return -1");
  }
}

static void test_first_slot_lowest_bit_wins(void) {
  /* Multiple bits set: lowest index wins, regardless of how many
   * higher bits are set. */
  if (ahci_dispatch_first_slot(0xFFFFFFFFu) != 0) {
    fail("all bits set must return slot 0");
  }
  if (ahci_dispatch_first_slot((1u << 3) | (1u << 7) | (1u << 15)) != 3) {
    fail("multiple bits must return the lowest set");
  }
}

static void test_first_slot_each_bit_position(void) {
  /* Sweep every bit position 0..31 to lock the mapping. */
  for (int i = 0; i < 32; ++i) {
    uint32_t mask = 1u << i;
    int got = ahci_dispatch_first_slot(mask);
    if (got != i) {
      fail("bit position must map 1:1 to slot index");
      return;
    }
  }
}

static void test_first_slot_msb_only(void) {
  /* Only bit 31 set -> return 31. */
  if (ahci_dispatch_first_slot(1u << 31) != 31) {
    fail("MSB-only mask must return slot 31");
  }
}

int run_ahci_dispatch_tests(void) {
  g_failures = 0;
  test_inflight_when_ci_set_no_errors();
  test_completed_when_ci_cleared();
  test_completed_when_ci_cleared_even_with_tfes();
  test_completed_when_ci_cleared_even_with_tfd_err();
  test_aborted_when_ci_set_and_tfes();
  test_aborted_when_ci_set_and_tfd_err();
  test_aborted_when_ci_set_and_both_errors();
  test_inflight_ignores_other_is_bits();
  test_inflight_ignores_other_tfd_bits();
  test_slot_bit_isolation();
  test_enum_values_are_stable();
  test_completed_slots_empty_when_no_inflight();
  test_completed_slots_empty_when_nothing_cleared();
  test_completed_slots_detects_single_clear();
  test_completed_slots_detects_multiple_clears();
  test_completed_slots_filters_unowned_clears();
  test_completed_slots_ignores_new_dispatches();
  test_completed_slots_partial_completion();
  test_inflight_count_zero();
  test_inflight_count_single();
  test_inflight_count_full();
  test_inflight_count_sparse();
  test_can_admit_no_limit_always_yes();
  test_can_admit_below_limit();
  test_can_admit_at_limit_rejects();
  test_can_admit_above_limit_rejects();
  test_can_admit_limit_one_serializes();
  test_first_slot_zero_returns_minus_one();
  test_first_slot_lowest_bit_wins();
  test_first_slot_each_bit_position();
  test_first_slot_msb_only();
  if (g_failures == 0) printf("[tests] ahci_dispatch OK\n");
  return g_failures;
}
