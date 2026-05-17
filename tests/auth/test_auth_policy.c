#include <stdio.h>
#include <string.h>

#include "auth/auth_policy.h"
#include "auth/user.h"

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

  /* Contract for `userdb_authenticate_with_policy` result codes: callers
   * such as the system setup login flow and any future GUI submit path
   * branch on these constants to distinguish locked accounts from generic
   * authentication failures. Drift here would silently degrade the
   * user-visible behaviour (e.g. locked accounts would render as "invalid
   * credentials"), so the values are locked by static review. */
  failures += expect_true(USERDB_AUTH_OK == 0,
                          "USERDB_AUTH_OK must remain 0");
  failures += expect_true(USERDB_AUTH_FAILED == -1,
                          "USERDB_AUTH_FAILED must remain -1");
  failures += expect_true(USERDB_AUTH_LOCKED == -2,
                          "USERDB_AUTH_LOCKED must remain -2");
  failures += expect_true(USERDB_AUTH_OK != USERDB_AUTH_FAILED,
                          "OK and FAILED must remain distinct");
  failures += expect_true(USERDB_AUTH_OK != USERDB_AUTH_LOCKED,
                          "OK and LOCKED must remain distinct");
  failures += expect_true(USERDB_AUTH_FAILED != USERDB_AUTH_LOCKED,
                          "FAILED and LOCKED must remain distinct");

  /* alpha.212: timing-equalised lockout. `userdb_authenticate_with_policy`
   * must perform the same crypto work (PBKDF2 over `password` + dummy
   * salt fallback) on the locked path as on the not-locked path so that
   * a remote attacker cannot distinguish locked accounts by observing
   * response latency. The full timing assertion lives in code review of
   * `src/auth/userdb_auth.c::userdb_authenticate_with_policy` (validated by
   * presence of `userdb_authenticate(username, password, out)` ABOVE
   * the `if (!allowed)` branch and by the SECURITY comment block); this
   * test only locks the policy-side contract: `auth_policy_check_allowed`
   * must keep returning 0 for locked accounts so the wrapper can detect
   * the lockout AFTER paying the PBKDF2 cost. */
  auth_policy_init();
  auth_policy_set_config(&cfg);
  for (uint32_t i = 0; i < cfg.max_attempts; ++i) {
    auth_policy_record_failure("timing-victim");
  }
  failures += expect_true(auth_policy_is_locked("timing-victim") == 1,
                          "victim must be locked after max_attempts failures");
  failures += expect_true(auth_policy_check_allowed("timing-victim") == 0,
                          "check_allowed must report 0 for locked accounts");
  failures += expect_true(auth_policy_check_allowed("not-locked") == 1,
                          "check_allowed must report 1 for fresh accounts");

  /* alpha.208: read paths must not pollute the tracking table. Before
   * alpha.208, `find_or_alloc` was called from `check_allowed` and
   * `is_locked` too, so probing many forged usernames via these
   * read-only entry points would exhaust `g_attempts[]` and silently
   * disable lockout for legitimate users (find_or_alloc returns NULL
   * when the table is full, and the callers treat NULL as "allow"). */
  auth_policy_init();
  auth_policy_set_config(&cfg);
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS + 5; i++) {
    char probe[16];
    snprintf(probe, sizeof(probe), "probe-%02d", i);
    failures += expect_true(auth_policy_check_allowed(probe) == 1,
                            "fresh probe should be allowed without tracking");
    failures += expect_true(auth_policy_is_locked(probe) == 0,
                            "fresh probe should not be locked");
  }
  memset(g_status_capture, 0, sizeof(g_status_capture));
  g_status_capture_len = 0;
  auth_policy_status(capture_status);
  /* alpha.211: status output never emits usernames; assert that the
   * tracking table has not been polluted by read-only probes using the
   * aggregate counter exposed for this purpose. */
  failures += expect_true(auth_policy_tracked_count() == 0,
                          "read-only probes must not allocate tracking slots");
  failures += expect_true(auth_policy_is_tracked("probe-00") == 0,
                          "probe-00 must not appear in the tracking table");
  /* And the privacy-preserving status output must not leak the username
   * even if some path were to allocate it inadvertently. */
  failures += expect_true(strstr(g_status_capture, "probe-00") == NULL,
                          "status output must not leak any username");

  /* alpha.208: when the tracking table is full of non-locked entries,
   * `record_failure` must evict the least-recently-active entry to make
   * room for the new one. Locked entries are excluded from eviction so
   * an active lockout cannot be reset by spraying fresh usernames. */
  auth_policy_init();
  auth_policy_set_config(&cfg);
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    char fill[16];
    snprintf(fill, sizeof(fill), "fill-%02d", i);
    auth_policy_record_failure(fill);
  }
  failures += expect_true(auth_policy_is_locked("fill-00") == 0,
                          "fill-00 has 1 failure, below max_attempts threshold");
  auth_policy_record_failure("newcomer");
  memset(g_status_capture, 0, sizeof(g_status_capture));
  g_status_capture_len = 0;
  auth_policy_status(capture_status);
  /* alpha.211: assert tracking state via the test-only `auth_policy_is_tracked`
   * hook instead of grepping the status output for usernames, because
   * the privacy-preserving status output now emits only aggregate
   * counters and never per-account identifiers. */
  failures += expect_true(auth_policy_is_tracked("newcomer") == 1,
                          "newcomer must be tracked after LRU eviction");
  failures += expect_true(auth_policy_is_tracked("fill-00") == 0,
                          "LRU entry fill-00 must be evicted to make room");
  failures += expect_true(strstr(g_status_capture, "newcomer") == NULL,
                          "status output must not leak the newcomer username");
  failures += expect_true(strstr(g_status_capture, "fill-") == NULL,
                          "status output must not leak any fill-* username");
  /* The aggregate counters must add up to the contents of the tracking
   * table: AUTH_MAX_TRACKED_USERS - 1 surviving fill-* entries plus the
   * newcomer, none currently locked because each has only one failure. */
  failures += expect_true(
      auth_policy_tracked_count() == (uint32_t)AUTH_MAX_TRACKED_USERS,
      "tracking table must be saturated after eviction");
  failures += expect_true(auth_policy_locked_count() == 0,
                          "no entries should be locked at this stage");

  if (failures == 0) {
    printf("[tests] auth_policy OK\n");
  }
  return failures;
}
