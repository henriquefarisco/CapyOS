#pragma GCC optimize("O0")
#include "services/service_manager.h"

#include <stddef.h>

static struct system_service_status g_services[SYSTEM_SERVICE_COUNT];
static struct system_service_target_status
    g_targets[SYSTEM_SERVICE_TARGET_COUNT];
struct service_poll_binding {
  system_service_poll_fn poll;
  system_service_action_fn start;
  system_service_action_fn stop;
  void *poll_ctx;
  void *control_ctx;
  uint32_t interval_ticks;
  uint32_t consecutive_failures;
  uint32_t restart_attempts;
  uint32_t dependency_mask;
  uint32_t restart_limit;
  uint64_t next_due_tick;
  uint8_t restart_pending;
};

static struct service_poll_binding g_service_polls[SYSTEM_SERVICE_COUNT];
static uint32_t g_current_target = SYSTEM_SERVICE_TARGET_NETWORK;
static int g_services_ready = 0;

static void local_zero(void *ptr, size_t len) {
  uint8_t *dst = (uint8_t *)ptr;
  while (len--) {
    *dst++ = 0;
  }
}

static void local_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (src) {
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int local_equal(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == b[i];
}

static void seed_service(uint32_t id, const char *name, uint8_t critical,
                         uint8_t startup, uint8_t state,
                         const char *summary) {
  struct system_service_status *svc = NULL;
  if (id >= SYSTEM_SERVICE_COUNT) {
    return;
  }
  svc = &g_services[id];
  local_zero(svc, sizeof(*svc));
  svc->id = id;
  svc->critical = critical;
  svc->startup = startup;
  svc->state = state;
  svc->last_result = 0;
  svc->transitions = 1;
  local_copy(svc->name, sizeof(svc->name), name);
  local_copy(svc->summary, sizeof(svc->summary), summary);
}

static void seed_target(uint32_t id, const char *name, uint32_t service_mask) {
  struct system_service_target_status *target = NULL;
  if (id >= SYSTEM_SERVICE_TARGET_COUNT) {
    return;
  }
  target = &g_targets[id];
  local_zero(target, sizeof(*target));
  target->id = id;
  target->service_mask = service_mask;
  local_copy(target->name, sizeof(target->name), name);
}

void service_manager_reset(void) {
  local_zero(g_services, sizeof(g_services));
  local_zero(g_targets, sizeof(g_targets));
  local_zero(g_service_polls, sizeof(g_service_polls));
  g_current_target = SYSTEM_SERVICE_TARGET_NETWORK;
  g_services_ready = 0;
}

void service_manager_bootstrap_defaults(void) {
  local_zero(g_service_polls, sizeof(g_service_polls));
  seed_service(SYSTEM_SERVICE_LOGGER, "logger", 1,
               SYSTEM_SERVICE_STARTUP_BOOT, SYSTEM_SERVICE_STATE_STARTING,
               "boot ring buffer only");
  seed_service(SYSTEM_SERVICE_NETWORKD, "networkd", 1,
               SYSTEM_SERVICE_STARTUP_BOOT, SYSTEM_SERVICE_STATE_STARTING,
               "network bootstrap pending");
  seed_service(SYSTEM_SERVICE_UPDATE_AGENT, "update-agent", 0,
               SYSTEM_SERVICE_STARTUP_MANUAL, SYSTEM_SERVICE_STATE_STOPPED,
               "update catalog idle");
  seed_target(SYSTEM_SERVICE_TARGET_CORE, "core",
              (1u << SYSTEM_SERVICE_LOGGER));
  seed_target(SYSTEM_SERVICE_TARGET_NETWORK, "network",
              (1u << SYSTEM_SERVICE_LOGGER) |
                  (1u << SYSTEM_SERVICE_NETWORKD));
  seed_target(SYSTEM_SERVICE_TARGET_MAINTENANCE, "maintenance",
              (1u << SYSTEM_SERVICE_LOGGER) |
                  (1u << SYSTEM_SERVICE_UPDATE_AGENT));
  seed_target(SYSTEM_SERVICE_TARGET_FULL, "full",
              (1u << SYSTEM_SERVICE_LOGGER) |
                  (1u << SYSTEM_SERVICE_NETWORKD) |
                  (1u << SYSTEM_SERVICE_UPDATE_AGENT));
  g_current_target = SYSTEM_SERVICE_TARGET_NETWORK;
  g_services_ready = 1;
}

void service_manager_init(void) {
  if (!g_services_ready) {
    service_manager_bootstrap_defaults();
  }
}

int service_manager_set_state(uint32_t id, uint8_t state, int32_t last_result,
                              const char *summary) {
  struct system_service_status *svc = NULL;
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  svc = &g_services[id];
  if (svc->state != state || svc->last_result != last_result ||
      (summary && summary[0])) {
    svc->transitions++;
  }
  svc->state = state;
  svc->last_result = last_result;
  if (summary) {
    local_copy(svc->summary, sizeof(svc->summary), summary);
  }
  return 0;
}

int service_manager_get(uint32_t id, struct system_service_status *out) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT || !out) {
    return -1;
  }
  *out = g_services[id];
  return 0;
}

