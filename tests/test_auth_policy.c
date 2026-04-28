#include <stdio.h>
#include <string.h>

#include "auth/auth_policy.h"

static char g_status_capture[512];
static size_t g_status_capture_len;

static int expect_true(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "[auth_policy] %s\n", message);
    return 1;
  }
  return 0;
}

static void capture_status(const char *text) {
  size_t idx = 0;
  if (!text) {
    return;
  }
  while (text[idx] && g_status_capture_len + 1 < sizeof(g_status_capture)) {
    g_status_capture[g_status_capture_len++] = text[idx++];
  }
  g_status_capture[g_status_capture_len] = '\0';
}

int run_auth_policy_tests(void) {
  int failures = 0;
  const char *reason = NULL;
  struct auth_policy_config cfg;

  auth_policy_init();
  cfg.max_attempts = 3;
  cfg.lockout_duration_ticks = 1ULL << 62;
  cfg.min_password_length = 6;
  cfg.audit_enabled = 0;
  auth_policy_set_config(&cfg);

  failures += expect_true(auth_policy_validate_password("12345", &reason) != 0,
                          "short password should be rejected");
  failures += expect_true(reason && strstr(reason, "minimum") != NULL,
                          "short password should report a policy reason");
  failures += expect_true(auth_policy_validate_password("123456", &reason) == 0,
                          "password meeting minimum length should pass");

  failures += expect_true(auth_policy_check_allowed("admin") == 1,
                          "fresh user should be allowed");
  auth_policy_record_failure("admin");
  auth_policy_record_failure("admin");
  failures += expect_true(auth_policy_is_locked("admin") == 0,
                          "account should remain unlocked before threshold");
  auth_policy_record_failure("admin");
  failures += expect_true(auth_policy_is_locked("admin") == 1,
                          "account should lock after configured threshold");
  failures += expect_true(auth_policy_check_allowed("admin") == 0,
                          "locked account should not be allowed");
  auth_policy_unlock("admin");
  failures += expect_true(auth_policy_check_allowed("admin") == 1,
                          "manual unlock should allow login again");
  auth_policy_record_failure("admin");
  auth_policy_record_success("admin");
  failures += expect_true(auth_policy_is_locked("admin") == 0,
                          "success should clear failure state");

  memset(g_status_capture, 0, sizeof(g_status_capture));
  g_status_capture_len = 0;
  auth_policy_status(capture_status);
  failures += expect_true(strstr(g_status_capture, "max_attempts=3") != NULL,
                          "status should expose max attempts");
  failures += expect_true(strstr(g_status_capture, "min_password_length=6") != NULL,
                          "status should expose minimum password length");

  if (failures == 0) {
    printf("[tests] auth_policy OK\n");
  }
  return failures;
}
