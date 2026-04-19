#ifndef CORE_BOOT_METRICS_H
#define CORE_BOOT_METRICS_H

#include <stdint.h>

#define BOOT_METRIC_MAX 16
#define BOOT_METRIC_NAME_MAX 32

struct boot_metric {
  char name[BOOT_METRIC_NAME_MAX];
  uint64_t start_tick;
  uint64_t end_tick;
  uint64_t duration_us;
};

struct boot_metrics {
  struct boot_metric stages[BOOT_METRIC_MAX];
  uint32_t count;
  uint64_t boot_start_tick;
  uint64_t login_ready_tick;
  uint64_t total_boot_us;
};

void boot_metrics_init(void);
void boot_metrics_stage_begin(const char *name);
void boot_metrics_stage_end(void);
void boot_metrics_mark_login_ready(void);
void boot_metrics_get(struct boot_metrics *out);
void boot_metrics_print(void (*print)(const char *));

#endif /* CORE_BOOT_METRICS_H */
