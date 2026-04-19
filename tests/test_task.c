#include <stdio.h>
#include <string.h>
#include "kernel/task.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
  do { tests_run++; printf("  %-40s ", name); } while (0)
#define PASS() \
  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) \
  do { printf("FAIL: %s\n", msg); } while (0)

static int dummy_ran = 0;
static void dummy_entry(void *arg) {
  (void)arg;
  dummy_ran = 1;
}

void test_task_init(void) {
  TEST("task_system_init");
  task_system_init();
  if (task_count() == 0) { PASS(); }
  else { FAIL("count should be 0"); }
}

void test_task_create(void) {
  TEST("task_create returns valid task");
  struct task *t = task_create("test", dummy_entry, NULL, TASK_PRIORITY_NORMAL);
  if (t && t->pid > 0 && t->state == TASK_STATE_READY) { PASS(); }
  else { FAIL("task not created properly"); }

  TEST("task_count increments");
  if (task_count() == 1) { PASS(); }
  else { FAIL("count should be 1"); }

  TEST("task_by_pid finds task");
  struct task *found = task_by_pid(t->pid);
  if (found == t) { PASS(); }
  else { FAIL("task not found by pid"); }
}

void test_task_kill(void) {
  task_system_init();
  struct task *t = task_create("killme", dummy_entry, NULL, TASK_PRIORITY_NORMAL);
  uint32_t pid = t->pid;

  TEST("task_kill removes task");
  int r = task_kill(pid);
  if (r == 0 && task_by_pid(pid) == NULL) { PASS(); }
  else { FAIL("task not killed"); }

  TEST("task_count decrements after kill");
  if (task_count() == 0) { PASS(); }
  else { FAIL("count should be 0"); }
}

void test_task_name(void) {
  task_system_init();
  TEST("task name preserved");
  struct task *t = task_create("myname", dummy_entry, NULL, TASK_PRIORITY_HIGH);
  if (t && strcmp(t->name, "myname") == 0) { PASS(); }
  else { FAIL("name mismatch"); }
}

void test_task_max(void) {
  task_system_init();
  TEST("task_create up to TASK_MAX_COUNT");
  int created = 0;
  for (int i = 0; i < TASK_MAX_COUNT + 5; i++) {
    struct task *t = task_create("bulk", dummy_entry, NULL, TASK_PRIORITY_NORMAL);
    if (t) created++;
  }
  if (created == TASK_MAX_COUNT) { PASS(); }
  else { FAIL("should create exactly TASK_MAX_COUNT"); }
}

int test_task_run(void) {
  printf("[test_task]\n");
  tests_run = 0;
  tests_passed = 0;
  test_task_init();
  test_task_create();
  test_task_kill();
  test_task_name();
  test_task_max();
  printf("  %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
