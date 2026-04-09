#include <stdio.h>
#include <string.h>
#include "core/boot_slot.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  %-40s ", name);
#define PASS() printf("OK\n"); tests_passed++; } while(0)
#define FAIL(msg) printf("FAIL: %s\n", msg); } while(0)

void test_boot_slot_init_and_get(void) {
  boot_slot_init();

  TEST("boot_slot_get_active returns slot A");
  struct boot_slot s;
  if (boot_slot_get_active(&s) == 0 && s.state == BOOT_SLOT_EMPTY) { PASS(); }
  else { FAIL("bad initial state"); }

  TEST("boot_slot_get both slots");
  struct boot_slot a, b;
  if (boot_slot_get(0, &a) == 0 && boot_slot_get(1, &b) == 0 &&
      a.state == BOOT_SLOT_EMPTY && b.state == BOOT_SLOT_EMPTY) { PASS(); }
  else { FAIL("slots not empty"); }
}

void test_boot_slot_stage_activate(void) {
  boot_slot_init();

  TEST("boot_slot_stage sets version");
  if (boot_slot_stage(0, "0.9.0", 0xDEAD) == 0) {
    struct boot_slot s;
    boot_slot_get(0, &s);
    if (s.state == BOOT_SLOT_VALID && strcmp(s.version, "0.9.0") == 0) { PASS(); }
    else { FAIL("stage did not set correctly"); }
  } else { FAIL("stage returned error"); }

  TEST("boot_slot_activate changes state");
  if (boot_slot_activate(0) == 0) {
    struct boot_slot s;
    boot_slot_get(0, &s);
    if (s.state == BOOT_SLOT_ACTIVE && s.boot_count == 1) { PASS(); }
    else { FAIL("activate did not work"); }
  } else { FAIL("activate returned error"); }
}

void test_boot_slot_health_rollback(void) {
  boot_slot_init();
  boot_slot_stage(0, "0.9.0", 0xAA);
  boot_slot_stage(1, "0.8.0", 0xBB);
  boot_slot_activate(0);

  TEST("boot_slot_needs_rollback after activate");
  if (boot_slot_needs_rollback() == 1) { PASS(); }
  else { FAIL("should need rollback"); }

  TEST("boot_slot_confirm_health clears rollback");
  boot_slot_confirm_health();
  if (boot_slot_needs_rollback() == 0) { PASS(); }
  else { FAIL("should not need rollback after confirm"); }

  /* Reset and test rollback */
  boot_slot_init();
  boot_slot_stage(0, "0.9.0", 0xAA);
  boot_slot_stage(1, "0.8.0", 0xBB);
  boot_slot_activate(0);

  TEST("boot_slot_rollback switches to slot B");
  if (boot_slot_rollback() == 0) {
    struct boot_slot s;
    boot_slot_get_active(&s);
    if (strcmp(s.version, "0.8.0") == 0 && s.state == BOOT_SLOT_ACTIVE) { PASS(); }
    else { FAIL("rollback did not switch"); }
  } else { FAIL("rollback failed"); }
}

int test_boot_slot_run(void) {
  printf("[test_boot_slot]\n");
  tests_run = 0;
  tests_passed = 0;
  test_boot_slot_init_and_get();
  test_boot_slot_stage_activate();
  test_boot_slot_health_rollback();
  printf("  %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
