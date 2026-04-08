#include "core/service_manager.h"

#include <stddef.h>

static struct system_service_status g_services[SYSTEM_SERVICE_COUNT];
struct service_poll_binding {
  system_service_poll_fn poll;
  void *ctx;
  uint32_t interval_ticks;
  uint64_t next_due_tick;
};

static struct service_poll_binding g_service_polls[SYSTEM_SERVICE_COUNT];
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

void service_manager_reset(void) {
  local_zero(g_services, sizeof(g_services));
  local_zero(g_service_polls, sizeof(g_service_polls));
  g_services_ready = 0;
}

void service_manager_bootstrap_defaults(void) {
  seed_service(SYSTEM_SERVICE_LOGGER, "logger", 1,
               SYSTEM_SERVICE_STARTUP_BOOT, SYSTEM_SERVICE_STATE_STARTING,
               "boot ring buffer only");
  seed_service(SYSTEM_SERVICE_NETWORKD, "networkd", 1,
               SYSTEM_SERVICE_STARTUP_BOOT, SYSTEM_SERVICE_STATE_STARTING,
               "network bootstrap pending");
  seed_service(SYSTEM_SERVICE_UPDATE_AGENT, "update-agent", 0,
               SYSTEM_SERVICE_STARTUP_BLOCKED, SYSTEM_SERVICE_STATE_BLOCKED,
               "signed updates not implemented");
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

int service_manager_set_poll(uint32_t id, system_service_poll_fn poll,
                             void *ctx) {
  service_manager_init();
  if (id >= SYSTEM_SERVICE_COUNT) {
    return -1;
  }
  g_service_polls[id].poll = poll;
  g_service_polls[id].ctx = ctx;
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

static int service_pollable(const struct system_service_status *svc,
                            const struct service_poll_binding *binding) {
  return svc && binding && binding->poll &&
         svc->state != SYSTEM_SERVICE_STATE_BLOCKED &&
         svc->state != SYSTEM_SERVICE_STATE_STOPPED;
}

int service_manager_poll_once(void) {
  int polled = 0;

  service_manager_init();
  for (uint32_t id = 0; id < SYSTEM_SERVICE_COUNT; ++id) {
    struct service_poll_binding *binding = &g_service_polls[id];
    struct system_service_status *svc = &g_services[id];

    if (!service_pollable(svc, binding)) {
      continue;
    }
    (void)binding->poll(binding->ctx);
    svc->polls++;
    polled++;
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
    (void)binding->poll(binding->ctx);
    svc->polls++;
    polled++;

    interval = binding->interval_ticks;
    if (interval == 0u) {
      interval = 1u;
    }
    binding->next_due_tick = now_ticks + (uint64_t)interval;
  }

  return polled;
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
