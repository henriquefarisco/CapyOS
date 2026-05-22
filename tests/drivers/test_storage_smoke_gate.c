/* Host tests for the storage stack smoke gate (Slice 3E.4). */

#include "drivers/storage/storage_smoke.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[storage-smoke] FAIL: %s\n", msg);
    g_failures++;
}

static void test_state_reset_zeros_counters(void) {
    struct storage_smoke_state s;
    memset(&s, 0xAA, sizeof(s));
    storage_smoke_state_reset(&s);
    if (s.ahci_ok_count != 0 || s.nvme_ok_count != 0 || s.emitted != 0) {
        fail("reset must zero all fields");
    }
}

static void test_state_reset_null_safe(void) {
    storage_smoke_state_reset(NULL); /* must not crash */
}

static void test_gate_predicate(void) {
    if (storage_smoke_gate_observed(0, 0)) {
        fail("0,0 must NOT be observed");
    }
    if (!storage_smoke_gate_observed(1, 0)) fail("ahci=1 must satisfy");
    if (!storage_smoke_gate_observed(0, 1)) fail("nvme=1 must satisfy");
    if (!storage_smoke_gate_observed(5, 7)) fail("both>0 must satisfy");
}

static void test_first_ahci_ok_emits(void) {
    struct storage_smoke_state s;
    storage_smoke_state_reset(&s);
    if (storage_smoke_observe(&s, STORAGE_SMOKE_SRC_AHCI) != 1) {
        fail("first AHCI OK must trigger emission");
    }
    if (s.ahci_ok_count != 1) fail("ahci counter must increment");
    if (s.emitted != 1) fail("latch must be set");
}

static void test_first_nvme_ok_emits(void) {
    struct storage_smoke_state s;
    storage_smoke_state_reset(&s);
    if (storage_smoke_observe(&s, STORAGE_SMOKE_SRC_NVME) != 1) {
        fail("first NVMe OK must trigger emission");
    }
    if (s.nvme_ok_count != 1) fail("nvme counter must increment");
}

static void test_subsequent_calls_do_not_re_emit(void) {
    struct storage_smoke_state s;
    storage_smoke_state_reset(&s);
    (void)storage_smoke_observe(&s, STORAGE_SMOKE_SRC_AHCI);
    if (storage_smoke_observe(&s, STORAGE_SMOKE_SRC_AHCI) != 0) {
        fail("second AHCI OK must NOT re-emit");
    }
    if (storage_smoke_observe(&s, STORAGE_SMOKE_SRC_NVME) != 0) {
        fail("subsequent NVMe OK must NOT re-emit after AHCI latch");
    }
    if (s.ahci_ok_count != 2) fail("counter must keep incrementing post-latch");
    if (s.nvme_ok_count != 1) fail("nvme counter must increment post-latch");
}

static void test_invalid_source_is_rejected(void) {
    struct storage_smoke_state s;
    storage_smoke_state_reset(&s);
    if (storage_smoke_observe(&s, (enum storage_smoke_source)42) != 0) {
        fail("invalid source must NOT trigger emission");
    }
    if (s.ahci_ok_count != 0 || s.nvme_ok_count != 0) {
        fail("invalid source must NOT mutate counters");
    }
}

static void test_null_state_is_rejected(void) {
    if (storage_smoke_observe(NULL, STORAGE_SMOKE_SRC_AHCI) != 0) {
        fail("NULL state must return 0");
    }
}

static void test_marker_constant_is_canonical(void) {
    /* The smoke harness greps for this exact string. Hard-code it
     * in the test so any accidental rename breaks here. */
    if (strcmp(STORAGE_SMOKE_MARKER, "[smoke] storage-stack ready") != 0) {
        fail("STORAGE_SMOKE_MARKER must be the canonical string");
    }
}

/* alpha.252 audit fix BUG #1 regression tests — cross-driver latch.
 *
 * Before the fix, each driver carried its own `storage_smoke_state`
 * and would emit independently. The global latch now closes this
 * gap; these tests pin that contract so a future refactor that
 * accidentally reintroduces per-driver latches breaks here. */

static void test_global_latch_ahci_then_nvme(void) {
    storage_smoke_global_reset();
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI) != 1) {
        fail("first AHCI OK must trip the global latch");
    }
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_NVME) != 0) {
        fail("subsequent NVMe OK must NOT re-emit after AHCI latched");
    }
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI) != 0) {
        fail("subsequent AHCI OK must NOT re-emit after global latch");
    }
}

static void test_global_latch_nvme_then_ahci(void) {
    storage_smoke_global_reset();
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_NVME) != 1) {
        fail("first NVMe OK must trip the global latch");
    }
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI) != 0) {
        fail("subsequent AHCI OK must NOT re-emit after NVMe latched");
    }
}

static void test_global_latch_reset_isolates_tests(void) {
    storage_smoke_global_reset();
    (void)storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI);
    storage_smoke_global_reset();
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_NVME) != 1) {
        fail("global reset must allow a fresh latch");
    }
}

static void test_global_latch_rejects_invalid_source(void) {
    storage_smoke_global_reset();
    if (storage_smoke_try_latch_global((enum storage_smoke_source)99) != 0) {
        fail("invalid source must not latch global state");
    }
    if (storage_smoke_try_latch_global(STORAGE_SMOKE_SRC_AHCI) != 1) {
        fail("AHCI must still latch after invalid source attempt");
    }
}

int run_storage_smoke_gate_tests(void) {
    g_failures = 0;
    test_state_reset_zeros_counters();
    test_state_reset_null_safe();
    test_gate_predicate();
    test_first_ahci_ok_emits();
    test_first_nvme_ok_emits();
    test_subsequent_calls_do_not_re_emit();
    test_invalid_source_is_rejected();
    test_null_state_is_rejected();
    test_marker_constant_is_canonical();
    test_global_latch_ahci_then_nvme();
    test_global_latch_nvme_then_ahci();
    test_global_latch_reset_isolates_tests();
    test_global_latch_rejects_invalid_source();
    /* Leave the global state clean for any downstream test runner. */
    storage_smoke_global_reset();
    if (g_failures == 0) printf("[tests] storage_smoke_gate OK\n");
    return g_failures;
}
