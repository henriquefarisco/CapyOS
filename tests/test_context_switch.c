/*
 * Tests for the context_switch seam (M4 phase 2).
 *
 * Two layers are exercised:
 *
 *   1. The C/asm contract: sizeof(struct task_context) and the offset of
 *      every field must match the constants hard-coded in
 *      src/arch/x86_64/cpu/context_switch.S. A drift in either side
 *      would silently corrupt every preempted task; locking the
 *      contract here makes it impossible to merge such a regression.
 *
 *   2. The scheduler-side seam: schedule() is private to scheduler.c
 *      but is invoked indirectly through scheduler_block_current() and
 *      scheduler_sleep_current(), both of which always call schedule()
 *      regardless of sched_running. Tests use these entry points to
 *      drive the scheduler state machine and verify that:
 *        - schedule() invokes context_switch with the correct old/new
 *          context pointers;
 *        - state transitions are observed exactly as scheduler.c
 *          documents them;
 *        - cooperative scheduler_tick() advances counters and wakes
 *          sleepers without performing a context switch on its own.
 *
 * The host build links against tests/stub_context_switch.c which
 * records every invocation in a small log so we can assert order and
 * argument identity.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/scheduler.h"
#include "kernel/task.h"
#include "stub_context_switch.h"

extern void task_set_current(struct task *t);

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
    stub_context_switch_log_clear();
}

/* -------------------- 1. Layout invariants ----------------------------- */

static void test_layout_locks_asm_contract(void) {
    TEST("sizeof(struct task_context) == 0x50 (10 x 8 bytes)");
    if (sizeof(struct task_context) == 0x50u) PASS();
    else FAIL("size drift between C and asm");

    TEST("offsetof(rsp) == 0x00");
    if (offsetof(struct task_context, rsp) == 0x00u) PASS();
    else FAIL("rsp offset drift");

    TEST("offsetof(rbp) == 0x08");
    if (offsetof(struct task_context, rbp) == 0x08u) PASS();
    else FAIL("rbp offset drift");

    TEST("offsetof(rbx) == 0x10");
    if (offsetof(struct task_context, rbx) == 0x10u) PASS();
    else FAIL("rbx offset drift");

    TEST("offsetof(r12) == 0x18");
    if (offsetof(struct task_context, r12) == 0x18u) PASS();
    else FAIL("r12 offset drift");

    TEST("offsetof(r13) == 0x20");
    if (offsetof(struct task_context, r13) == 0x20u) PASS();
    else FAIL("r13 offset drift");

    TEST("offsetof(r14) == 0x28");
    if (offsetof(struct task_context, r14) == 0x28u) PASS();
    else FAIL("r14 offset drift");

    TEST("offsetof(r15) == 0x30");
    if (offsetof(struct task_context, r15) == 0x30u) PASS();
    else FAIL("r15 offset drift");

    TEST("offsetof(rip) == 0x38");
    if (offsetof(struct task_context, rip) == 0x38u) PASS();
    else FAIL("rip offset drift");

    TEST("offsetof(rflags) == 0x40");
    if (offsetof(struct task_context, rflags) == 0x40u) PASS();
    else FAIL("rflags offset drift");

    TEST("offsetof(cr3) == 0x48");
    if (offsetof(struct task_context, cr3) == 0x48u) PASS();
    else FAIL("cr3 offset drift");
}

/* -------------------- 2. scheduler_init / pick_next -------------------- */

static void test_init_resets_stats(void) {
    struct scheduler_stats s;
    reset_world();
    scheduler_stats_get(&s);
    TEST("scheduler_init zeroes total_switches");
    if (s.total_switches == 0u) PASS();
    else FAIL("expected 0 switches");

    TEST("scheduler_init zeroes total_ticks");
    if (s.total_ticks == 0u) PASS();
    else FAIL("expected 0 ticks");

    TEST("scheduler_init zeroes runnable_count");
    if (s.runnable_count == 0u) PASS();
    else FAIL("expected 0 runnable");

    TEST("scheduler_running() is 0 before scheduler_start");
    if (scheduler_running() == 0) PASS();
    else FAIL("expected sched_running == 0");
}

