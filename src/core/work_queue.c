#include "core/work_queue.h"

#include <stddef.h>

#if defined(UNIT_TEST)
/* Host tests cannot execute privileged interrupt instructions. The tests are
 * process-context only, so an atomic lock provides the same data-race guard. */
static volatile uint32_t g_work_test_lock;
static void work_lock(uint64_t *flags) {
  if (flags) *flags = 0u;
  while (__sync_lock_test_and_set(&g_work_test_lock, 1u)) { }
}
static void work_unlock(uint64_t flags) {
  (void)flags;
  __sync_lock_release(&g_work_test_lock);
}
#else
#include "kernel/spinlock.h"
static struct spinlock g_work_lock = SPINLOCK_INIT;
static void work_lock(uint64_t *flags) {
  spin_lock_irqsave(&g_work_lock, flags);
}
static void work_unlock(uint64_t flags) {
  spin_unlock_irqrestore(&g_work_lock, flags);
}
#endif

struct work_binding {
  system_work_fn fn;
  void *ctx;
};

static struct system_work_status g_work_items[SYSTEM_WORK_COUNT];
static struct work_binding g_work_bindings[SYSTEM_WORK_COUNT];
/* A callback executes without holding the queue lock. The generation prevents
 * its completion from overwriting a concurrent disable/reschedule/register. */
static uint64_t g_work_versions[SYSTEM_WORK_COUNT];
static int g_work_ready = 0;

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

static void work_seed(uint32_t id, const char *name) {
  if (id >= SYSTEM_WORK_COUNT) {
    return;
  }
  local_zero(&g_work_items[id], sizeof(g_work_items[id]));
  g_work_items[id].id = id;
  g_work_items[id].state = SYSTEM_WORK_STATE_DISABLED;
  g_work_items[id].next_due_tick = 0u;
  local_copy(g_work_items[id].name, sizeof(g_work_items[id].name), name);
  local_copy(g_work_items[id].summary, sizeof(g_work_items[id].summary),
             "work item not registered");
}

void work_queue_reset(void) {
  uint64_t flags;
  work_lock(&flags);
  local_zero(g_work_items, sizeof(g_work_items));
  local_zero(g_work_bindings, sizeof(g_work_bindings));
  local_zero(g_work_versions, sizeof(g_work_versions));
  g_work_ready = 0;
  work_unlock(flags);
}

void work_queue_init(void) {
  uint64_t flags;
  work_lock(&flags);
  if (!g_work_ready) {
    for (uint32_t id = 0; id < SYSTEM_WORK_COUNT; ++id) {
      work_seed(id, "unnamed");
    }
    work_seed(SYSTEM_WORK_RECOVERY_SNAPSHOT, "recovery-snapshot");
    work_seed(SYSTEM_WORK_GPU_DISCOVERY, "gpu-discovery");
    work_seed(SYSTEM_WORK_USB_BRINGUP, "usb-bringup");
    work_seed(SYSTEM_WORK_UPDATE_AGENT_WARMUP, "update-agent-warmup");
    work_seed(SYSTEM_WORK_USB_POLL, "usb-poll");
    work_seed(SYSTEM_WORK_STORAGE_HYPERV_RETRY, "storage-hyperv-retry");
    work_seed(SYSTEM_WORK_POWER_TRANSITION, "power-transition");
    g_work_ready = 1;
  }
  work_unlock(flags);
}

int work_queue_register(uint32_t id, const char *name, system_work_fn fn,
                        void *ctx) {
  uint64_t flags;
  work_queue_init();
  if (id >= SYSTEM_WORK_COUNT || !fn) {
    return -1;
  }
  work_lock(&flags);
  g_work_bindings[id].fn = fn;
  g_work_bindings[id].ctx = ctx;
  if (name && name[0]) {
    local_copy(g_work_items[id].name, sizeof(g_work_items[id].name), name);
  }
  g_work_items[id].state = SYSTEM_WORK_STATE_IDLE;
  g_work_items[id].last_result = 0;
  local_copy(g_work_items[id].summary, sizeof(g_work_items[id].summary),
             "registered");
  ++g_work_versions[id];
  work_unlock(flags);
  return 0;
}

int work_queue_set_interval(uint32_t id, uint32_t interval_ticks) {
  uint64_t flags;
  work_queue_init();
  if (id >= SYSTEM_WORK_COUNT) {
    return -1;
  }
  work_lock(&flags);
  g_work_items[id].interval_ticks = interval_ticks;
  ++g_work_versions[id];
  work_unlock(flags);
  return 0;
}

int work_queue_schedule_now(uint32_t id, uint64_t now_ticks) {
  return work_queue_schedule_after(id, now_ticks, 0u);
}

