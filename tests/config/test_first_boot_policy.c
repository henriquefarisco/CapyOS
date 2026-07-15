#include "config/first_boot_policy.h"

#include <stdio.h>

static int expect_int(int actual, int expected, const char *name) {
  if (actual != expected) {
    printf("[FAIL] first_boot_policy: %s (got %d, expected %d)\n", name,
           actual, expected);
    return 1;
  }
  return 0;
}

int run_first_boot_policy_tests(void) {
  int fails = 0;
  fails += expect_int(first_boot_setup_required(0, 0, 0), 1,
                      "empty volume requires setup");
  fails += expect_int(first_boot_setup_required(0, 1, 1), 1,
                      "users and config without marker require setup");
  fails += expect_int(first_boot_setup_required(1, 0, 0), 1,
                      "marker without state requires setup");
  fails += expect_int(first_boot_setup_required(1, 1, 0), 1,
                      "marker without config requires setup");
  fails += expect_int(first_boot_setup_required(1, 0, 1), 1,
                      "marker without users requires setup");
  fails += expect_int(first_boot_setup_required(1, 1, 1), 0,
                      "complete committed state skips setup");
  if (fails == 0) {
    printf("[OK] first_boot_policy\n");
  }
  return fails;
}
