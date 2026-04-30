/*
 * Tests for task_stats_get (include/kernel/task_iter.h).
 *
 * Covers:
 *  - PID 0 -> -1
 *  - unknown PID -> -1
 *  - valid PID -> 0 with stats populated
 *  - NULL stats -> -1 even for a valid PID
 *  - killed task -> stats no longer available.
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

static void test_pid_zero_is_invalid(void) {
    struct task_stats s;
    task_system_init();
    TEST("task_stats_get(0, &s) returns -1");
    if (task_stats_get(0u, &s) == -1) PASS();
    else FAIL("expected -1 for PID 0");
}

static void test_unknown_pid(void) {
    struct task_stats s;
    task_system_init();
    TEST("task_stats_get(unknown_pid, &s) returns -1");
    if (task_stats_get(99999u, &s) == -1) PASS();
    else FAIL("expected -1 for unknown PID");
}

static void test_null_stats_pointer(void) {
    struct task *t;
    task_system_init();
    t = task_create("hello", noop_entry, NULL, TASK_PRIORITY_NORMAL);
    TEST("task_stats_get(valid, NULL) returns -1");
    if (t && task_stats_get(t->pid, NULL) == -1) PASS();
    else FAIL("expected -1 for NULL stats");
}

static void test_valid_pid_populates_stats(void) {
    struct task *t;
    struct task_stats s;
    int rc;

    task_system_init();
    t = task_create("worker", noop_entry, NULL, TASK_PRIORITY_HIGH);
    TEST("task_create produced a task");
    if (t && t->pid > 0u) PASS();
    else FAIL("task_create returned NULL");

    memset(&s, 0xCC, sizeof(s));
    rc = task_stats_get(t->pid, &s);
    TEST("task_stats_get(valid_pid, &s) returns 0");
    if (rc == 0) PASS();
    else FAIL("expected 0 for valid PID");

    TEST("stats.pid matches");
    if (s.pid == t->pid) PASS();
    else FAIL("pid mismatch");

    TEST("stats.name matches");
    if (strcmp(s.name, "worker") == 0) PASS();
    else FAIL("name not copied");

    TEST("stats.priority matches");
    if (s.priority == TASK_PRIORITY_HIGH) PASS();
    else FAIL("priority not copied");

    TEST("stats.state is READY for fresh task");
    if (s.state == TASK_STATE_READY) PASS();
    else FAIL("state not READY");

    TEST("stats.cpu_time_ns starts at 0");
    if (s.cpu_time_ns == 0) PASS();
    else FAIL("cpu_time_ns must start at 0");
}

static void test_killed_pid_unavailable(void) {
    struct task *t;
    struct task_stats s;
    uint32_t pid;

    task_system_init();
    t = task_create("doomed", noop_entry, NULL, TASK_PRIORITY_NORMAL);
    pid = t->pid;
    task_kill(pid);

    TEST("task_stats_get on killed PID returns -1");
    if (task_stats_get(pid, &s) == -1) PASS();
    else FAIL("expected -1 after kill");
}

int test_task_stats_run(void) {
    printf("[test_task_stats]\n");
    tests_run = 0;
    tests_passed = 0;
    test_pid_zero_is_invalid();
    test_unknown_pid();
    test_null_stats_pointer();
    test_valid_pid_populates_stats();
    test_killed_pid_unavailable();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