int work_queue_schedule_after(uint32_t id, uint64_t now_ticks,
                              uint32_t delay_ticks) {
  struct system_work_status *item = NULL;
  uint64_t flags;

  work_queue_init();
  if (id >= SYSTEM_WORK_COUNT) return -1;
  work_lock(&flags);
  if (!g_work_bindings[id].fn) {
    work_unlock(flags);
    return -1;
  }
  item = &g_work_items[id];
  item->state = SYSTEM_WORK_STATE_SCHEDULED;
  item->next_due_tick = now_ticks + (uint64_t)delay_ticks;
  item->last_result = 0;
  local_copy(item->summary, sizeof(item->summary),
             delay_ticks == 0u ? "scheduled now" : "scheduled");
  ++g_work_versions[id];
  work_unlock(flags);
  return 0;
}

int work_queue_disable(uint32_t id) {
  uint64_t flags;
  work_queue_init();
  if (id >= SYSTEM_WORK_COUNT) {
    return -1;
  }
  work_lock(&flags);
  g_work_items[id].state = SYSTEM_WORK_STATE_DISABLED;
  g_work_items[id].next_due_tick = 0u;
  local_copy(g_work_items[id].summary, sizeof(g_work_items[id].summary),
             "disabled");
  ++g_work_versions[id];
  work_unlock(flags);
  return 0;
}

static int work_item_due(const struct system_work_status *item,
                         const struct work_binding *binding,
                         uint64_t now_ticks) {
  return item && binding && binding->fn &&
         item->state != SYSTEM_WORK_STATE_DISABLED &&
         now_ticks >= item->next_due_tick &&
         (item->state == SYSTEM_WORK_STATE_SCHEDULED ||
          (item->state == SYSTEM_WORK_STATE_READY &&
           item->interval_ticks != 0u) ||
          (item->state == SYSTEM_WORK_STATE_FAILED &&
           item->interval_ticks != 0u) ||
          item->state == SYSTEM_WORK_STATE_IDLE);
}

int work_queue_poll_due(uint64_t now_ticks) {
  int ran = 0;

  work_queue_init();
  for (uint32_t id = 0; id < SYSTEM_WORK_COUNT; ++id) {
    struct system_work_status *item;
    struct work_binding binding;
    uint64_t flags;
    uint64_t run_version;
    int rc = 0;

    work_lock(&flags);
    item = &g_work_items[id];
    if (!work_item_due(item, &g_work_bindings[id], now_ticks)) {
      work_unlock(flags);
      continue;
    }

    binding = g_work_bindings[id];
    item->state = SYSTEM_WORK_STATE_RUNNING;
    local_copy(item->summary, sizeof(item->summary), "running");
    run_version = ++g_work_versions[id];
    work_unlock(flags);

    rc = binding.fn(binding.ctx);

    work_lock(&flags);
    item = &g_work_items[id];
    if (g_work_versions[id] == run_version &&
        item->state == SYSTEM_WORK_STATE_RUNNING) {
      item->runs++;
      item->last_result = rc;
      if (rc < 0) {
        item->failures++;
        item->state = SYSTEM_WORK_STATE_FAILED;
        local_copy(item->summary, sizeof(item->summary), "last run failed");
      } else {
        item->state = SYSTEM_WORK_STATE_READY;
        local_copy(item->summary, sizeof(item->summary), "last run completed");
      }
      if (item->interval_ticks != 0u) {
        item->next_due_tick = now_ticks + (uint64_t)item->interval_ticks;
      } else {
        item->next_due_tick = 0u;
      }
    }
    work_unlock(flags);
    ran++;
  }

  return ran;
}

int work_queue_get(uint32_t id, struct system_work_status *out) {
  uint64_t flags;
  work_queue_init();
  if (id >= SYSTEM_WORK_COUNT || !out) {
    return -1;
  }
  work_lock(&flags);
  *out = g_work_items[id];
  work_unlock(flags);
  return 0;
}

int work_queue_get_at(size_t index, struct system_work_status *out) {
  return work_queue_get((uint32_t)index, out);
}

int work_queue_find(const char *name, struct system_work_status *out) {
  uint64_t flags;
  work_queue_init();
  if (!name || !name[0]) {
    return -1;
  }
  work_lock(&flags);
  for (uint32_t id = 0; id < SYSTEM_WORK_COUNT; ++id) {
    if (!local_equal(name, g_work_items[id].name)) {
      continue;
    }
    if (out) {
      *out = g_work_items[id];
    }
    work_unlock(flags);
    return (int)id;
  }
  work_unlock(flags);
  return -1;
}

size_t work_queue_count(void) { return SYSTEM_WORK_COUNT; }

const char *work_queue_state_label(uint8_t state) {
  switch (state) {
  case SYSTEM_WORK_STATE_IDLE:
    return "idle";
  case SYSTEM_WORK_STATE_SCHEDULED:
    return "scheduled";
  case SYSTEM_WORK_STATE_RUNNING:
    return "running";
  case SYSTEM_WORK_STATE_READY:
    return "ready";
  case SYSTEM_WORK_STATE_FAILED:
    return "failed";
  default:
    return "disabled";
  }
}
