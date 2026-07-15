#ifndef SERVICES_CAPYAI_SYSTEM_ACTIONS_H
#define SERVICES_CAPYAI_SYSTEM_ACTIONS_H

#include <stddef.h>
#include <stdint.h>

struct capy_ai_output;

/* Fixed application vocabulary.  Natural-language or model output never
 * becomes an executable name: it must resolve to one of these canonical IDs
 * and then to a callback installed by the CapyOS desktop adapter. */
#define CAPYAI_SYSTEM_APP_COUNT 8u

enum capyai_system_app_action {
  CAPYAI_SYSTEM_APP_OPEN = 1,
  CAPYAI_SYSTEM_APP_CLOSE = 2
};

enum capyai_system_action_result {
  CAPYAI_SYSTEM_ACTION_OK = 0,
  CAPYAI_SYSTEM_ACTION_ERR_INVALID = -1,
  CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED = -2,
  CAPYAI_SYSTEM_ACTION_ERR_BUSY = -3,
  CAPYAI_SYSTEM_ACTION_ERR_TIMEOUT = -4,
  CAPYAI_SYSTEM_ACTION_ERR_FAILED = -5,
  CAPYAI_SYSTEM_ACTION_ERR_STALE = -6
};

typedef int (*capyai_system_app_fn)(void *ctx);

struct capyai_system_app_binding {
  const char *app_id;
  capyai_system_app_fn open;
  capyai_system_app_fn close;
  void *ctx;
};

enum capyai_system_request_state {
  CAPYAI_SYSTEM_REQUEST_IDLE = 0,
  CAPYAI_SYSTEM_REQUEST_QUEUED,
  CAPYAI_SYSTEM_REQUEST_RUNNING,
  CAPYAI_SYSTEM_REQUEST_COMPLETED
};

struct capyai_system_action_snapshot {
  uint64_t ticket;
  uint32_t state;
  uint32_t action;
  char app_id[24];
  int result;
};

/* Replace the complete adapter table atomically.  Exactly one binding for
 * every canonical app ID is required; partial tables fail closed. */
int capyai_system_apps_install(
    const struct capyai_system_app_binding *bindings, size_t count);
int capyai_system_app_id_valid(const char *app_id);

/* Worker-side API. submit never touches compositor state. wait sleeps one
 * scheduler tick between checks and is bounded by timeout_ticks. */
int capyai_system_app_submit(uint32_t action, const char *app_id,
                             uint64_t *out_ticket);
int capyai_system_app_wait(uint64_t ticket, uint32_t timeout_ticks);
int capyai_system_app_request(uint32_t action, const char *app_id,
                              uint32_t timeout_ticks);

/* Foreground-only compatibility API for commands already running in the
 * desktop task. It invokes the fixed registered callback directly and must
 * never be called by a worker. Worker-side callers use request/wait above. */
int capyai_system_app_execute_foreground(uint32_t action,
                                         const char *app_id);

/* Foreground desktop hook. Processes at most one queued mutation per frame. */
int capyai_system_actions_pump(void);
int capyai_system_action_snapshot(struct capyai_system_action_snapshot *out);
void capyai_system_actions_reset_for_tests(void);

/* Observable one-shot reboot schedule backed by core/work_queue. */
typedef int (*capyai_power_reboot_fn)(void *ctx);

enum capyai_power_schedule_state {
  CAPYAI_POWER_SCHEDULE_IDLE = 0,
  CAPYAI_POWER_SCHEDULE_SCHEDULED,
  CAPYAI_POWER_SCHEDULE_CANCELLED,
  CAPYAI_POWER_SCHEDULE_TRIGGERED,
  CAPYAI_POWER_SCHEDULE_FAILED
};

struct capyai_power_schedule_snapshot {
  uint64_t generation;
  uint64_t requested_at_tick;
  uint64_t due_tick;
  uint32_t delay_ticks;
  uint32_t state;
  int last_result;
};

int capyai_power_schedule_configure(capyai_power_reboot_fn reboot, void *ctx);
int capyai_power_schedule_reboot_after(uint64_t now_ticks,
                                       uint32_t delay_ticks);
int capyai_power_schedule_cancel(void);
int capyai_power_schedule_snapshot(struct capyai_power_schedule_snapshot *out);

/* Parse a bounded user-facing duration token. Plain numbers mean minutes;
 * suffixes s/m/h/d are supported. Maximum delay is seven days at 100 Hz. */
int capyai_power_parse_delay_ticks(const char *token, uint32_t *out_ticks);

/* Native typed bridge consumed by capyai_execute_intent_v2. It recognizes
 * only the app/power tuples compiled into the CapyOS executor; file tools use
 * capyai_native_file_dispatch instead. */
int capyai_native_system_dispatch(void *ctx,
                                  const struct capy_ai_output *tool_call,
                                  char *detail, size_t detail_size);
int capyai_native_power_dispatch(const struct capy_ai_output *tool_call,
                                 char *detail, size_t detail_size);

#endif /* SERVICES_CAPYAI_SYSTEM_ACTIONS_H */