int service_manager_get_at(size_t index, struct system_service_status *out) {
  service_manager_init();
  if (!out || index >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  *out = g_services[index];
  return 0;
}

size_t service_manager_count(void) { return SYSTEM_SERVICE_COUNT; }

size_t service_manager_target_count(void) { return SYSTEM_SERVICE_TARGET_COUNT; }

int service_manager_set_poll(uint32_t id, system_service_poll_fn poll,
                             void *ctx) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].poll = poll;
  g_service_polls[id].poll_ctx = ctx;
  g_service_polls[id].next_due_tick = 0u;
  g_services[id].poll_interval_ticks = g_service_polls[id].interval_ticks;
  return 0;
}

int service_manager_set_poll_interval(uint32_t id, uint32_t interval_ticks) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].interval_ticks = interval_ticks;
  g_service_polls[id].next_due_tick = 0u;
  g_services[id].poll_interval_ticks = interval_ticks;
  return 0;
}

int service_manager_set_dependencies(uint32_t id, uint32_t dependency_mask) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].dependency_mask = dependency_mask;
  g_services[id].dependency_mask = dependency_mask;
  return 0;
}

int service_manager_set_restart_limit(uint32_t id, uint32_t restart_limit) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].restart_limit = restart_limit;
  g_services[id].restart_limit = restart_limit;
  return 0;
}

static int service_pollable(const struct system_service_status *svc,
                            const struct service_poll_binding *binding) {
  uint32_t active_mask = 0u;

  if (g_current_target < SYSTEM_SERVICE_TARGET_COUNT) {
    active_mask = g_targets[g_current_target].service_mask;
  }
  return svc && binding && binding->poll &&
         (active_mask & (1u << svc->id)) != 0u &&
         svc->state != SYSTEM_SERVICE_STATE_BLOCKED &&
         svc->state != SYSTEM_SERVICE_STATE_STOPPED;
}

int service_manager_set_control(uint32_t id, system_service_action_fn start,
                                system_service_action_fn stop, void *ctx) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].start = start;
  g_service_polls[id].stop = stop;
  g_service_polls[id].control_ctx = ctx;
  return 0;
}

static uint32_t service_backoff_ticks(uint32_t interval,
                                      uint32_t consecutive_failures) {
  uint32_t base = interval ? interval : 1u;
  uint32_t shift = consecutive_failures > 5u ? 5u : consecutive_failures;
  return base << shift;
}

static int service_dependencies_ready(uint32_t dependency_mask) {
  for (uint32_t id = 0; id < SYSTEM_SERVICE_COUNT; ++id) {
    if ((dependency_mask & (1u << id)) == 0u) {
      continue;
    }
    if (g_services[id].state != SYSTEM_SERVICE_STATE_READY) {
      return 0;
    }
  }
  return 1;
}

static int service_maybe_wait_dependencies(uint32_t id, uint64_t now_ticks,
                                           int update_due_tick) {
  struct service_poll_binding *binding = &g_service_polls[id];
  uint32_t interval = binding->interval_ticks ? binding->interval_ticks : 1u;

  if (binding->dependency_mask == 0u ||
      service_dependencies_ready(binding->dependency_mask)) {
    return 0;
  }
  (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STARTING, -3,
                                  "waiting for dependencies");
  if (update_due_tick) {
    binding->next_due_tick = now_ticks + (uint64_t)interval;
  }
  return 1;
}

static int service_run_pending_restart(uint32_t id, uint64_t now_ticks,
                                       int update_due_tick) {
  struct service_poll_binding *binding = &g_service_polls[id];
  struct system_service_status *svc = &g_services[id];
  int rc = 0;
  uint32_t interval = binding->interval_ticks ? binding->interval_ticks : 1u;

  if (!binding->restart_pending) {
    return 0;
  }
  if (service_maybe_wait_dependencies(id, now_ticks, update_due_tick)) {
    return 1;
  }
  binding->restart_pending = 0u;
  if (binding->start) {
    rc = binding->start(binding->control_ctx);
    if (rc < 0) {
      binding->restart_pending = 1u;
      svc->backoff_ticks =
          service_backoff_ticks(interval, binding->restart_attempts);
      if (update_due_tick) {
        binding->next_due_tick = now_ticks + (uint64_t)svc->backoff_ticks;
      }
      (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                      "service restart failed");
      return 1;
    }
  }
  binding->consecutive_failures = 0u;
  binding->restart_attempts = 0u;
  svc->backoff_ticks = 0u;
  svc->restarts++;
  (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STARTING, 0,
                                  "service restart applied");
  if (update_due_tick) {
    binding->next_due_tick = now_ticks + (uint64_t)interval;
  }
  return 1;
}

