#ifndef CORE_WORK_QUEUE_H
#define CORE_WORK_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#define SYSTEM_WORK_NAME_MAX 32u
#define SYSTEM_WORK_SUMMARY_MAX 80u

enum system_work_id {
  SYSTEM_WORK_RECOVERY_SNAPSHOT = 0,
  SYSTEM_WORK_GPU_DISCOVERY,
  SYSTEM_WORK_USB_BRINGUP,
  SYSTEM_WORK_UPDATE_AGENT_WARMUP,
  SYSTEM_WORK_COUNT
};

enum system_work_state {
  SYSTEM_WORK_STATE_DISABLED = 0,
  SYSTEM_WORK_STATE_IDLE,
  SYSTEM_WORK_STATE_SCHEDULED,
  SYSTEM_WORK_STATE_RUNNING,
  SYSTEM_WORK_STATE_READY,
  SYSTEM_WORK_STATE_FAILED
};

typedef int (*system_work_fn)(void *ctx);

struct system_work_status {
  uint32_t id;
  char name[SYSTEM_WORK_NAME_MAX];
  char summary[SYSTEM_WORK_SUMMARY_MAX];
  uint8_t state;
  uint32_t runs;
  uint32_t failures;
  uint32_t interval_ticks;
  uint64_t next_due_tick;
  int32_t last_result;
};

void work_queue_init(void);
void work_queue_reset(void);
int work_queue_register(uint32_t id, const char *name, system_work_fn fn,
                        void *ctx);
int work_queue_set_interval(uint32_t id, uint32_t interval_ticks);
int work_queue_schedule_now(uint32_t id, uint64_t now_ticks);
int work_queue_schedule_after(uint32_t id, uint64_t now_ticks,
                              uint32_t delay_ticks);
int work_queue_disable(uint32_t id);
int work_queue_poll_due(uint64_t now_ticks);
int work_queue_get(uint32_t id, struct system_work_status *out);
int work_queue_get_at(size_t index, struct system_work_status *out);
int work_queue_find(const char *name, struct system_work_status *out);
size_t work_queue_count(void);
const char *work_queue_state_label(uint8_t state);

#endif /* CORE_WORK_QUEUE_H */
