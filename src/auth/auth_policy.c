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
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

void auth_policy_init(void) {
  ap_memset(g_attempts, 0, sizeof(g_attempts));
  g_config.max_attempts = AUTH_DEFAULT_MAX_ATTEMPTS;
  g_config.lockout_duration_ticks = (uint64_t)AUTH_DEFAULT_LOCKOUT_SECONDS * 2000000000ULL;
  g_config.audit_enabled = 1;
  g_initialized = 1;
}

void auth_policy_set_config(const struct auth_policy_config *cfg) {
  if (cfg) g_config = *cfg;
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
  return NULL;
}

int auth_policy_check_allowed(const char *username) {
  if (!g_initialized || !username) return 1;
  struct auth_attempt *a = find_or_alloc(username);
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
  struct auth_attempt *a = find_or_alloc(username);
  if (!a) return;
  a->failed_count = 0;
  a->locked = 0;
  if (g_config.audit_enabled)
    klog(KLOG_INFO, "[auth] Login success.");
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
  struct auth_attempt *a = find_or_alloc(username);
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
  struct auth_attempt *a = find_or_alloc(username);
  if (a) { a->locked = 0; a->failed_count = 0; }
}

static void ap_print_u32(void (*print)(const char *), uint32_t v) {
  char buf[12]; int p = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = 0; print(buf); return; }
  char tmp[12]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[p++] = tmp[i];
  buf[p] = 0; print(buf);
}

void auth_policy_status(void (*print)(const char *)) {
  if (!print) return;
  print("Auth policy: max_attempts=");
  ap_print_u32(print, g_config.max_attempts);
  print(" audit="); print(g_config.audit_enabled ? "on" : "off");
  print("\nTracked accounts:\n");
  for (int i = 0; i < AUTH_MAX_TRACKED_USERS; i++) {
    if (!g_attempts[i].username[0]) continue;
    print("  "); print(g_attempts[i].username);
    print(" fails="); ap_print_u32(print, g_attempts[i].failed_count);
    print(g_attempts[i].locked ? " LOCKED" : " ok");
    print("\n");
  }
}