static int service_execute_poll(uint32_t id, uint64_t now_ticks,
                                int update_due_tick) {
  struct service_poll_binding *binding = &g_service_polls[id];
  struct system_service_status *svc = &g_services[id];
  int rc = 0;
  uint32_t interval = 0u;

  if (!service_pollable(svc, binding)) {
    return 0;
  }
  if (service_run_pending_restart(id, now_ticks, update_due_tick)) {
    return 1;
  }
  if (service_maybe_wait_dependencies(id, now_ticks, update_due_tick)) {
    return 0;
  }

  rc = binding->poll(binding->poll_ctx);
  svc->polls++;
  interval = binding->interval_ticks ? binding->interval_ticks : 1u;
  if (rc < 0) {
    binding->consecutive_failures++;
    svc->failures++;
    if (binding->restart_limit != 0u && binding->start && binding->stop &&
        binding->restart_attempts < binding->restart_limit) {
      int stop_rc = binding->stop(binding->control_ctx);
      if (stop_rc < 0) {
        (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_DEGRADED,
                                        stop_rc,
                                        "service stop failed during recovery");
        return 1;
      }
      binding->restart_attempts++;
      binding->restart_pending = 1u;
      svc->backoff_ticks =
          service_backoff_ticks(interval, binding->consecutive_failures - 1u);
      if (update_due_tick) {
        binding->next_due_tick = now_ticks + (uint64_t)svc->backoff_ticks;
      }
      (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STARTING, rc,
                                      "service restart scheduled after failure");
      return 1;
    }
    if (svc->state != SYSTEM_SERVICE_STATE_BLOCKED &&
        svc->state != SYSTEM_SERVICE_STATE_STOPPED) {
      const char *summary = NULL;
      if (binding->restart_limit != 0u && binding->start && binding->stop &&
          binding->restart_attempts >= binding->restart_limit) {
        summary = "service restart limit reached";
      }
      (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                      summary);
    } else {
      svc->last_result = rc;
    }
    svc->backoff_ticks =
        service_backoff_ticks(interval, binding->consecutive_failures - 1u);
    if (update_due_tick) {
      binding->next_due_tick = now_ticks + (uint64_t)svc->backoff_ticks;
    }
    return 1;
  }

  binding->consecutive_failures = 0u;
  binding->restart_attempts = 0u;
  svc->backoff_ticks = 0u;
  if (update_due_tick) {
    binding->next_due_tick = now_ticks + (uint64_t)interval;
  }
  return 1;
}

int service_manager_poll_once(void) {
  int polled = 0;

  service_manager_init();
  for (uint32_t id = 0; id < SYSTEM_SERVICE_COUNT; ++id) {
    polled += service_execute_poll(id, 0u, 0);
  }

  return polled;
}

int service_manager_poll_due(uint64_t now_ticks) {
  int polled = 0;

  service_manager_init();
  for (uint32_t id = 0; id < SYSTEM_SERVICE_COUNT; ++id) {
    struct service_poll_binding *binding = &g_service_polls[id];
    struct system_service_status *svc = &g_services[id];
    uint32_t interval = 0u;

    if (!service_pollable(svc, binding)) {
      continue;
    }
    if (now_ticks < binding->next_due_tick) {
      continue;
    }
    polled += service_execute_poll(id, now_ticks, 1);
    interval = binding->interval_ticks ? binding->interval_ticks : 1u;
    svc->poll_interval_ticks = interval;
  }

  return polled;
}

int service_manager_start(uint32_t id) {
  struct service_poll_binding *binding = NULL;
  struct system_service_status *svc = NULL;
  int rc = 0;

  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  svc = &g_services[id];
  binding = &g_service_polls[id];
  if (svc->startup == SYSTEM_SERVICE_STARTUP_BLOCKED &&
      svc->state == SYSTEM_SERVICE_STATE_BLOCKED) {
    return -2;
  }
  if (!service_dependencies_ready(binding->dependency_mask)) {
    (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STARTING, -3,
                                    "waiting for dependencies");
    return -3;
  }

  svc->backoff_ticks = 0u;
  binding->consecutive_failures = 0u;
  binding->restart_attempts = 0u;
  binding->restart_pending = 0u;
  binding->next_due_tick = 0u;
  if (binding->start) {
    rc = binding->start(binding->control_ctx);
    if (rc < 0) {
      (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                      "service start failed");
      return rc;
    }
  }
  if (svc->state == SYSTEM_SERVICE_STATE_STOPPED ||
      svc->state == SYSTEM_SERVICE_STATE_UNKNOWN) {
    (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STARTING, 0,
                                    "service start requested");
  }
  return 0;
}

