#include "services/capyai_system_actions.h"

#include "core/work_queue.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"

#define CAPYAI_APP_ID_MAX 24u
#define CAPYAI_POWER_TICKS_PER_SECOND 100u
#define CAPYAI_POWER_MAX_DELAY_TICKS (7u * 24u * 60u * 60u * CAPYAI_POWER_TICKS_PER_SECOND)

static const char *const g_allowed_app_ids[CAPYAI_SYSTEM_APP_COUNT] = {
    "calculator", "files", "editor", "tasks", "settings", "capyai",
    "browser", "terminal"};

struct capyai_system_action_runtime {
  struct spinlock lock;
  int initialized;
  int bindings_ready;
  struct capyai_system_app_binding bindings[CAPYAI_SYSTEM_APP_COUNT];
  uint64_t next_ticket;
  struct capyai_system_action_snapshot request;
};

struct capyai_power_runtime {
  struct spinlock lock;
  int initialized;
  capyai_power_reboot_fn reboot;
  void *reboot_ctx;
  struct capyai_power_schedule_snapshot status;
};

static struct capyai_system_action_runtime g_apps = {
    .lock = SPINLOCK_INIT,
};
static struct capyai_power_runtime g_power = {
    .lock = SPINLOCK_INIT,
};

static int local_equal(const char *a, const char *b) {
  size_t i = 0u;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    ++i;
  }
  return a[i] == b[i];
}

