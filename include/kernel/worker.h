#ifndef KERNEL_WORKER_H
#define KERNEL_WORKER_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/spinlock.h"

#define WORKER_POOL_NAME_MAX 24
#define WORKER_QUEUE_MAX 64
#define WORKER_POOL_MAX 4

typedef void (*worker_fn)(void *arg);

enum worker_item_state {
  WORKER_ITEM_FREE = 0,
  WORKER_ITEM_QUEUED,
  WORKER_ITEM_RUNNING,
  WORKER_ITEM_DONE
};

struct worker_item {
  worker_fn func;
  void *arg;
  enum worker_item_state state;
  int32_t result;
};

struct worker_pool {
  char name[WORKER_POOL_NAME_MAX];
  uint32_t id;
  uint32_t thread_count;
  uint32_t active_threads;
  struct worker_item queue[WORKER_QUEUE_MAX];
  uint32_t head;
  uint32_t tail;
  uint32_t count;
  struct spinlock lock;
  uint64_t jobs_completed;
  uint64_t jobs_failed;
  int running;
};

struct worker_pool_stats {
  char name[WORKER_POOL_NAME_MAX];
  uint32_t thread_count;
  uint32_t active_threads;
  uint32_t queued;
  uint64_t completed;
  uint64_t failed;
};

void worker_system_init(void);
int worker_pool_create(const char *name, uint32_t thread_count);
int worker_pool_submit(uint32_t pool_id, worker_fn func, void *arg);
int worker_pool_stats_get(uint32_t pool_id, struct worker_pool_stats *out);
size_t worker_pool_count(void);
void worker_pool_drain(uint32_t pool_id);
void worker_pool_shutdown(uint32_t pool_id);

#endif /* KERNEL_WORKER_H */
