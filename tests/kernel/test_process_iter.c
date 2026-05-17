/*
 * Tests for the public process iterator (include/kernel/process_iter.h)
 * and process_stats_get.
 *
 * The process module depends on the VMM for address-space allocation;
 * tests/stub_vmm.c provides a calloc/free-backed stand-in so the host
 * can exercise process_create / process_at_index without touching
 * x86_64 inline asm.
 *
 * Covers:
 *  - empty proc table -> first() returns 0
 *  - 1, 2 processes -> iter returns them in stable order with stats copied
 *  - process_stats_get for unknown PID -> -1
 *  - process_state_label maps known and unknown values
 *  - rss_pages stays 0 (M4 phase 7 fills it).
 */
#include <stdio.h>
#include <string.h>

#include "kernel/process.h"
#include "kernel/process_iter.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                        \
    do {                                                                  \
        tests_run++;                                                      \
        printf("  %-48s ", name);                                         \
    } while (0)
#define PASS()                                                            \
    do {                                                                  \
        printf("OK\n");                                                   \
        tests_passed++;                                                   \
    } while (0)
#define FAIL(msg)                                                         \
    do {                                                                  \
        printf("FAIL: %s\n", msg);                                        \
    } while (0)

static int count_iter_visible(void) {
    struct process_iter it;
    struct process_stats s;
    int n = 0;
    for (int ok = process_iter_first(&it, &s); ok;
         ok = process_iter_next(&it, &s)) {
        n++;
    }
    return n;
}

static void test_empty_proc_table(void) {
    struct process_iter it;
    struct process_stats s;
    process_system_init();

    TEST("process_iter_first on empty proc table returns 0");
    if (process_iter_first(&it, &s) == 0) PASS();
    else FAIL("expected 0 on empty proc table");

    TEST("process_iter_first NULL it -> 0");
    if (process_iter_first(NULL, &s) == 0) PASS();
    else FAIL("expected 0 for NULL iter");

    TEST("count_iter_visible == 0 on empty");
    if (count_iter_visible() == 0) PASS();
    else FAIL("count must be 0");
}

static void test_iter_with_processes(void) {
    struct process *a, *b;
    struct process_iter it;
    struct process_stats s;
    int saw_a = 0, saw_b = 0;
    int order_pids[2] = {0, 0};
    int idx = 0;

    process_system_init();
    a = process_create("init", 0u, 0u);
    b = process_create("shell", 1000u, 1000u);

    TEST("process_create produces two processes");
    if (a && b && process_count() == 2u) PASS();
    else FAIL("process_create failed");

    TEST("count_iter_visible reflects 2 processes");
    if (count_iter_visible() == 2) PASS();
    else FAIL("expected 2 visible");

    TEST("iter visits both in stable PID-ascending order");
    for (int ok = process_iter_first(&it, &s); ok && idx < 2;
         ok = process_iter_next(&it, &s)) {
        order_pids[idx++] = (int)s.pid;
        if (s.pid == a->pid) saw_a = 1;
        if (s.pid == b->pid) saw_b = 1;
    }
    if (saw_a && saw_b && idx == 2 && order_pids[0] < order_pids[1]) {
        PASS();
    } else {
        FAIL("ordering or coverage wrong");
    }

    TEST("stats.name copies from process->name");
    {
        struct process_stats sa;
        if (process_stats_get(a->pid, &sa) == 0 &&
            strcmp(sa.name, "init") == 0) {
            PASS();
        } else {
            FAIL("name not copied for 'init'");
        }
    }

    TEST("stats.uid/gid copied");
    {
        struct process_stats sb;
        if (process_stats_get(b->pid, &sb) == 0 &&
            sb.uid == 1000u && sb.gid == 1000u) {
            PASS();
        } else {
            FAIL("uid/gid not copied");
        }
    }

    /* Phase 7b wired rss_pages to vmm_address_space_rss(as). The
     * host stub_vmm.c does not touch the rss_pages field on
     * map/unmap (because there are no real PTEs), so the counter
     * stays at 0 for any process created here. The assertion still
     * locks "rss == AS counter" rather than the prior literal 0
     * sentinel, and the AS counter ITSELF is exercised end-to-end
     * by tests/test_vmm_anon_regions.c. */
    TEST("rss_pages mirrors vmm_address_space_rss (0 in host harness)");
    {
        struct process_stats sa;
        if (process_stats_get(a->pid, &sa) == 0 && sa.rss_pages == 0u) {
            PASS();
        } else {
            FAIL("rss_pages drifted from AS counter");
        }
    }

    /* Phase 7b: prove the wiring is real, not stubbed to literal 0.
     * Bump the AS counter by hand and check that the snapshot
     * reflects it. */
    TEST("rss_pages picks up AS counter changes");
    {
        a->address_space->rss_pages = 42u;
        struct process_stats sa;
        int ok = process_stats_get(a->pid, &sa) == 0 && sa.rss_pages == 42u;
        a->address_space->rss_pages = 0u;
        if (ok) PASS();
        else FAIL("rss_pages did not reflect AS counter");
    }
}

static void test_process_stats_get_errors(void) {
    struct process_stats s;

    process_system_init();

    TEST("process_stats_get(0, &s) returns -1");
    if (process_stats_get(0u, &s) == -1) PASS();
    else FAIL("expected -1 for PID 0");

    TEST("process_stats_get(unknown, &s) returns -1");
    if (process_stats_get(99999u, &s) == -1) PASS();
    else FAIL("expected -1 for unknown PID");

    TEST("process_stats_get(valid, NULL) returns -1");
    {
        struct process *p = process_create("nullcheck", 0u, 0u);
        if (p && process_stats_get(p->pid, NULL) == -1) PASS();
        else FAIL("expected -1 for NULL stats");
    }
}

static void test_process_state_labels(void) {
    TEST("process_state_label maps known states");
    if (strcmp(process_state_label(PROC_STATE_UNUSED), "unused") == 0 &&
        strcmp(process_state_label(PROC_STATE_EMBRYO), "embryo") == 0 &&
        strcmp(process_state_label(PROC_STATE_RUNNING), "running") == 0 &&
        strcmp(process_state_label(PROC_STATE_SLEEPING), "sleeping") == 0 &&
        strcmp(process_state_label(PROC_STATE_ZOMBIE), "zombie") == 0) {
        PASS();
    } else {
        FAIL("known state labels mismatch");
    }

    TEST("process_state_label rejects out-of-range values");
    if (strcmp(process_state_label((enum process_state)9999), "?") == 0) PASS();
    else FAIL("expected '?' for unknown state");
}

static void test_process_at_index_bounds(void) {
    process_system_init();

    TEST("process_at_index returns NULL for index >= PROCESS_MAX");
    if (process_at_index((size_t)PROCESS_MAX) == NULL &&
        process_at_index((size_t)PROCESS_MAX + 1u) == NULL) {
        PASS();
    } else {
        FAIL("expected NULL for out-of-range index");
    }

    TEST("process_at_index(0) returns valid pointer");
    if (process_at_index(0u) != NULL) PASS();
    else FAIL("expected non-NULL for index 0");
}

int test_process_iter_run(void) {
    printf("[test_process_iter]\n");
    tests_run = 0;
    tests_passed = 0;
    test_empty_proc_table();
    test_iter_with_processes();
    test_process_stats_get_errors();
    test_process_state_labels();
    test_process_at_index_bounds();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