static void test_pick_next_priority(void) {
    reset_world();
    scheduler_init(SCHED_POLICY_PRIORITY);

    struct task *low = task_create("low", noop_entry, (void *)0,
                                   TASK_PRIORITY_LOW);
    struct task *high = task_create("high", noop_entry, (void *)0,
                                    TASK_PRIORITY_HIGH);
    struct task *normal = task_create("normal", noop_entry, (void *)0,
                                      TASK_PRIORITY_NORMAL);

    scheduler_add(low);
    scheduler_add(high);
    scheduler_add(normal);

    TEST("PRIORITY policy picks highest-priority READY task");
    if (scheduler_pick_next() == high) PASS();
    else FAIL("expected high");

    /* Mark high as RUNNING so it should no longer be picked. */
    high->state = TASK_STATE_RUNNING;
    TEST("scheduler_pick_next skips RUNNING tasks");
    if (scheduler_pick_next() == normal) PASS();
    else FAIL("expected normal after high goes RUNNING");
}

static void test_pick_next_cooperative(void) {
    reset_world();

    struct task *t1 = task_create("t1", noop_entry, (void *)0,
                                  TASK_PRIORITY_NORMAL);
    struct task *t2 = task_create("t2", noop_entry, (void *)0,
                                  TASK_PRIORITY_HIGH);

    scheduler_add(t1);
    scheduler_add(t2);

    TEST("COOPERATIVE picks first READY (insertion order)");
    if (scheduler_pick_next() == t1) PASS();
    else FAIL("expected t1 (head) regardless of priority");
}

/* -------------------- 3. Scheduler_tick semantics ---------------------- */

static void test_tick_advances_counters(void) {
    struct scheduler_stats s;
    reset_world();

    scheduler_tick();
    scheduler_stats_get(&s);
    TEST("scheduler_tick increments total_ticks");
    if (s.total_ticks == 1u) PASS();
    else FAIL("total_ticks did not advance");

    scheduler_tick();
    scheduler_tick();
    scheduler_stats_get(&s);
    TEST("scheduler_tick is monotonic");
    if (s.total_ticks == 3u) PASS();
    else FAIL("expected 3 ticks total");
}

