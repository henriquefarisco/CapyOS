#include <stdio.h>
#include <string.h>

#include "core/work_queue.h"

static int g_work_hits = 0;
static int g_work_fail_hits = 0;

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[work_queue] %s\n", msg);
        return 1;
    }
    return 0;
}

static int work_ok_cb(void *ctx) {
    int *value = (int *)ctx;
    g_work_hits++;
    if (value) {
        (*value)++;
    }
    return 0;
}

static int work_fail_cb(void *ctx) {
    int *value = (int *)ctx;
    g_work_fail_hits++;
    if (value) {
        (*value)++;
    }
    return -9;
}

int run_work_queue_tests(void) {
    int fails = 0;
    int ctx = 0;
    struct system_work_status work;

    g_work_hits = 0;
    g_work_fail_hits = 0;
    work_queue_reset();
    work_queue_init();

    fails += expect_true(work_queue_count() == SYSTEM_WORK_COUNT,
                         "unexpected builtin work count");
    fails += expect_true(work_queue_get(SYSTEM_WORK_RECOVERY_SNAPSHOT, &work) == 0,
                         "builtin recovery snapshot work should exist");
    fails += expect_true(strcmp(work.name, "recovery-snapshot") == 0,
                         "builtin work name mismatch");
    fails += expect_true(work.state == SYSTEM_WORK_STATE_DISABLED,
                         "builtin work should start disabled");
    fails += expect_true(strcmp(work_queue_state_label(SYSTEM_WORK_STATE_READY), "ready") == 0,
                         "ready label mismatch");

    fails += expect_true(work_queue_register(SYSTEM_WORK_RECOVERY_SNAPSHOT,
                                             "recovery-snapshot",
                                             work_ok_cb, &ctx) == 0,
                         "work registration failed");
    fails += expect_true(work_queue_set_interval(SYSTEM_WORK_RECOVERY_SNAPSHOT, 5u) == 0,
                         "work interval should be configurable");
    fails += expect_true(work_queue_find("recovery-snapshot", &work) ==
                             SYSTEM_WORK_RECOVERY_SNAPSHOT,
                         "work lookup by name should succeed");
    fails += expect_true(work_queue_poll_due(0u) == 1,
                         "idle registered work should run immediately");
    fails += expect_true(g_work_hits == 1 && ctx == 1,
                         "registered work should execute once");
    fails += expect_true(work_queue_get(SYSTEM_WORK_RECOVERY_SNAPSHOT, &work) == 0,
                         "work should remain readable after execution");
    fails += expect_true(work.state == SYSTEM_WORK_STATE_READY,
                         "successful work should transition to ready");
    fails += expect_true(work.runs == 1 && work.failures == 0,
                         "successful work counters mismatch");
    fails += expect_true(work.next_due_tick == 5u,
                         "interval work should schedule next due tick");
    fails += expect_true(work_queue_poll_due(4u) == 0,
                         "work should not rerun before interval");
    fails += expect_true(work_queue_poll_due(5u) == 1,
                         "work should rerun when interval expires");
    fails += expect_true(g_work_hits == 2 && ctx == 2,
                         "interval work should rerun");

    work_queue_reset();
    work_queue_init();
    ctx = 0;
    g_work_hits = 0;
    fails += expect_true(work_queue_register(SYSTEM_WORK_RECOVERY_SNAPSHOT,
                                             "recovery-snapshot",
                                             work_ok_cb, &ctx) == 0,
                         "delayed work registration failed");
    fails += expect_true(work_queue_schedule_after(SYSTEM_WORK_RECOVERY_SNAPSHOT, 10u, 3u) == 0,
                         "work should support delayed scheduling");
    fails += expect_true(work_queue_poll_due(12u) == 0,
                         "delayed work should wait until due tick");
    fails += expect_true(work_queue_poll_due(13u) == 1,
                         "delayed work should run when due");
    fails += expect_true(g_work_hits == 1 && ctx == 1,
                         "delayed work should execute once");

    work_queue_reset();
    work_queue_init();
    ctx = 0;
    g_work_fail_hits = 0;
    fails += expect_true(work_queue_register(SYSTEM_WORK_RECOVERY_SNAPSHOT,
                                             "recovery-snapshot",
                                             work_fail_cb, &ctx) == 0,
                         "failing work registration failed");
    fails += expect_true(work_queue_set_interval(SYSTEM_WORK_RECOVERY_SNAPSHOT, 4u) == 0,
                         "failing work interval should be configurable");
    fails += expect_true(work_queue_poll_due(0u) == 1,
                         "failing work should still be polled");
    fails += expect_true(g_work_fail_hits == 1 && ctx == 1,
                         "failing work should execute once");
    fails += expect_true(work_queue_get(SYSTEM_WORK_RECOVERY_SNAPSHOT, &work) == 0,
                         "failing work should remain readable");
    fails += expect_true(work.state == SYSTEM_WORK_STATE_FAILED,
                         "failing work should transition to failed");
    fails += expect_true(work.failures == 1 && work.last_result == -9,
                         "failing work counters mismatch");
    fails += expect_true(work.next_due_tick == 4u,
                         "failing interval work should reschedule");
    fails += expect_true(work_queue_disable(SYSTEM_WORK_RECOVERY_SNAPSHOT) == 0,
                         "work should disable cleanly");
    fails += expect_true(work_queue_poll_due(8u) == 0,
                         "disabled work should not be polled");
    fails += expect_true(work_queue_get(SYSTEM_WORK_RECOVERY_SNAPSHOT, &work) == 0,
                         "disabled work should remain readable");
    fails += expect_true(work.state == SYSTEM_WORK_STATE_DISABLED,
                         "disabled work should stay disabled");

    if (fails == 0) {
        printf("[tests] work_queue OK\n");
    }
    return fails;
}
