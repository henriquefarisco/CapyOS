#include "boot/boot_metrics.h"
#include <stddef.h>

#pragma GCC optimize("O0")

static struct boot_metrics g_metrics;

static void bm_strcpy(char *dst, const char *src, size_t max) {
  size_t i = 0;
  while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static uint64_t bm_ticks(void) {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

__attribute__((optimize("O0"))) void boot_metrics_init(void) {
  for (int i = 0; i < BOOT_METRIC_MAX; i++) {
    g_metrics.stages[i].name[0] = '\0';
    g_metrics.stages[i].start_tick = 0;
    g_metrics.stages[i].end_tick = 0;
    g_metrics.stages[i].duration_us = 0;
  }
  g_metrics.count = 0;
  g_metrics.boot_start_tick = bm_ticks();
  g_metrics.login_ready_tick = 0;
  g_metrics.total_boot_us = 0;
}

void boot_metrics_stage_begin(const char *name) {
  if (g_metrics.count >= BOOT_METRIC_MAX || !name) return;
  struct boot_metric *m = &g_metrics.stages[g_metrics.count];
  bm_strcpy(m->name, name, BOOT_METRIC_NAME_MAX);
  m->start_tick = bm_ticks();
  m->end_tick = 0;
  m->duration_us = 0;
}

void boot_metrics_stage_end(void) {
  if (g_metrics.count >= BOOT_METRIC_MAX) return;
  struct boot_metric *m = &g_metrics.stages[g_metrics.count];
  if (m->start_tick == 0) return;
  m->end_tick = bm_ticks();
  uint64_t delta = m->end_tick - m->start_tick;
  /* Approximate: assume ~2GHz TSC as rough estimate.
   * A proper implementation would calibrate TSC frequency via APIC timer. */
  m->duration_us = delta / 2000;
  g_metrics.count++;
}

void boot_metrics_mark_login_ready(void) {
  if (g_metrics.login_ready_tick != 0) {
    return;
  }
  g_metrics.login_ready_tick = bm_ticks();
  if (g_metrics.boot_start_tick > 0) {
    uint64_t delta = g_metrics.login_ready_tick - g_metrics.boot_start_tick;
    g_metrics.total_boot_us = delta / 2000;
  }
}

void boot_metrics_get(struct boot_metrics *out) {
  if (out) *out = g_metrics;
}

static void bm_print_u64(void (*print)(const char *), uint64_t v) {
  char buf[24];
  int pos = 0;
  if (v == 0) { buf[0] = '0'; buf[1] = '\0'; print(buf); return; }
  char tmp[24]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (char)(v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[pos++] = tmp[i];
  buf[pos] = '\0';
  print(buf);
}

void boot_metrics_print(void (*print)(const char *)) {
  if (!print) return;
  print("=== Boot Metrics ===\n");
  for (uint32_t i = 0; i < g_metrics.count; i++) {
    struct boot_metric *m = &g_metrics.stages[i];
    print("  ");
    print(m->name);
    print(": ");
    bm_print_u64(print, m->duration_us);
    print(" us\n");
  }
  print("  Total boot->login: ");
  bm_print_u64(print, g_metrics.total_boot_us);
  print(" us\n");
  print("====================\n");
}
