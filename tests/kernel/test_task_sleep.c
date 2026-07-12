/*
 * tests/kernel/test_task_sleep.c — host tests for the alpha.311 task_sleep
 * degrade-to-yield guard and the scheduler_can_sleep_current() predicate it
 * depends on. Split out of tests/kernel/test_context_switch.c to keep both
 * files under the host-test line budget (see docs/architecture/source-layout).
 *
 * Regression for the VMware capygfx hang: on a platform where the timer tick
 * never advances (scheduler_can_sleep_current() sees total_ticks == 0), a real
 * sleep would park the ring-3 browser FOREVER, so it never re-polls, never sees
 * WINDOW_CLOSE and never exits. task_sleep() (src/kernel/task.c) now degrades to
 * a cooperative yield whenever the current task cannot be woken, while
 * scheduler_sleep_current() (the primitive, unit-tested in test_context_switch)
 * keeps its original blocking contract. These lock the predicate and both
 * task_sleep branches.
 *
 * Links the real src/kernel/scheduler.c + src/kernel/task.c against the
 * context-switch and arch-hook stubs (same harness as test_context_switch.c).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "../stubs/stub_arch_sched_hooks.h"
#include "../stubs/stub_context_switch.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
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

static void noop_entry(void *arg) { (void)arg; }

static void reset_world(void) {
    task_system_init();
    scheduler_init(SCHED_POLICY_COOPERATIVE);
    task_set_current((struct task *)0);
    stub_arch_sched_hooks_log_clear();
    stub_context_switch_log_clear();
}

/* The predicate: a task may take a real sleep ONLY when a tick can wake it and
 * something else is runnable; otherwise task_sleep must degrade to a yield. */
static void test_scheduler_can_sleep_current_conditions(void) {
    reset_world(); /* COOPERATIVE, sched_running=0, total_ticks=0 */
    struct task *a = task_create("a", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(a);
    a->state = TASK_STATE_RUNNING;
    task_set_current(a);

    TEST("can_sleep is false while the scheduler is not running");
    if (!scheduler_can_sleep_current()) PASS();
    else FAIL("expected 0 when sched_running == 0");

    scheduler_set_running(1);
    TEST("can_sleep is false when no tick has advanced (total_ticks==0)");
    if (!scheduler_can_sleep_current()) PASS(); /* the VMware no-APIC case */
    else FAIL("expected 0 when total_ticks == 0");

    scheduler_tick(); /* total_ticks -> 1; cooperative tick never switches */
    TEST("can_sleep is false with a tick but no other READY task (coop)");
    if (!scheduler_can_sleep_current()) PASS();
    else FAIL("expected 0 with no runnable peer in cooperative policy");

    struct task *b = task_create("b", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(b); /* b is READY */
    TEST("can_sleep is true once a READY peer exists and ticks advance");
    if (scheduler_can_sleep_current()) PASS();
    else FAIL("expected 1 with a runnable peer and total_ticks>0");

    a->state = TASK_STATE_READY; /* current no longer RUNNING */
    TEST("can_sleep is false when current is not RUNNING");
    if (!scheduler_can_sleep_current()) PASS();
    else FAIL("expected 0 when current->state != RUNNING");

    scheduler_set_running(0);
}

static void test_task_sleep_degrades_to_yield_when_cannot_sleep(void) {
    struct scheduler_stats s;
    reset_world();
    struct task *a = task_create("a", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    struct task *b = task_create("b", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(a);
    scheduler_add(b);
    a->state = TASK_STATE_RUNNING;
    task_set_current(a);
    scheduler_set_running(1);
    /* total_ticks==0: can_sleep is false EVEN THOUGH b is READY -- the exact
     * no-advancing-tick case that hung capygfx on VMware. */

    uint32_t cs_before = stub_context_switch_log_count();
    task_sleep(5u);

    TEST("task_sleep does NOT park the task when it cannot sleep");
    if (a->state != TASK_STATE_SLEEPING) PASS();
    else FAIL("task went to SLEEPING with no way to be woken");

    scheduler_stats_get(&s);
    TEST("task_sleep degrade leaves sleeping_count untouched");
    if (s.sleeping_count == 0u) PASS();
    else FAIL("sleeping_count advanced on a degraded sleep");

    TEST("task_sleep degrades to a cooperative yield (switches to peer)");
    if (stub_context_switch_log_count() == cs_before + 1u &&
        task_current() == b && a->state == TASK_STATE_READY) PASS();
    else FAIL("degraded task_sleep did not yield into the READY peer");

    scheduler_set_running(0);
}

static void test_task_sleep_really_sleeps_when_allowed(void) {
    struct scheduler_stats s;
    reset_world();
    struct task *a = task_create("a", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    struct task *b = task_create("b", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(a);
    scheduler_add(b);
    a->state = TASK_STATE_RUNNING;
    task_set_current(a);
    scheduler_set_running(1);
    scheduler_tick(); /* total_ticks -> 1 so a real sleep is wakeable */

    uint32_t cs_before = stub_context_switch_log_count();
    task_sleep(7u);

    TEST("task_sleep really sleeps when the task can be woken");
    if (a->state == TASK_STATE_SLEEPING) PASS();
    else FAIL("task_sleep failed to park a wakeable task");

    TEST("real task_sleep records wake_tick = now + ticks");
    if (a->wake_tick == 8u) PASS(); /* total_ticks(1) + 7 */
    else FAIL("wake_tick not set to total_ticks + ticks");

    scheduler_stats_get(&s);
    TEST("real task_sleep advances sleeping_count and context-switches");
    if (s.sleeping_count == 1u &&
        stub_context_switch_log_count() == cs_before + 1u &&
        task_current() == b) PASS();
    else FAIL("real task_sleep did not use scheduler_sleep_current");

    scheduler_set_running(0);
}

int test_task_sleep_run(void) {
    printf("[test_task_sleep]\n");
    tests_run = 0;
    tests_passed = 0;
    test_scheduler_can_sleep_current_conditions();
    test_task_sleep_degrades_to_yield_when_cannot_sleep();
    test_task_sleep_really_sleeps_when_allowed();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