int service_manager_stop(uint32_t id) {
  struct service_poll_binding *binding = NULL;
  struct system_service_status *svc = NULL;
  int rc = 0;

  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  svc = &g_services[id];
  binding = &g_service_polls[id];
  if (svc->state == SYSTEM_SERVICE_STATE_BLOCKED) {
    return -2;
  }
  if (binding->stop) {
    rc = binding->stop(binding->control_ctx);
    if (rc < 0) {
      (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                      "service stop failed");
      return rc;
    }
  }
  binding->consecutive_failures = 0u;
  binding->restart_attempts = 0u;
  binding->restart_pending = 0u;
  binding->next_due_tick = 0u;
  svc->backoff_ticks = 0u;
  (void)service_manager_set_state(id, SYSTEM_SERVICE_STATE_STOPPED, 0,
                                  "service stopped");
  return 0;
}

int service_manager_restart(uint32_t id) {
  struct system_service_status *svc = NULL;
  int rc = 0;

  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  svc = &g_services[id];
  if (svc->state != SYSTEM_SERVICE_STATE_STOPPED) {
    rc = service_manager_stop(id);
    if (rc < 0) {
      return rc;
    }
  }
  rc = service_manager_start(id);
  if (rc == 0) {
    svc->restarts++;
  }
  return rc;
}

int service_manager_target_current(struct system_service_target_status *out) {
  service_manager_init();
  if (!out || g_current_target >= SYSTEM_SERVICE_TARGET_COUNT) {
    return -1;
  }
  *out = g_targets[g_current_target];
  return 0;
}

int service_manager_target_get_at(size_t index,
                                  struct system_service_target_status *out) {
  service_manager_init();
  if (!out || index >= SYSTEM_SERVICE_TARGET_COUNT) {
    return -1;
  }
  *out = g_targets[index];
  return 0;
}

int service_manager_target_find(const char *name,
                                struct system_service_target_status *out) {
  service_manager_init();
  if (!name || !name[0]) {
    return -1;
  }
  for (size_t i = 0; i < SYSTEM_SERVICE_TARGET_COUNT; ++i) {
    if (!local_equal(name, g_targets[i].name)) {
      continue;
    }
    if (out) {
      *out = g_targets[i];
    }
    return (int)g_targets[i].id;
  }
  return -1;
}

int service_manager_target_apply(uint32_t id) {
  uint32_t mask = 0u;
  int rc = 0;
  int last_error = 0;

  service_manager_init();
  if (id >= SYSTEM_SERVICE_TARGET_COUNT) {
    return -1;
  }

  g_current_target = id;
  mask = g_targets[id].service_mask;
  for (uint32_t svc_id = 0; svc_id < SYSTEM_SERVICE_COUNT; ++svc_id) {
    struct system_service_status *svc = &g_services[svc_id];

    if ((mask & (1u << svc_id)) != 0u) {
      if (svc->state == SYSTEM_SERVICE_STATE_STOPPED ||
          svc->state == SYSTEM_SERVICE_STATE_UNKNOWN) {
        rc = service_manager_start(svc_id);
        if (rc < 0 && rc != -2 && rc != -3) {
          last_error = rc;
        }
      }
      continue;
    }
    if (svc->state == SYSTEM_SERVICE_STATE_BLOCKED ||
        svc->state == SYSTEM_SERVICE_STATE_STOPPED) {
      continue;
    }
    rc = service_manager_stop(svc_id);
    if (rc == 0) {
      (void)service_manager_set_state(svc_id, SYSTEM_SERVICE_STATE_STOPPED, 0,
                                      "excluded from current target");
    } else if (rc != -2) {
      last_error = rc;
    }
  }
  return last_error;
}

const char *service_manager_state_label(uint8_t state) {
  switch (state) {
  case SYSTEM_SERVICE_STATE_STARTING:
    return "starting";
  case SYSTEM_SERVICE_STATE_READY:
    return "ready";
  case SYSTEM_SERVICE_STATE_DEGRADED:
    return "degraded";
  case SYSTEM_SERVICE_STATE_BLOCKED:
    return "blocked";
  case SYSTEM_SERVICE_STATE_STOPPED:
    return "stopped";
  default:
    return "unknown";
  }
}

const char *service_manager_startup_label(uint8_t startup) {
  switch (startup) {
  case SYSTEM_SERVICE_STARTUP_BOOT:
    return "boot";
  case SYSTEM_SERVICE_STARTUP_MANUAL:
    return "manual";
  case SYSTEM_SERVICE_STARTUP_BLOCKED:
    return "blocked";
  default:
    return "unknown";
  }
}

const char *service_manager_target_label(uint32_t id) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_TARGET_COUNT) {
    return "unknown";
  }
  return g_targets[id].name[0] ? g_targets[id].name : "unknown";
}
