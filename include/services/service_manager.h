#ifndef CORE_SERVICE_MANAGER_H
#define CORE_SERVICE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#define SYSTEM_SERVICE_NAME_MAX 24u
#define SYSTEM_SERVICE_SUMMARY_MAX 80u
#define SYSTEM_SERVICE_TARGET_NAME_MAX 16u

enum system_service_id {
  SYSTEM_SERVICE_LOGGER = 0,
  SYSTEM_SERVICE_NETWORKD,
  SYSTEM_SERVICE_UPDATE_AGENT,
  SYSTEM_SERVICE_COUNT
};

enum system_service_target_id {
  SYSTEM_SERVICE_TARGET_CORE = 0,
  SYSTEM_SERVICE_TARGET_NETWORK,
  SYSTEM_SERVICE_TARGET_MAINTENANCE,
  SYSTEM_SERVICE_TARGET_FULL,
  SYSTEM_SERVICE_TARGET_COUNT
};

enum system_service_startup {
  SYSTEM_SERVICE_STARTUP_BOOT = 0,
  SYSTEM_SERVICE_STARTUP_MANUAL,
  SYSTEM_SERVICE_STARTUP_BLOCKED
};

enum system_service_state {
  SYSTEM_SERVICE_STATE_UNKNOWN = 0,
  SYSTEM_SERVICE_STATE_STARTING,
  SYSTEM_SERVICE_STATE_READY,
  SYSTEM_SERVICE_STATE_DEGRADED,
  SYSTEM_SERVICE_STATE_BLOCKED,
  SYSTEM_SERVICE_STATE_STOPPED
};

typedef int (*system_service_poll_fn)(void *ctx);
typedef int (*system_service_action_fn)(void *ctx);

struct system_service_status {
  uint32_t id;
  char name[SYSTEM_SERVICE_NAME_MAX];
  char summary[SYSTEM_SERVICE_SUMMARY_MAX];
  uint8_t critical;
  uint8_t startup;
  uint8_t state;
  int32_t last_result;
  uint32_t transitions;
  uint32_t polls;
  uint32_t poll_interval_ticks;
  uint32_t failures;
  uint32_t restarts;
  uint32_t backoff_ticks;
  uint32_t dependency_mask;
  uint32_t restart_limit;
};

struct system_service_target_status {
  uint32_t id;
  char name[SYSTEM_SERVICE_TARGET_NAME_MAX];
  uint32_t service_mask;
};

void service_manager_init(void);
void service_manager_reset(void);
void service_manager_bootstrap_defaults(void);
int service_manager_set_state(uint32_t id, uint8_t state, int32_t last_result,
                              const char *summary);
int service_manager_get(uint32_t id, struct system_service_status *out);
int service_manager_get_at(size_t index, struct system_service_status *out);
size_t service_manager_count(void);
int service_manager_set_poll(uint32_t id, system_service_poll_fn poll,
                             void *ctx);
int service_manager_set_control(uint32_t id, system_service_action_fn start,
                                system_service_action_fn stop, void *ctx);
int service_manager_set_poll_interval(uint32_t id, uint32_t interval_ticks);
int service_manager_set_dependencies(uint32_t id, uint32_t dependency_mask);
int service_manager_set_restart_limit(uint32_t id, uint32_t restart_limit);
int service_manager_poll_once(void);
int service_manager_poll_due(uint64_t now_ticks);
int service_manager_start(uint32_t id);
int service_manager_stop(uint32_t id);
int service_manager_restart(uint32_t id);
size_t service_manager_target_count(void);
int service_manager_target_current(struct system_service_target_status *out);
int service_manager_target_get_at(size_t index,
                                  struct system_service_target_status *out);
int service_manager_target_find(const char *name,
                                struct system_service_target_status *out);
int service_manager_target_apply(uint32_t id);
const char *service_manager_state_label(uint8_t state);
const char *service_manager_startup_label(uint8_t startup);
const char *service_manager_target_label(uint32_t id);

#endif /* CORE_SERVICE_MANAGER_H */