static void local_copy(char *dst, size_t capacity, const char *src) {
  size_t i = 0u;
  if (!dst || capacity == 0u) return;
  while (src && src[i] && i + 1u < capacity) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static void local_zero(void *ptr, size_t size) {
  unsigned char *bytes = (unsigned char *)ptr;
  while (ptr && size--) *bytes++ = 0u;
}

static void apps_init(void) {
  uint64_t flags;
  spin_lock_irqsave(&g_apps.lock, &flags);
  if (!g_apps.initialized) {
    g_apps.next_ticket = 1u;
    g_apps.request.state = CAPYAI_SYSTEM_REQUEST_IDLE;
    g_apps.initialized = 1;
  }
  spin_unlock_irqrestore(&g_apps.lock, flags);
}

static void power_init(void) {
  uint64_t flags;
  spin_lock_irqsave(&g_power.lock, &flags);
  if (!g_power.initialized) {
    g_power.status.state = CAPYAI_POWER_SCHEDULE_IDLE;
    g_power.initialized = 1;
  }
  spin_unlock_irqrestore(&g_power.lock, flags);
}

int capyai_system_app_id_valid(const char *app_id) {
  size_t i;
  for (i = 0u; i < CAPYAI_SYSTEM_APP_COUNT; ++i) {
    if (local_equal(app_id, g_allowed_app_ids[i])) return 1;
  }
  return 0;
}

static int app_index(const char *app_id) {
  size_t i;
  for (i = 0u; i < CAPYAI_SYSTEM_APP_COUNT; ++i) {
    if (local_equal(app_id, g_allowed_app_ids[i])) return (int)i;
  }
  return -1;
}

int capyai_system_apps_install(
    const struct capyai_system_app_binding *bindings, size_t count) {
  struct capyai_system_app_binding ordered[CAPYAI_SYSTEM_APP_COUNT];
  uint32_t seen = 0u;
  uint64_t flags;
  size_t i;
  apps_init();
  if (!bindings || count != CAPYAI_SYSTEM_APP_COUNT) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  local_zero(ordered, sizeof(ordered));
  for (i = 0u; i < count; ++i) {
    int index = app_index(bindings[i].app_id);
    uint32_t bit;
    if (index < 0 || !bindings[i].open || !bindings[i].close) {
      return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
    }
    bit = (uint32_t)1u << (uint32_t)index;
    if (seen & bit) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
    seen |= bit;
    ordered[index] = bindings[i];
  }
  if (seen != (((uint32_t)1u << CAPYAI_SYSTEM_APP_COUNT) - 1u)) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  spin_lock_irqsave(&g_apps.lock, &flags);
  for (i = 0u; i < CAPYAI_SYSTEM_APP_COUNT; ++i) g_apps.bindings[i] = ordered[i];
  g_apps.bindings_ready = 1;
  spin_unlock_irqrestore(&g_apps.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_system_app_submit(uint32_t action, const char *app_id,
                             uint64_t *out_ticket) {
  uint64_t flags;
  uint64_t ticket;
  int index;
  apps_init();
  if (!out_ticket ||
      (action != CAPYAI_SYSTEM_APP_OPEN &&
       action != CAPYAI_SYSTEM_APP_CLOSE)) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  index = app_index(app_id);
  if (index < 0) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  spin_lock_irqsave(&g_apps.lock, &flags);
  if (!g_apps.bindings_ready || !g_apps.bindings[index].open ||
      !g_apps.bindings[index].close) {
    spin_unlock_irqrestore(&g_apps.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;
  }
  if (g_apps.request.state == CAPYAI_SYSTEM_REQUEST_QUEUED ||
      g_apps.request.state == CAPYAI_SYSTEM_REQUEST_RUNNING) {
    spin_unlock_irqrestore(&g_apps.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_BUSY;
  }
  ticket = g_apps.next_ticket++;
  if (ticket == 0u) ticket = g_apps.next_ticket++;
  local_zero(&g_apps.request, sizeof(g_apps.request));
  g_apps.request.ticket = ticket;
  g_apps.request.action = action;
  g_apps.request.state = CAPYAI_SYSTEM_REQUEST_QUEUED;
  g_apps.request.result = CAPYAI_SYSTEM_ACTION_ERR_BUSY;
  local_copy(g_apps.request.app_id, sizeof(g_apps.request.app_id), app_id);
  *out_ticket = ticket;
  spin_unlock_irqrestore(&g_apps.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_system_action_snapshot(struct capyai_system_action_snapshot *out) {
  uint64_t flags;
  apps_init();
  if (!out) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  spin_lock_irqsave(&g_apps.lock, &flags);
  *out = g_apps.request;
  spin_unlock_irqrestore(&g_apps.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_system_app_wait(uint64_t ticket, uint32_t timeout_ticks) {
  uint32_t waited = 0u;
  if (ticket == 0u) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  for (;;) {
    struct capyai_system_action_snapshot snapshot;
    if (capyai_system_action_snapshot(&snapshot) != 0) {
      return CAPYAI_SYSTEM_ACTION_ERR_FAILED;
    }
    if (snapshot.ticket != ticket) return CAPYAI_SYSTEM_ACTION_ERR_STALE;
    if (snapshot.state == CAPYAI_SYSTEM_REQUEST_COMPLETED) {
      return snapshot.result;
    }
    if (waited >= timeout_ticks) {
      uint64_t flags;
      /* If foreground execution has not started, revoke the queued request so
       * a reported timeout can never mutate the UI later. A RUNNING callback
       * is already committed and is deliberately left to finish. */
      spin_lock_irqsave(&g_apps.lock, &flags);
      if (g_apps.request.ticket == ticket &&
          g_apps.request.state == CAPYAI_SYSTEM_REQUEST_QUEUED) {
        g_apps.request.result = CAPYAI_SYSTEM_ACTION_ERR_TIMEOUT;
        g_apps.request.state = CAPYAI_SYSTEM_REQUEST_COMPLETED;
      }
      spin_unlock_irqrestore(&g_apps.lock, flags);
      return CAPYAI_SYSTEM_ACTION_ERR_TIMEOUT;
    }
    ++waited;
    task_sleep(1u);
  }
}

int capyai_system_app_request(uint32_t action, const char *app_id,
                              uint32_t timeout_ticks) {
  uint64_t ticket = 0u;
  int rc = capyai_system_app_submit(action, app_id, &ticket);
  if (rc != CAPYAI_SYSTEM_ACTION_OK) return rc;
  return capyai_system_app_wait(ticket, timeout_ticks);
}

int capyai_system_app_execute_foreground(uint32_t action,
                                         const char *app_id) {
  struct capyai_system_app_binding binding;
  capyai_system_app_fn callback;
  uint64_t flags;
  int index;
  apps_init();
  if (action != CAPYAI_SYSTEM_APP_OPEN &&
      action != CAPYAI_SYSTEM_APP_CLOSE) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  index = app_index(app_id);
  if (index < 0) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  spin_lock_irqsave(&g_apps.lock, &flags);
  if (!g_apps.bindings_ready) {
    spin_unlock_irqrestore(&g_apps.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;
  }
  binding = g_apps.bindings[index];
  callback = action == CAPYAI_SYSTEM_APP_OPEN ? binding.open : binding.close;
  spin_unlock_irqrestore(&g_apps.lock, flags);
  if (!callback) return CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;
  return callback(binding.ctx) == 0 ? CAPYAI_SYSTEM_ACTION_OK
                                    : CAPYAI_SYSTEM_ACTION_ERR_FAILED;
}

int capyai_system_actions_pump(void) {
  struct capyai_system_app_binding binding;
  capyai_system_app_fn callback;
  uint64_t ticket;
  uint64_t flags;
  int index;
  int rc;
  apps_init();
  spin_lock_irqsave(&g_apps.lock, &flags);
  if (g_apps.request.state != CAPYAI_SYSTEM_REQUEST_QUEUED) {
    spin_unlock_irqrestore(&g_apps.lock, flags);
    return 0;
  }
  index = app_index(g_apps.request.app_id);
  if (index < 0 || !g_apps.bindings_ready) {
    g_apps.request.result = CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;
    g_apps.request.state = CAPYAI_SYSTEM_REQUEST_COMPLETED;
    spin_unlock_irqrestore(&g_apps.lock, flags);
    return 1;
  }
  binding = g_apps.bindings[index];
  callback = g_apps.request.action == CAPYAI_SYSTEM_APP_OPEN
                 ? binding.open
                 : binding.close;
  ticket = g_apps.request.ticket;
  g_apps.request.state = CAPYAI_SYSTEM_REQUEST_RUNNING;
  spin_unlock_irqrestore(&g_apps.lock, flags);

  rc = callback ? callback(binding.ctx) : CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;

  spin_lock_irqsave(&g_apps.lock, &flags);
  if (g_apps.request.ticket == ticket &&
      g_apps.request.state == CAPYAI_SYSTEM_REQUEST_RUNNING) {
    g_apps.request.result = rc == 0 ? CAPYAI_SYSTEM_ACTION_OK
                                   : CAPYAI_SYSTEM_ACTION_ERR_FAILED;
    g_apps.request.state = CAPYAI_SYSTEM_REQUEST_COMPLETED;
  }
  spin_unlock_irqrestore(&g_apps.lock, flags);
  return 1;
}

void capyai_system_actions_reset_for_tests(void) {
  local_zero(&g_apps, sizeof(g_apps));
  local_zero(&g_power, sizeof(g_power));
}

static int power_transition_work(void *ctx) {
  capyai_power_reboot_fn reboot;
  void *reboot_ctx;
  struct system_work_status work;
  uint64_t flags;
  int rc;
  (void)ctx;
  power_init();
  spin_lock_irqsave(&g_power.lock, &flags);
  if (g_power.status.state != CAPYAI_POWER_SCHEDULE_SCHEDULED ||
      !g_power.reboot ||
      work_queue_get(SYSTEM_WORK_POWER_TRANSITION, &work) != 0 ||
      work.state != SYSTEM_WORK_STATE_RUNNING) {
    spin_unlock_irqrestore(&g_power.lock, flags);
    return -1;
  }
  g_power.status.state = CAPYAI_POWER_SCHEDULE_TRIGGERED;
  reboot = g_power.reboot;
  reboot_ctx = g_power.reboot_ctx;
  spin_unlock_irqrestore(&g_power.lock, flags);

  rc = reboot(reboot_ctx);

  spin_lock_irqsave(&g_power.lock, &flags);
  g_power.status.last_result = rc;
  if (rc != 0) g_power.status.state = CAPYAI_POWER_SCHEDULE_FAILED;
  spin_unlock_irqrestore(&g_power.lock, flags);
  return rc;
}

int capyai_power_schedule_configure(capyai_power_reboot_fn reboot, void *ctx) {
  uint64_t flags;
  int rc;
  power_init();
  if (!reboot) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  spin_lock_irqsave(&g_power.lock, &flags);
  if (g_power.reboot == reboot && g_power.reboot_ctx == ctx) {
    spin_unlock_irqrestore(&g_power.lock, flags);
    return CAPYAI_SYSTEM_ACTION_OK;
  }
  g_power.reboot = reboot;
  g_power.reboot_ctx = ctx;
  rc = work_queue_register(SYSTEM_WORK_POWER_TRANSITION, "power-transition",
                           power_transition_work, NULL);
  if (rc == 0) rc = work_queue_disable(SYSTEM_WORK_POWER_TRANSITION);
  if (rc != 0) {
    g_power.reboot = NULL;
    g_power.reboot_ctx = NULL;
  }
  spin_unlock_irqrestore(&g_power.lock, flags);
  return rc == 0 ? CAPYAI_SYSTEM_ACTION_OK
                 : CAPYAI_SYSTEM_ACTION_ERR_FAILED;
}

int capyai_power_schedule_reboot_after(uint64_t now_ticks,
                                       uint32_t delay_ticks) {
  uint64_t flags;
  power_init();
  if (delay_ticks == 0u || delay_ticks > CAPYAI_POWER_MAX_DELAY_TICKS) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  spin_lock_irqsave(&g_power.lock, &flags);
  if (!g_power.reboot) {
    spin_unlock_irqrestore(&g_power.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_NOT_REGISTERED;
  }
  ++g_power.status.generation;
  if (g_power.status.generation == 0u) ++g_power.status.generation;
  g_power.status.requested_at_tick = now_ticks;
  g_power.status.due_tick = now_ticks + (uint64_t)delay_ticks;
  g_power.status.delay_ticks = delay_ticks;
  g_power.status.state = CAPYAI_POWER_SCHEDULE_SCHEDULED;
  g_power.status.last_result = 0;
  if (work_queue_schedule_after(SYSTEM_WORK_POWER_TRANSITION, now_ticks,
                                delay_ticks) != 0) {
    g_power.status.state = CAPYAI_POWER_SCHEDULE_FAILED;
    g_power.status.last_result = -1;
    spin_unlock_irqrestore(&g_power.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_FAILED;
  }
  spin_unlock_irqrestore(&g_power.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_power_schedule_cancel(void) {
  uint64_t flags;
  power_init();
  spin_lock_irqsave(&g_power.lock, &flags);
  if (g_power.status.state != CAPYAI_POWER_SCHEDULE_SCHEDULED) {
    spin_unlock_irqrestore(&g_power.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_STALE;
  }
  g_power.status.state = CAPYAI_POWER_SCHEDULE_CANCELLED;
  if (work_queue_disable(SYSTEM_WORK_POWER_TRANSITION) != 0) {
    g_power.status.state = CAPYAI_POWER_SCHEDULE_FAILED;
    g_power.status.last_result = -1;
    spin_unlock_irqrestore(&g_power.lock, flags);
    return CAPYAI_SYSTEM_ACTION_ERR_FAILED;
  }
  spin_unlock_irqrestore(&g_power.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_power_schedule_snapshot(struct capyai_power_schedule_snapshot *out) {
  uint64_t flags;
  power_init();
  if (!out) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  spin_lock_irqsave(&g_power.lock, &flags);
  *out = g_power.status;
  spin_unlock_irqrestore(&g_power.lock, flags);
  return CAPYAI_SYSTEM_ACTION_OK;
}

int capyai_power_parse_delay_ticks(const char *token, uint32_t *out_ticks) {
  uint64_t value = 0u;
  uint64_t multiplier = 60u * CAPYAI_POWER_TICKS_PER_SECOND;
  size_t i = 0u;
  char suffix;
  if (!token || !out_ticks || token[0] == '\0') {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  while (token[i] >= '0' && token[i] <= '9') {
    value = value * 10u + (uint64_t)(token[i] - '0');
    if (value > 1000000u) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
    ++i;
  }
  if (i == 0u) return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  suffix = token[i];
  if (suffix != '\0' && token[i + 1u] != '\0') {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  if (suffix == 's' || suffix == 'S') multiplier = CAPYAI_POWER_TICKS_PER_SECOND;
  else if (suffix == 'm' || suffix == 'M' || suffix == '\0')
    multiplier = 60u * CAPYAI_POWER_TICKS_PER_SECOND;
  else if (suffix == 'h' || suffix == 'H')
    multiplier = 60u * 60u * CAPYAI_POWER_TICKS_PER_SECOND;
  else if (suffix == 'd' || suffix == 'D')
    multiplier = 24u * 60u * 60u * CAPYAI_POWER_TICKS_PER_SECOND;
  else
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  value *= multiplier;
  if (value == 0u || value > CAPYAI_POWER_MAX_DELAY_TICKS) {
    return CAPYAI_SYSTEM_ACTION_ERR_INVALID;
  }
  *out_ticks = (uint32_t)value;
  return CAPYAI_SYSTEM_ACTION_OK;
}
