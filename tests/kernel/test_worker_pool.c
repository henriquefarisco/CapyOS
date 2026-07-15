#include <stdio.h>
#include <string.h>

#include "kernel/task.h"
#include "kernel/worker.h"

static int failures;
static int create_fail;
static int created_count;
static int killed_count;
static int blocked_count;
static int wake_count;
static int yield_count;
static int shutdown_on_yield;
static int job_count;
static task_entry_fn captured_entry;
static void *captured_arg;
static struct task tasks[WORKER_POOL_THREAD_MAX + 1u];
static struct task *current_task;

#define CHECK(cond, label)                                                    \
  do {                                                                        \
    if (cond) printf("  ok   %s\n", label);                                  \
    else { printf("  FAIL %s\n", label); failures++; }                       \
  } while (0)

void spin_lock(struct spinlock *lock) { lock->locked = 1u; }
void spin_unlock(struct spinlock *lock) { lock->locked = 0u; }
void spin_lock_irqsave(struct spinlock *lock, uint64_t *flags) {
  *flags = 0u;
  spin_lock(lock);
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
  (void)flags;
  spin_unlock(lock);
}
int spin_trylock(struct spinlock *lock) {
  if (lock->locked) return -1;
  lock->locked = 1u;
  return 0;
}

struct task *task_create_kernel(const char *name, task_entry_fn entry,
                                void *arg) {
  struct task *task;
  (void)name;
  if (create_fail || created_count >= (int)WORKER_POOL_THREAD_MAX) return NULL;
  task = &tasks[created_count++];
  memset(task, 0, sizeof(*task));
  task->pid = (uint32_t)created_count;
  task->state = TASK_STATE_READY;
  task->active_session = (void *)(uintptr_t)1u;
  captured_entry = entry;
  captured_arg = arg;
  return task;
}
void scheduler_add(struct task *task) { (void)task; }
int task_kill(uint32_t pid) {
  (void)pid;
  killed_count++;
  return 0;
}
struct task *task_current(void) { return current_task; }
void task_block(struct task *task, void *channel) {
  blocked_count++;
  task->state = TASK_STATE_BLOCKED;
  task->wait_channel = channel;
}
void task_unblock_channel(void *channel) {
  wake_count++;
  if (current_task && current_task->state == TASK_STATE_BLOCKED &&
      current_task->wait_channel == channel) {
    current_task->state = TASK_STATE_READY;
    current_task->wait_channel = NULL;
  }
}
void task_yield(void) {
  yield_count++;
  if (shutdown_on_yield) {
    shutdown_on_yield = 0;
    worker_pool_shutdown(0u);
  }
}

static void reset_fakes(void) {
  memset(tasks, 0, sizeof(tasks));
  create_fail = 0;
  created_count = 0;
  killed_count = 0;
  blocked_count = 0;
  wake_count = 0;
  yield_count = 0;
  shutdown_on_yield = 0;
  job_count = 0;
  captured_entry = NULL;
  captured_arg = NULL;
  current_task = NULL;
  worker_system_init();
}

static void noop_job(void *arg) { (void)arg; }

static void shutdown_job(void *arg) {
  uint32_t pool_id = (uint32_t)(uintptr_t)arg;
  job_count++;
  worker_pool_shutdown(pool_id);
}

int main(void) {
  struct worker_pool_stats stats;
  int pool;
  int i;
  printf("[test_worker_pool]\n");

  reset_fakes();
  create_fail = 1;
  CHECK(worker_pool_create("fail", 1u) == -1 && worker_pool_count() == 0u,
        "create fails closed when no task exists");

  reset_fakes();
  pool = worker_pool_create("bounded", 99u);
  CHECK(pool == 0 && created_count == (int)WORKER_POOL_THREAD_MAX,
        "thread count is capped");
  CHECK(tasks[0].active_session == NULL,
        "persistent workers start without an inherited principal");

  reset_fakes();
  pool = worker_pool_create("idle", 1u);
  current_task = &tasks[0];
  shutdown_on_yield = 1;
  captured_entry(captured_arg);
  CHECK(pool == 0 && blocked_count == 1 && yield_count == 1,
        "idle worker blocks instead of busy-yielding");
  CHECK(wake_count >= 1, "shutdown wakes a blocked worker");

  reset_fakes();
  pool = worker_pool_create("run", 1u);
  current_task = &tasks[0];
  CHECK(worker_pool_submit((uint32_t)pool, shutdown_job,
                           (void *)(uintptr_t)pool) == 0,
        "submit accepts one job");
  captured_entry(captured_arg);
  memset(&stats, 0, sizeof(stats));
  CHECK(job_count == 1 && worker_pool_stats_get((uint32_t)pool, &stats) == 0 &&
            stats.completed == 1u && stats.queued == 0u &&
            stats.active_threads == 0u,
        "worker completes and publishes consistent stats");

  reset_fakes();
  pool = worker_pool_create("full", 1u);
  for (i = 0; i < WORKER_QUEUE_MAX; ++i) {
    if (worker_pool_submit((uint32_t)pool, noop_job, NULL) != 0) failures++;
  }
  CHECK(worker_pool_submit((uint32_t)pool, noop_job, NULL) == -1,
        "queue rejects overflow");

  if (failures) {
    printf("[test_worker_pool] %d failure(s)\n", failures);
    return 1;
  }
  printf("[test_worker_pool] all passed\n");
  return 0;
}
