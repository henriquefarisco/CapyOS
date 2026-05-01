#include "kernel/stdin_buf.h"

/* M5 phase E.1: bounded SPSC ring buffer for kernel -> user stdin.
 *
 * Layout: classical fixed-size circular buffer with head (write) and
 * tail (read) indices and an explicit `count` field so empty/full
 * states are unambiguous without "waste-a-slot" encoding.
 *
 * Single-CPU assumption today: the producer is the keyboard IRQ
 * handler, the consumer is sys_read on fd 0. Interrupts are not
 * disabled around producer writes because the consumer runs in a
 * non-preemptible kernel context for the duration of its SPSC
 * critical section -- it pops AT MOST one byte per call and returns.
 *
 * SMP TODO: wrap push/pop in irqsave + spinlock once SMP lands. The
 * existing refcount-style lock pattern in pipe.c is the template. */

static char     g_buf[STDIN_BUF_SIZE];
static size_t   g_head;          /* next write index */
static size_t   g_tail;          /* next read index */
static size_t   g_count;         /* bytes available */
static uint64_t g_dropped_total; /* overflow drop counter */

void stdin_buf_init(void) {
  for (size_t i = 0; i < STDIN_BUF_SIZE; i++) g_buf[i] = 0;
  g_head = 0;
  g_tail = 0;
  g_count = 0;
  g_dropped_total = 0;
}

int stdin_buf_push(char c) {
  if (g_count >= STDIN_BUF_SIZE) {
    g_dropped_total++;
    return 0;
  }
  g_buf[g_head] = c;
  g_head = (g_head + 1u) % STDIN_BUF_SIZE;
  g_count++;
  return 1;
}

int stdin_buf_pop(char *out) {
  if (!out) return 0;
  if (g_count == 0) return 0;
  *out = g_buf[g_tail];
  g_tail = (g_tail + 1u) % STDIN_BUF_SIZE;
  g_count--;
  return 1;
}

uint64_t stdin_buf_dropped_total(void) {
  return g_dropped_total;
}

size_t stdin_buf_count(void) {
  return g_count;
}
