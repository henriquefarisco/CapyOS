#include "kernel/worker.h"
#include "kernel/task.h"
#include <stddef.h>

static struct worker_pool pools[WORKER_POOL_MAX];
static uint32_t pool_count = 0;

static void worker_memset(void *dst, int val, size_t len) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void worker_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static void worker_thread_entry(void *arg) {
  struct worker_pool *pool = (struct worker_pool *)arg;
  for (;;) {
    uint64_t flags;
    struct worker_item item;
    spin_lock_irqsave(&pool->lock, &flags);
    if (!pool->running) {
      spin_unlock_irqrestore(&pool->lock, flags);
      break;
    }
    if (pool->count == 0) {
      /* Mark the task blocked while holding the same lock used by submit.
       * submit cannot miss the transition: after enqueueing it wakes every
       * task waiting on this pool address.  Keeping idle workers out of the
       * runnable set also avoids a permanent task_yield busy loop. */
      task_block(task_current(), pool);
      spin_unlock_irqrestore(&pool->lock, flags);
      task_yield();
      continue;
    }
    item = pool->queue[pool->tail];
    pool->tail = (pool->tail + 1) % WORKER_QUEUE_MAX;
    pool->count--;
    pool->active_threads++;
    spin_unlock_irqrestore(&pool->lock, flags);

    item.state = WORKER_ITEM_RUNNING;
    if (item.func) {
      item.func(item.arg);
      spin_lock_irqsave(&pool->lock, &flags);
      pool->jobs_completed++;
      spin_unlock_irqrestore(&pool->lock, flags);
    }
    item.state = WORKER_ITEM_DONE;
    spin_lock_irqsave(&pool->lock, &flags);
    if (pool->active_threads > 0u) pool->active_threads--;
    spin_unlock_irqrestore(&pool->lock, flags);
  }
}

void worker_system_init(void) {
  worker_memset(pools, 0, sizeof(pools));
  pool_count = 0;
}

int worker_pool_create(const char *name, uint32_t thread_count) {
  uint32_t created = 0u;
  if (pool_count >= WORKER_POOL_MAX) return -1;
  struct worker_pool *pool = &pools[pool_count];
  worker_memset(pool, 0, sizeof(*pool));
  worker_strcpy(pool->name, name ? name : "pool", WORKER_POOL_NAME_MAX);
  pool->id = pool_count;
  pool->thread_count = thread_count > 0 ? thread_count : 1;
  if (pool->thread_count > WORKER_POOL_THREAD_MAX) {
    pool->thread_count = WORKER_POOL_THREAD_MAX;
  }
  pool->active_threads = 0;
  pool->head = 0;
  pool->tail = 0;
  pool->count = 0;
  spinlock_init(&pool->lock);
  pool->jobs_completed = 0;
  pool->jobs_failed = 0;
  pool->running = 1;

  for (uint32_t i = 0; i < pool->thread_count; i++) {
    char tname[TASK_NAME_MAX];
    worker_strcpy(tname, name ? name : "worker", TASK_NAME_MAX - 4);
    size_t len = 0;
    while (tname[len]) len++;
    if (len < TASK_NAME_MAX - 2) {
      tname[len] = '-';
      tname[len + 1] = '0' + (char)(i % 10);
      tname[len + 2] = '\0';
    }
    struct task *t = task_create_kernel(tname, worker_thread_entry, pool);
    if (t) {
      extern void scheduler_add(struct task *t);
      pool->threads[created++] = t;
      scheduler_add(t);
    }
  }

  if (created != pool->thread_count) {
    pool->running = 0;
    for (uint32_t i = 0u; i < created; ++i) {
      if (pool->threads[i]) (void)task_kill(pool->threads[i]->pid);
    }
    worker_memset(pool, 0, sizeof(*pool));
    return -1;
  }

  return (int)pool_count++;
}

int worker_pool_submit(uint32_t pool_id, worker_fn func, void *arg) {
  if (pool_id >= pool_count || !func) return -1;
  struct worker_pool *pool = &pools[pool_id];

  uint64_t flags;
  spin_lock_irqsave(&pool->lock, &flags);
  if (pool->count >= WORKER_QUEUE_MAX) {
    spin_unlock_irqrestore(&pool->lock, flags);
    return -1;
  }

  struct worker_item *item = &pool->queue[pool->head];
  item->func = func;
  item->arg = arg;
  item->state = WORKER_ITEM_QUEUED;
  item->result = 0;
  pool->head = (pool->head + 1) % WORKER_QUEUE_MAX;
  pool->count++;
  spin_unlock_irqrestore(&pool->lock, flags);
  task_unblock_channel(pool);
  return 0;
}

int worker_pool_stats_get(uint32_t pool_id, struct worker_pool_stats *out) {
  uint64_t flags;
  if (pool_id >= pool_count || !out) return -1;
  struct worker_pool *pool = &pools[pool_id];
  spin_lock_irqsave(&pool->lock, &flags);
  worker_strcpy(out->name, pool->name, WORKER_POOL_NAME_MAX);
  out->thread_count = pool->thread_count;
  out->active_threads = pool->active_threads;
  out->queued = pool->count;
  out->completed = pool->jobs_completed;
  out->failed = pool->jobs_failed;
  spin_unlock_irqrestore(&pool->lock, flags);
  return 0;
}

size_t worker_pool_count(void) {
  return pool_count;
}

void worker_pool_drain(uint32_t pool_id) {
  if (pool_id >= pool_count) return;
  struct worker_pool *pool = &pools[pool_id];
  for (;;) {
    uint32_t queued;
    uint32_t active;
    uint64_t flags;
    spin_lock_irqsave(&pool->lock, &flags);
    queued = pool->count;
    active = pool->active_threads;
    spin_unlock_irqrestore(&pool->lock, flags);
    if (queued == 0u && active == 0u) return;
    task_yield();
  }
}

void worker_pool_shutdown(uint32_t pool_id) {
  uint64_t flags;
  if (pool_id >= pool_count) return;
  spin_lock_irqsave(&pools[pool_id].lock, &flags);
  pools[pool_id].running = 0;
  spin_unlock_irqrestore(&pools[pool_id].lock, flags);
  task_unblock_channel(&pools[pool_id]);
}
