#ifndef CORE_AUTH_POLICY_H
#define CORE_AUTH_POLICY_H

#include <stdint.h>
#include <stddef.h>

#define AUTH_MAX_TRACKED_USERS 32
#define AUTH_USERNAME_MAX 32
#define AUTH_DEFAULT_MAX_ATTEMPTS 5
#define AUTH_DEFAULT_LOCKOUT_SECONDS 300

struct auth_attempt {
  char username[AUTH_USERNAME_MAX];
  uint32_t failed_count;
  uint64_t last_fail_tick;
  uint64_t lockout_until_tick;
  int locked;
};

struct auth_policy_config {
  uint32_t max_attempts;
  uint64_t lockout_duration_ticks;
  int audit_enabled;
};

void auth_policy_init(void);
void auth_policy_set_config(const struct auth_policy_config *cfg);
int auth_policy_check_allowed(const char *username);
void auth_policy_record_success(const char *username);
void auth_policy_record_failure(const char *username);
int auth_policy_is_locked(const char *username);
void auth_policy_unlock(const char *username);
void auth_policy_status(void (*print)(const char *));

#endif /* CORE_AUTH_POLICY_H */