static void test_tick_does_not_switch_in_cooperative(void) {
    reset_world();
    struct task *a = task_create("a", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    struct task *b = task_create("b", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(a);
    scheduler_add(b);
    a->state = TASK_STATE_RUNNING;
    task_set_current(a);

    scheduler_tick();
    scheduler_tick();
    TEST("COOPERATIVE scheduler_tick never invokes context_switch");
    if (stub_context_switch_invocations() == 0u) PASS();
    else FAIL("unexpected context_switch in cooperative mode");
}

static void test_tick_wakes_sleepers(void) {
    reset_world();
    struct task *t = task_create("napper", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(t);
    /* Simulate a sleeper that should wake on tick #5. */
    t->state = TASK_STATE_SLEEPING;
    t->wake_tick = 5u;

    for (int i = 0; i < 4; ++i) scheduler_tick();
    TEST("sleeper still sleeping before wake_tick");
    if (t->state == TASK_STATE_SLEEPING) PASS();
    else FAIL("woke too early");

    scheduler_tick();
    TEST("sleeper becomes READY at wake_tick");
    if (t->state == TASK_STATE_READY) PASS();
    else FAIL("did not wake at wake_tick");
}

static void test_tick_reaps_zombies(void) {
    reset_world();
    struct task *alive = task_create("alive", noop_entry, (void *)0,
                                     TASK_PRIORITY_NORMAL);
    struct task *zombie = task_create("zombie", noop_entry, (void *)0,
                                      TASK_PRIORITY_NORMAL);
    scheduler_add(alive);
    scheduler_add(zombie);
    zombie->state = TASK_STATE_ZOMBIE;

    scheduler_tick();
    TEST("zombie is reaped to UNUSED on tick");
    if (zombie->state == TASK_STATE_UNUSED) PASS();
    else FAIL("zombie not reaped");

    TEST("alive task is preserved across reaping tick");
    if (alive->state == TASK_STATE_READY) PASS();
    else FAIL("alive task disturbed by reaping");
}

/* -------------------- 4. schedule() invocation seam -------------------- */

static void test_block_triggers_context_switch(void) {
    const struct stub_context_switch_entry *entry;

    reset_world();
    struct task *t1 = task_create("t1", noop_entry, (void *)0,
                                  TASK_PRIORITY_NORMAL);
    struct task *t2 = task_create("t2", noop_entry, (void *)0,
                                  TASK_PRIORITY_NORMAL);
    scheduler_add(t1);
    scheduler_add(t2);
    t1->state = TASK_STATE_RUNNING;
    task_set_current(t1);

    scheduler_block_current((void *)0xdeadbeefULL);

    TEST("scheduler_block_current invokes context_switch exactly once");
    if (stub_context_switch_invocations() == 1u) PASS();
    else FAIL("expected exactly one context_switch call");

    entry = stub_context_switch_log_at(0u);
    TEST("context_switch was called with old=&t1->context");
    if (entry && entry->old_ctx == &t1->context) PASS();
    else FAIL("old_ctx mismatch");

    TEST("context_switch was called with new=&t2->context");
    if (entry && entry->new_ctx == &t2->context) PASS();
    else FAIL("new_ctx mismatch");

    TEST("blocked task transitions to TASK_STATE_BLOCKED");
    if (t1->state == TASK_STATE_BLOCKED) PASS();
    else FAIL("t1 state mismatch");

    TEST("picked task transitions to TASK_STATE_RUNNING");
    if (t2->state == TASK_STATE_RUNNING) PASS();
    else FAIL("t2 state mismatch");

    TEST("task_current() reflects the freshly scheduled task");
    if (task_current() == t2) PASS();
    else FAIL("current did not switch to t2");
}

static void test_sleep_triggers_context_switch(void) {
    reset_world();
    struct task *t1 = task_create("t1", noop_entry, (void *)0,
                                  TASK_PRIORITY_NORMAL);
    struct task *t2 = task_create("t2", noop_entry, (void *)0,
                                  TASK_PRIORITY_NORMAL);
    scheduler_add(t1);
    scheduler_add(t2);
    t1->state = TASK_STATE_RUNNING;
    task_set_current(t1);

    scheduler_sleep_current(7u);

    TEST("scheduler_sleep_current invokes context_switch");
    if (stub_context_switch_invocations() == 1u) PASS();
    else FAIL("expected one context_switch");

    TEST("sleeping task transitions to TASK_STATE_SLEEPING");
    if (t1->state == TASK_STATE_SLEEPING) PASS();
    else FAIL("t1 not SLEEPING");

    TEST("sleeping task records wake_tick = now + ticks");
    if (t1->wake_tick == 7u) PASS();
    else FAIL("wake_tick not set correctly");
}

static void test_unblock_promotes_blocked_to_ready(void) {
    reset_world();
    struct task *t = task_create("blocker", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(t);
    t->state = TASK_STATE_BLOCKED;
    t->wait_channel = (void *)0xfeedfaceULL;

    scheduler_unblock((void *)0xfeedfaceULL);

    TEST("scheduler_unblock promotes BLOCKED to READY for matching channel");
    if (t->state == TASK_STATE_READY) PASS();
    else FAIL("not promoted");

    TEST("scheduler_unblock clears wait_channel");
    if (t->wait_channel == (void *)0) PASS();
    else FAIL("wait_channel not cleared");
}

static void test_unblock_ignores_other_channels(void) {
    reset_world();
    struct task *t = task_create("waiter", noop_entry, (void *)0,
                                 TASK_PRIORITY_NORMAL);
    scheduler_add(t);
    t->state = TASK_STATE_BLOCKED;
    t->wait_channel = (void *)0xaaaaULL;

    scheduler_unblock((void *)0xbbbbULL);

    TEST("scheduler_unblock leaves non-matching channels untouched");
    if (t->state == TASK_STATE_BLOCKED && t->wait_channel == (void *)0xaaaaULL)
        PASS();
    else FAIL("non-matching unblock disturbed task");
}

int test_context_switch_run(void) {
    printf("[test_context_switch]\n");
    tests_run = 0;
    tests_passed = 0;
    test_layout_locks_asm_contract();
    test_init_resets_stats();
    test_pick_next_priority();
    test_pick_next_cooperative();
    test_tick_advances_counters();
    test_tick_does_not_switch_in_cooperative();
    test_tick_wakes_sleepers();
    test_tick_reaps_zombies();
    test_block_triggers_context_switch();
    test_sleep_triggers_context_switch();
    test_unblock_promotes_blocked_to_ready();
    test_unblock_ignores_other_channels();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
