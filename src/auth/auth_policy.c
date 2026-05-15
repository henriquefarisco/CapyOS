#include "auth/auth_policy.h"
#include "kernel/log/klog.h"
#include <stddef.h>

static struct auth_attempt g_attempts[AUTH_MAX_TRACKED_USERS];
static struct auth_policy_config g_config;
static int g_initialized = 0;

static void ap_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static int ap_streq(const char *a, const char *b) {
  while (*a && *b) { if (*a != *b) return 0; a++; b++; }
  return *a == *b;
}

static void ap_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

static uint64_t ap_ticks(void) {
  uint32_t lo, hi;
#if defined(UNIT_TEST) || !defined(__x86_64__)
  /* Host unit tests need only a monotonically increasing counter
   * for relative ordering in lockout windows. */
  static uint64_t fake_ticks = 0;
  (void)lo; (void)hi;
  return ++fake_ticks;
#else
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
#endif
}

void auth_policy_init(void) {
  ap_memset(g_attempts, 0, sizeof(g_attempts));
  g_config.max_attempts = AUTH_DEFAULT_MAX_ATTEMPTS;
  g_config.lockout_duration_ticks = (uint64_t)AUTH_DEFAULT_LOCKOUT_SECONDS * 2000000000ULL;
  g_config.min_password_length = AUTH_DEFAULT_MIN_PASSWORD_LENGTH;
  g_config.audit_enabled = 1;
  g_initialized = 1;
}

void auth_policy_set_config(const struct auth_policy_config *cfg) {
  if (!cfg) {
    return;
  }
  g_config = *cfg;
  if (g_config.max_attempts == 0) {
    g_config.max_attempts = AUTH_DEFAULT_MAX_ATTEMPTS;
  }
  if (g_config.lockout_duration_ticks == 0) {
    g_config.lockout_duration_ticks =
        (uint64_t)AUTH_DEFAULT_LOCKOUT_SECONDS * 2000000000ULL;
  }
  if (g_config.min_password_length == 0) {
    g_config.min_password_length = AUTH_DEFAULT_MIN_PASSWORD_LENGTH;
  }
}

/* Read-only lookup. Returns NULL when the username is not tracked. Used
 * by every entry point that does not need to create a new tracking slot
 * (`check_allowed`, `is_locked`, `record_success`, `unlock`). Keeping
 * these paths non-allocating closes a denial-of-security where an
 * attacker could exhaust `g_attempts[AUTH_MAX_TRACKED_USERS]` by simply
 * probing many forged usernames via `check_allowed`, blocking lockouts
 * from being installed for legitimate users. */
static struct auth_attempt *find_existing(const char *username) {
  if (!username) return NULL;
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (g_attempts[i].username[0] && ap_streq(g_attempts[i].username, username))
      return &g_attempts[i];
  }
  return NULL;
}

static struct auth_attempt *find_or_alloc(const char *username) {
  if (!username) return NULL;
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (g_attempts[i].username[0] && ap_streq(g_attempts[i].username, username))
      return &g_attempts[i];
  }
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (!g_attempts[i].username[0]) {
      ap_strcpy(g_attempts[i].username, username, AUTH_USERNAME_MAX);
      return &g_attempts[i];
    }
  }

  /* Table full. Reclaim naturally-expired lockouts first, then evict
   * the least-recently-active non-locked entry. Locked entries are
   * sticky -- evicting one would let an attacker reset their own
   * lockout by spraying fresh usernames, so we refuse (return NULL)
   * when every slot is currently locked. */
  int lru_idx = -1;
  uint64_t lru_tick = ~(uint64_t)0;
  uint64_t now = ap_ticks();
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (g_attempts[i].locked && now >= g_attempts[i].lockout_until_tick) {
      g_attempts[i].locked = 0;
      g_attempts[i].failed_count = 0;
    }
    if (g_attempts[i].locked) continue;
    if (g_attempts[i].last_fail_tick < lru_tick) {
      lru_tick = g_attempts[i].last_fail_tick;
      lru_idx = i;
    }
  }
  if (lru_idx < 0) {
    return NULL;
  }
  ap_memset(&g_attempts[lru_idx], 0, sizeof(g_attempts[lru_idx]));
  ap_strcpy(g_attempts[lru_idx].username, username, AUTH_USERNAME_MAX);
  return &g_attempts[lru_idx];
}

int auth_policy_check_allowed(const char *username) {
  if (!g_initialized || !username) return 1;
  struct auth_attempt *a = find_existing(username);
  if (!a) return 1;

  if (a->locked) {
    uint64_t now = ap_ticks();
    if (now >= a->lockout_until_tick) {
      a->locked = 0;
      a->failed_count = 0;
      return 1;
    }
    return 0;
  }
  return 1;
}

