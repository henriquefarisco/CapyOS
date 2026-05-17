/*
 * Tests for the public task iterator (include/kernel/task_iter.h).
 *
 * The iterator walks the kernel task table skipping UNUSED slots and
 * publishes a snapshot stats struct that decouples observers from the
 * private storage. These tests cover:
 *  - empty table -> first() returns 0
 *  - 1, 2, 3 tasks -> iter returns them in stable order
 *  - task_kill -> the iterator stops returning the killed PID
 *  - state/priority/name fields are copied verbatim into the stats
 *  - task_state_label / task_priority_label return stable, lower-case
 *    strings even for out-of-range values.
 */
#include <stdio.h>
#include <string.h>

#include "kernel/task.h"
#include "kernel/task_iter.h"

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

static void noop_entry(void *arg) { (void)arg; }

static int count_iter_visible(void) {
    struct task_iter it;
    struct task_stats s;
    int n = 0;
    for (int ok = task_iter_first(&it, &s); ok;
         ok = task_iter_next(&it, &s)) {
        n++;
    }
    return n;
}

static void test_empty_iter(void) {
    struct task_iter it;
    struct task_stats s;
    task_system_init();
    TEST("task_iter_first on empty table returns 0");
    if (task_iter_first(&it, &s) == 0) {
        PASS();
    } else {
        FAIL("expected 0 on empty table");
    }

    TEST("task_iter_next without first returns 0");
    {
        struct task_iter empty;
        struct task_stats sx;
        empty.next_index = 0; /* mimic uninitialized contract */
        /* On an empty table next() also returns 0. */
        if (task_iter_next(&empty, &sx) == 0) PASS();
        else FAIL("expected 0 on empty table next");
    }

    TEST("task_iter_first NULL it -> 0");
    if (task_iter_first(NULL, &s) == 0) PASS();
    else FAIL("expected 0 for NULL it");
}

static void test_iter_returns_tasks_in_order(void) {
    struct task *a, *b, *c;
    struct task_iter it;
    struct task_stats s;
    int seen[4] = {0, 0, 0, 0};
    int order_pids[3] = {0, 0, 0};
    int idx = 0;

    task_system_init();
    a = task_create("alpha", noop_entry, NULL, TASK_PRIORITY_NORMAL);
    b = task_create("bravo", noop_entry, NULL, TASK_PRIORITY_HIGH);
    c = task_create("charlie", noop_entry, NULL, TASK_PRIORITY_LOW);

    TEST("task_create returns 3 valid tasks");
    if (a && b && c) PASS();
    else FAIL("task_create failed");

    TEST("count_iter_visible reflects 3 tasks");
    if (count_iter_visible() == 3) PASS();
    else FAIL("expected 3 visible");

    TEST("iter visits tasks in stable PID-ascending order");
    for (int ok = task_iter_first(&it, &s); ok && idx < 3;
         ok = task_iter_next(&it, &s)) {
        order_pids[idx++] = (int)s.pid;
        if (s.pid >= 1 && s.pid <= 3) seen[s.pid] = 1;
    }
    if (seen[1] && seen[2] && seen[3] && idx == 3 &&
        order_pids[0] < order_pids[1] && order_pids[1] < order_pids[2]) {
        PASS();
    } else {
        FAIL("iteration order or coverage wrong");
    }
}

static void test_iter_skips_killed_task(void) {
    struct task *a, *b;
    struct task_stats s;
    struct task_iter it;
    int found_a = 0;
    int found_b = 0;

    task_system_init();
    a = task_create("kept", noop_entry, NULL, TASK_PRIORITY_NORMAL);
    b = task_create("doomed", noop_entry, NULL, TASK_PRIORITY_NORMAL);

    TEST("created two tasks before kill");
    if (a && b && task_count() == 2) PASS();
    else FAIL("setup failed");

    task_kill(b->pid);

    TEST("iter skips killed task");
    for (int ok = task_iter_first(&it, &s); ok;
         ok = task_iter_next(&it, &s)) {
        if (s.pid == a->pid) found_a = 1;
        if (s.pid == 0) {
            /* PID 0 is reserved; iter must never return it. */
            FAIL("returned reserved PID 0");
            return;
        }
        /* killed task slot is now UNUSED with pid=0 (per task_kill
         * + task_alloc reuse), so we should not see it */
        (void)found_b;
    }
    if (found_a && count_iter_visible() == 1) PASS();
    else FAIL("expected only kept task");
}

static void test_iter_copies_stats(void) {
    struct task *t;
    struct task_iter it;
    struct task_stats s;
    int found = 0;

    task_system_init();
    t = task_create("snapshot", noop_entry, NULL, TASK_PRIORITY_HIGH);

    TEST("stats name matches task name");
    for (int ok = task_iter_first(&it, &s); ok;
         ok = task_iter_next(&it, &s)) {
        if (s.pid == t->pid) {
            if (strcmp(s.name, "snapshot") == 0) found = 1;
            break;
        }
    }
    if (found) PASS();
    else FAIL("name not copied");

    TEST("stats priority matches task priority");
    if (s.priority == TASK_PRIORITY_HIGH) PASS();
    else FAIL("priority not copied");

    TEST("stats state is READY for fresh task");
    if (s.state == TASK_STATE_READY) PASS();
    else FAIL("state not READY");

    TEST("cpu_time_ns starts at 0 (M4 phase 7 will populate)");
    if (s.cpu_time_ns == 0) PASS();
    else FAIL("cpu_time_ns must start at 0");
}

static void test_state_and_priority_labels(void) {
    TEST("task_state_label maps known states");
    if (strcmp(task_state_label(TASK_STATE_UNUSED), "unused") == 0 &&
        strcmp(task_state_label(TASK_STATE_READY), "ready") == 0 &&
        strcmp(task_state_label(TASK_STATE_RUNNING), "running") == 0 &&
        strcmp(task_state_label(TASK_STATE_BLOCKED), "blocked") == 0 &&
        strcmp(task_state_label(TASK_STATE_SLEEPING), "sleeping") == 0 &&
        strcmp(task_state_label(TASK_STATE_ZOMBIE), "zombie") == 0 &&
        strcmp(task_state_label(TASK_STATE_DEAD), "dead") == 0) {
        PASS();
    } else {
        FAIL("state labels mismatch");
    }

    TEST("task_state_label rejects out-of-range values");
    if (strcmp(task_state_label((enum task_state)9999), "?") == 0) PASS();
    else FAIL("expected '?' for unknown state");

    TEST("task_priority_label maps known priorities");
    if (strcmp(task_priority_label(TASK_PRIORITY_IDLE), "idle") == 0 &&
        strcmp(task_priority_label(TASK_PRIORITY_LOW), "low") == 0 &&
        strcmp(task_priority_label(TASK_PRIORITY_NORMAL), "normal") == 0 &&
        strcmp(task_priority_label(TASK_PRIORITY_HIGH), "high") == 0 &&
        strcmp(task_priority_label(TASK_PRIORITY_REALTIME), "rt") == 0) {
        PASS();
    } else {
        FAIL("priority labels mismatch");
    }
}

int test_task_iter_run(void) {
    printf("[test_task_iter]\n");
    tests_run = 0;
    tests_passed = 0;
    test_empty_iter();
    test_iter_returns_tasks_in_order();
    test_iter_skips_killed_task();
    test_iter_copies_stats();
    test_state_and_priority_labels();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
