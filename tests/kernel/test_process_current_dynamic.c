/*
 * tests/test_process_current_dynamic.c (2026-05-02)
 *
 * Regression test for the ROOT-CAUSE fix in
 * `src/kernel/process.c::process_current`: the function used to
 * return a static `current_proc` that was set to NULL by
 * `process_system_init` and never updated again in production
 * (no caller ever invoked `process_set_current`). Every syscall
 * handler that resolved its caller via `process_current()` was
 * therefore broken: sys_read/sys_write fell back to stdin_buf /
 * debugcon, sys_open/sys_close/sys_pipe returned -1, sys_fork /
 * sys_exec / sys_wait returned -1, and the page-fault classifier
 * could not identify the offending process.
 *
 * The visible symptom was "browser engine never sees NAVIGATE":
 * the engine's `capy_read(0, ...)` resolved fd 0 to stdin_buf
 * (always empty) instead of to its request pipe, because the
 * FD-table lookup in sys_read needed `process_current()` to be
 * the engine's process slot.
 *
 * Contract this test locks:
 *
 *   1. After `task_set_current(p->main_thread)`, calling
 *      `process_current()` returns `p` (without any call to
 *      `process_set_current`). This is the production path.
 *   2. With a UNUSED slot, the dynamic resolution skips it and
 *      keeps walking.
 *   3. With multiple live processes, each task maps back to its
 *      own process (no aliasing).
 *   4. With `task_current() == NULL`, `process_current()` returns
 *      NULL (kernel-only / pre-bootstrap context).
 *   5. The legacy `process_set_current(p)` override still works
 *      for host tests that need to fake a process without a task.
 *
 * Without these guarantees the kernel cannot route syscalls to
 * the right process and any future refactor that breaks the
 * resolution silently regresses the entire user-mode subsystem.
 */

#include "kernel/process.h"
#include "kernel/task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void task_set_current(struct task *t);
extern void process_set_current(struct process *p);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                          \
    do {                                                                    \
        tests_run++;                                                        \
        printf("  %-58s ", name);                                           \
    } while (0)
#define PASS()                                                              \
    do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static void reset_world(void) {
    process_system_init();
    task_system_init();
    process_set_current((struct process *)0);
    task_set_current((struct task *)0);
}

/* === 1. task_set_current alone is enough to drive process_current === */
static void test_task_drives_process_current(void) {
    reset_world();
    struct process *p = process_create("driver", 0u, 0u);
    if (!p || !p->main_thread) {
        TEST("setup process");
        FAIL("alloc");
        return;
    }
    /* Production path: scheduler updates task_current via
     * task_set_current; nothing ever calls process_set_current.
     * After this single call, process_current() must return p. */
    task_set_current(p->main_thread);

    TEST("process_current resolves dynamically from task_current");
    if (process_current() == p) PASS();
    else FAIL("process_current() did not match the task's process");
}

/* === 2. UNUSED slots are skipped during the scan ============== */
static void test_unused_slots_skipped(void) {
    reset_world();
    /* Allocate three processes, then destroy the middle one to
     * leave a hole. The dynamic resolver must keep walking past
     * the UNUSED slot to find the live one. */
    struct process *a = process_create("a", 0u, 0u);
    struct process *b = process_create("b", 0u, 0u);
    struct process *c = process_create("c", 0u, 0u);
    if (!a || !b || !c) {
        TEST("setup three processes");
        FAIL("alloc");
        return;
    }
    /* Destroy `b` -> its slot becomes UNUSED. */
    process_destroy(b);
    task_set_current(c->main_thread);

    TEST("process_current skips UNUSED slots and finds c");
    if (process_current() == c) PASS();
    else FAIL("did not return c after middle slot was destroyed");
}

/* === 3. Multiple live processes do not alias =================== */
static void test_multiple_processes_no_alias(void) {
    reset_world();
    struct process *a = process_create("a", 0u, 0u);
    struct process *b = process_create("b", 0u, 0u);
    if (!a || !b) {
        TEST("setup two processes");
        FAIL("alloc");
        return;
    }

    task_set_current(a->main_thread);
    int a_ok = (process_current() == a);
    task_set_current(b->main_thread);
    int b_ok = (process_current() == b);
    task_set_current(a->main_thread);
    int back_to_a = (process_current() == a);

    TEST("process_current tracks task_current swaps without alias");
    if (a_ok && b_ok && back_to_a) PASS();
    else FAIL("aliased between processes");
}

/* === 4. NULL task_current => NULL process_current ============== */
static void test_null_task_returns_null(void) {
    reset_world();
    struct process *p = process_create("solo", 0u, 0u);
    if (!p) {
        TEST("setup process");
        FAIL("alloc");
        return;
    }
    /* Even with a live process in the table, if no task is current
     * (kernel boot path, pre-scheduler), process_current() must
     * return NULL rather than guess. */
    task_set_current((struct task *)0);

    TEST("process_current returns NULL when task_current is NULL");
    if (process_current() == NULL) PASS();
    else FAIL("returned a process despite null task");
}

/* === 5. process_set_current override still wins ================ */
static void test_legacy_override_wins(void) {
    reset_world();
    struct process *p = process_create("override-target", 0u, 0u);
    struct process *q = process_create("override-source", 0u, 0u);
    if (!p || !q) {
        TEST("setup two processes");
        FAIL("alloc");
        return;
    }
    /* Set task_current to q's main thread but override
     * process_current with p. The override path must win so
     * existing host tests that fake a process without a task
     * (test_syscall_pipe_priority.c) keep working. */
    task_set_current(q->main_thread);
    process_set_current(p);

    TEST("process_set_current(p) overrides dynamic resolution");
    if (process_current() == p) PASS();
    else FAIL("override did not win");

    /* Clearing the override falls back to the dynamic path. */
    process_set_current((struct process *)0);
    TEST("clearing override falls back to dynamic resolution (q)");
    if (process_current() == q) PASS();
    else FAIL("did not fall back to q");
}

/* === Entry point ============================================== */
int test_process_current_dynamic_run(void) {
    printf("[test_process_current_dynamic]\n");
    tests_run = 0;
    tests_passed = 0;

    test_task_drives_process_current();
    test_unused_slots_skipped();
    test_multiple_processes_no_alias();
    test_null_task_returns_null();
    test_legacy_override_wins();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