void auth_policy_record_success(const char *username) {
  if (!g_initialized || !username) return;
  if (g_config.audit_enabled)
    klog(KLOG_INFO, "[auth] Login success.");
  /* Use the non-allocating lookup so a fresh successful login does not
   * pollute the tracking table with a new entry. The function only
   * needs to clear failure state when an entry already exists. */
  struct auth_attempt *a = find_existing(username);
  if (!a) return;
  a->failed_count = 0;
  a->locked = 0;
}

void auth_policy_record_failure(const char *username) {
  if (!g_initialized || !username) return;
  struct auth_attempt *a = find_or_alloc(username);
  if (!a) return;

  a->failed_count++;
  a->last_fail_tick = ap_ticks();

  if (g_config.audit_enabled)
    klog(KLOG_WARN, "[auth] Login failure recorded.");

  if (a->failed_count >= g_config.max_attempts) {
    a->locked = 1;
    a->lockout_until_tick = a->last_fail_tick + g_config.lockout_duration_ticks;
    klog(KLOG_WARN, "[auth] Account locked due to repeated failures.");
  }
}

int auth_policy_is_locked(const char *username) {
  if (!g_initialized || !username) return 0;
  struct auth_attempt *a = find_existing(username);
  if (!a) return 0;
  if (a->locked) {
    uint64_t now = ap_ticks();
    if (now >= a->lockout_until_tick) {
      a->locked = 0;
      a->failed_count = 0;
      return 0;
    }
    return 1;
  }
  return 0;
}

void auth_policy_unlock(const char *username) {
  if (!g_initialized || !username) return;
  struct auth_attempt *a = find_existing(username);
  if (a) { a->locked = 0; a->failed_count = 0; }
}

int auth_policy_validate_password(const char *password, const char **reason) {
  uint32_t min_len = g_initialized ? g_config.min_password_length
                                   : AUTH_DEFAULT_MIN_PASSWORD_LENGTH;
  uint32_t len = 0;
  if (reason) {
    *reason = NULL;
  }
  if (!password) {
    if (reason) {
      *reason = "password is required";
    }
    return -1;
  }
  while (password[len]) {
    ++len;
  }
  if (len < min_len) {
    if (reason) {
      *reason = "password is shorter than the configured minimum length";
    }
    return -1;
  }
  return 0;
}

static void ap_print_u32(void (*print)(const char *), uint32_t v) {
  char buf[12]; int p = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = 0; print(buf); return; }
  char tmp[12]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[p++] = tmp[i];
  buf[p] = 0; print(buf);
}

uint32_t auth_policy_tracked_count(void) {
  uint32_t count = 0;
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (g_attempts[i].username[0]) count++;
  }
  return count;
}

uint32_t auth_policy_locked_count(void) {
  uint32_t count = 0;
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (g_attempts[i].username[0] && g_attempts[i].locked) count++;
  }
  return count;
}

#if defined(UNIT_TEST)
int auth_policy_is_tracked(const char *username) {
  return find_existing(username) != NULL ? 1 : 0;
}
#endif

void auth_policy_status(void (*print)(const char *)) {
  /*
   * PRIVACY: This function used to iterate over every tracked
   * `g_attempts[i]` slot and print the `username` of each user with at
   * least one failed login, alongside their failure count and lockout
   * state. It is reachable from the shell `auth-status` command (see
   * `src/shell/commands/extended.c::cmd_auth_status`), which has NO
   * privilege check today, so any local shell session — including
   * non-admin users and guests — could enumerate:
   *   - the list of every account that ever failed a login on this
   *     machine (user enumeration);
   *   - the exact failure count for each (timing/strategy signal);
   *   - which accounts are currently locked out (lets an attacker
   *     wait for the lockout window to lapse before resuming
   *     credential stuffing against a specific target).
   * That is a PII/security disclosure regardless of how few users a
   * single machine has. The remediation collapses the per-account
   * detail into aggregate counters that carry no identifying
   * information. Internal callers that need to assert specific
   * tracking state (unit tests) use `auth_policy_is_tracked` instead,
   * which is only exposed under `UNIT_TEST` builds.
   */
  if (!print) return;
  print("Auth policy: max_attempts=");
  ap_print_u32(print, g_config.max_attempts);
  print(" min_password_length=");
  ap_print_u32(print, g_config.min_password_length);
  print(" audit="); print(g_config.audit_enabled ? "on" : "off");
  print("\nTracked accounts: ");
  ap_print_u32(print, auth_policy_tracked_count());
  print(" (locked: ");
  ap_print_u32(print, auth_policy_locked_count());
  print(")\n");
}
