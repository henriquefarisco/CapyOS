/*
 * Tests for the kernel service runner (include/services/service_runner.h).
 *
 * Covers the phase-1 contract:
 *  - service_runner_init is idempotent and assigns a stable, non-zero PID;
 *  - the spawned task is visible in task_iter under the canonical name;
 *  - the task carries TASK_PRIORITY_NORMAL and starts in TASK_STATE_READY;
 *  - service_runner_step increments step_count and last_tick on every
 *    invocation, regardless of whether any service was due;
 *  - step_count grows monotonically across consecutive calls;
 *  - service_runner_stats_get rejects NULL out and reflects the latest
 *    counters on success.
 *
 * The test is host-only and bypasses the scheduler; the runner task body
 * itself is not dispatched here. Phase 8 will switch the production
 * scheduler to preemption and add an integration smoke for that path.
 */
#include <stdio.h>
#include <string.h>

#include "kernel/task.h"
#include "kernel/task_iter.h"
#include "services/service_manager.h"
#include "services/service_runner.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-52s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do {                                                                   \
        printf("OK\n");                                                    \
        tests_passed++;                                                    \
    } while (0)
#define FAIL(msg)                                                          \
    do {                                                                   \
        printf("FAIL: %s\n", msg);                                         \
    } while (0)

static void reset_world(void) {
    task_system_init();
    service_manager_reset();
    service_runner_reset();
}

static void test_init_is_idempotent(void) {
    reset_world();
    TEST("service_runner_pid is 0 before init");
    if (service_runner_pid() == 0u) PASS();
    else FAIL("expected PID 0 before init");

    service_runner_init();
    uint32_t pid_after_first = service_runner_pid();
    TEST("service_runner_init assigns non-zero PID");
    if (pid_after_first != 0u) PASS();
    else FAIL("expected non-zero PID");

    service_runner_init();
    TEST("service_runner_init is idempotent (PID stable)");
    if (service_runner_pid() == pid_after_first) PASS();
    else FAIL("PID changed across init calls");

    TEST("task_count reflects exactly one runner task");
    if (task_count() == 1u) PASS();
    else FAIL("expected exactly one task in the table");
}

static void test_runner_visible_in_task_iter(void) {
    struct task_iter it;
    struct task_stats s;
    int found = 0;
    enum task_priority observed_priority = TASK_PRIORITY_IDLE;
    enum task_state observed_state = TASK_STATE_UNUSED;

    reset_world();
    service_runner_init();

    for (int ok = task_iter_first(&it, &s); ok;
         ok = task_iter_next(&it, &s)) {
        if (strcmp(s.name, SERVICE_RUNNER_NAME) == 0 &&
            s.pid == service_runner_pid()) {
            found = 1;
            observed_priority = s.priority;
            observed_state = s.state;
            break;
        }
    }

    TEST("task_iter exposes the service-runner task");
    if (found) PASS();
    else FAIL("service-runner not found in iterator");

    TEST("runner task uses TASK_PRIORITY_NORMAL");
    if (observed_priority == TASK_PRIORITY_NORMAL) PASS();
    else FAIL("expected NORMAL priority");

    TEST("runner task starts in TASK_STATE_READY");
    if (observed_state == TASK_STATE_READY) PASS();
    else FAIL("expected READY state for fresh task");
}

static void test_step_grows_counters(void) {
    struct service_runner_stats stats_before;
    struct service_runner_stats stats_after_one;
    struct service_runner_stats stats_after_three;

    reset_world();
    service_runner_init();

    TEST("stats_get rejects NULL out");
    if (service_runner_stats_get((struct service_runner_stats *)0) == -1) {
        PASS();
    } else {
        FAIL("expected -1 for NULL out");
    }

    if (service_runner_stats_get(&stats_before) != 0) {
        TEST("baseline stats_get success");
        FAIL("stats_get returned non-zero");
        return;
    }
    TEST("baseline step_count is 0");
    if (stats_before.step_count == 0u) PASS();
    else FAIL("expected step_count == 0 before any step");

    (void)service_runner_step(100u);
    if (service_runner_stats_get(&stats_after_one) != 0) {
        TEST("stats_get after one step succeeds");
        FAIL("stats_get returned non-zero");
        return;
    }
    TEST("step_count grows to 1 after one step");
    if (stats_after_one.step_count == 1u) PASS();
    else FAIL("expected step_count == 1");

    TEST("last_tick reflects the tick passed to step");
    if (stats_after_one.last_tick == 100u) PASS();
    else FAIL("last_tick mismatch");

    (void)service_runner_step(101u);
    (void)service_runner_step(102u);
    if (service_runner_stats_get(&stats_after_three) != 0) {
        TEST("stats_get after three steps succeeds");
        FAIL("stats_get returned non-zero");
        return;
    }
    TEST("step_count is monotonically increasing across calls");
    if (stats_after_three.step_count == 3u &&
        stats_after_three.step_count > stats_after_one.step_count) {
        PASS();
    } else {
        FAIL("step_count not monotonic");
    }

    TEST("last_tick advances with the most recent step");
    if (stats_after_three.last_tick == 102u) PASS();
    else FAIL("last_tick did not advance");

    TEST("stats.pid matches service_runner_pid()");
    if (stats_after_three.pid == service_runner_pid() &&
        stats_after_three.pid != 0u) {
        PASS();
    } else {
        FAIL("stats.pid mismatch");
    }
}

static void test_reset_clears_state(void) {
    struct service_runner_stats stats;

    reset_world();
    service_runner_init();
    (void)service_runner_step(50u);

    service_runner_reset();
    TEST("service_runner_reset clears PID");
    if (service_runner_pid() == 0u) PASS();
    else FAIL("PID not cleared by reset");

    if (service_runner_stats_get(&stats) != 0) {
        TEST("stats_get after reset succeeds");
        FAIL("stats_get failed");
        return;
    }
    TEST("service_runner_reset zeroes counters");
    if (stats.step_count == 0u && stats.last_tick == 0u &&
        stats.services_polled_total == 0u) {
        PASS();
    } else {
        FAIL("counters not zeroed by reset");
    }

    /* After reset, init must be able to recreate the task. */
    service_runner_init();
    TEST("service_runner_init recreates task after reset");
    if (service_runner_pid() != 0u) PASS();
    else FAIL("init failed after reset");
}

int test_service_runner_run(void) {
    printf("[test_service_runner]\n");
    tests_run = 0;
    tests_passed = 0;
    test_init_is_idempotent();
    test_runner_visible_in_task_iter();
    test_step_grows_counters();
    test_reset_clears_state();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
