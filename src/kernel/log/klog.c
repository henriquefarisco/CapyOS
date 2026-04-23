#include "kernel/log/klog.h"

#include <stddef.h>
#include <stdint.h>

static char g_klog_ring[KLOG_RING_LINES][KLOG_LINE_MAX];
static uint32_t g_klog_head = 0;
static uint32_t g_klog_count = 0;
static uint32_t g_klog_total = 0;
static uint32_t g_klog_flushed_total = 0;
static char g_klog_serial_buf[KLOG_RING_BYTES + KLOG_LINE_MAX + 1];

extern uint64_t pit_ticks(void) __attribute__((weak));

static uint64_t klog_tick(void) {
  if (pit_ticks) {
    return pit_ticks();
  }
  return 0;
}

static void klog_append_str(char *buf, size_t cap, size_t *pos, const char *s) {
  if (!buf || !s || !pos) {
    return;
  }
  while (*s && *pos + 1 < cap) {
    buf[(*pos)++] = *s++;
  }
  buf[*pos] = '\0';
}

static void klog_append_hex(char *buf, size_t cap, size_t *pos, uint64_t val) {
  static const char hex[] = "0123456789ABCDEF";
  char tmp[17];
  int started = 0;
  int idx = 0;

  if (val == 0) {
    if (*pos + 1 < cap) {
      buf[(*pos)++] = '0';
    }
    buf[*pos] = '\0';
    return;
  }

  for (int i = 60; i >= 0; i -= 4) {
    char c = hex[(val >> i) & 0xF];
    if (c != '0') {
      started = 1;
    }
    if (started) {
      tmp[idx++] = c;
    }
  }
  tmp[idx] = '\0';
  klog_append_str(buf, cap, pos, tmp);
}

static void klog_append_dec(char *buf, size_t cap, size_t *pos, uint32_t val) {
  char tmp[12];
  int idx = 0;

  if (val == 0) {
    if (*pos + 1 < cap) {
      buf[(*pos)++] = '0';
    }
    buf[*pos] = '\0';
    return;
  }

  while (val > 0 && idx < 10) {
    tmp[idx++] = (char)('0' + (val % 10));
    val /= 10;
  }
  for (int i = idx - 1; i >= 0 && *pos + 1 < cap; --i) {
    buf[(*pos)++] = tmp[i];
  }
  buf[*pos] = '\0';
}

static const char *klog_level_tag(int level) {
  switch (level) {
  case KLOG_DEBUG:
    return "DBG";
  case KLOG_INFO:
    return "INF";
  case KLOG_WARN:
    return "WRN";
  case KLOG_ERROR:
    return "ERR";
  default:
    return "???";
  }
}

static void klog_begin_entry(char *slot, size_t *pos, int level) {
  klog_append_str(slot, KLOG_LINE_MAX, pos, "[");
  klog_append_dec(slot, KLOG_LINE_MAX, pos, (uint32_t)klog_tick());
  klog_append_str(slot, KLOG_LINE_MAX, pos, "] [");
  klog_append_str(slot, KLOG_LINE_MAX, pos, klog_level_tag(level));
  klog_append_str(slot, KLOG_LINE_MAX, pos, "] ");
}

static void klog_commit_entry(void) {
  g_klog_head = (g_klog_head + 1) % KLOG_RING_LINES;
  if (g_klog_count < KLOG_RING_LINES) {
    g_klog_count++;
  }
  g_klog_total++;
}

static uint32_t klog_oldest_total(void) {
  if (g_klog_total > g_klog_count) {
    return g_klog_total - g_klog_count;
  }
  return 0;
}

static void klog_serialize_range(uint32_t start_total, uint32_t end_total,
                                 size_t *pos) {
  for (uint32_t total = start_total; total < end_total; ++total) {
    uint32_t idx = total % KLOG_RING_LINES;
    klog_append_str(g_klog_serial_buf, sizeof(g_klog_serial_buf), pos,
                    g_klog_ring[idx]);
    klog_append_str(g_klog_serial_buf, sizeof(g_klog_serial_buf), pos, "\n");
  }
}

void klog(int level, const char *msg) {
  char *slot = g_klog_ring[g_klog_head];
  size_t pos = 0;

  klog_begin_entry(slot, &pos, level);
  if (msg) {
    klog_append_str(slot, KLOG_LINE_MAX, &pos, msg);
  }
  klog_commit_entry();
}

void klog_hex(int level, const char *prefix, uint64_t value) {
  char *slot = g_klog_ring[g_klog_head];
  size_t pos = 0;

  klog_begin_entry(slot, &pos, level);
  if (prefix) {
    klog_append_str(slot, KLOG_LINE_MAX, &pos, prefix);
  }
  klog_append_str(slot, KLOG_LINE_MAX, &pos, "0x");
  klog_append_hex(slot, KLOG_LINE_MAX, &pos, value);
  klog_commit_entry();
}

void klog_dec(int level, const char *prefix, uint32_t value) {
  char *slot = g_klog_ring[g_klog_head];
  size_t pos = 0;

  klog_begin_entry(slot, &pos, level);
  if (prefix) {
    klog_append_str(slot, KLOG_LINE_MAX, &pos, prefix);
  }
  klog_append_dec(slot, KLOG_LINE_MAX, &pos, value);
  klog_commit_entry();
}

void klog_dump(void (*print)(const char *s)) {
  if (!print || g_klog_count == 0) {
    return;
  }

  for (uint32_t total = klog_oldest_total(); total < g_klog_total; ++total) {
    uint32_t idx = total % KLOG_RING_LINES;
    print(g_klog_ring[idx]);
    print("\n");
  }
}

const char *klog_serialize(void) {
  size_t pos = 0;

  g_klog_serial_buf[0] = '\0';
  if (g_klog_count == 0) {
    return g_klog_serial_buf;
  }

  klog_serialize_range(klog_oldest_total(), g_klog_total, &pos);
  return g_klog_serial_buf;
}

int klog_flush(int (*write_fn)(const char *path, const char *text)) {
  uint32_t start_total = 0;
  uint32_t oldest_total = 0;
  size_t pos = 0;
  int rc = 0;

  if (!write_fn) {
    return -1;
  }
  if (g_klog_total == 0) {
    return 0;
  }

  g_klog_serial_buf[0] = '\0';
  start_total = g_klog_flushed_total;
  oldest_total = klog_oldest_total();

  if (start_total < oldest_total) {
    klog_append_str(g_klog_serial_buf, sizeof(g_klog_serial_buf), &pos,
                    "[0] [WRN] klog lost ");
    klog_append_dec(g_klog_serial_buf, sizeof(g_klog_serial_buf), &pos,
                    oldest_total - start_total);
    klog_append_str(g_klog_serial_buf, sizeof(g_klog_serial_buf), &pos,
                    " entries before flush\n");
    start_total = oldest_total;
  }

  if (start_total < g_klog_total) {
    klog_serialize_range(start_total, g_klog_total, &pos);
  }

  if (g_klog_serial_buf[0] == '\0') {
    return 0;
  }

  rc = write_fn("/var/log/capyos_klog.txt", g_klog_serial_buf);
  if (rc == 0) {
    g_klog_flushed_total = g_klog_total;
  }
  return rc;
}

uint32_t klog_count(void) { return g_klog_count; }

void klog_reset(void) {
  g_klog_head = 0;
  g_klog_count = 0;
  g_klog_total = 0;
  g_klog_flushed_total = 0;
  g_klog_serial_buf[0] = '\0';
  for (uint32_t i = 0; i < KLOG_RING_LINES; ++i) {
    g_klog_ring[i][0] = '\0';
  }
}
