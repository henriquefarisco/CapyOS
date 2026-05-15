#ifndef KERNEL_STDIN_BUF_H
#define KERNEL_STDIN_BUF_H

#include <stddef.h>
#include <stdint.h>

/*
 * M5 phase E.1: kernel-side stdin ring buffer.
 *
 * Producer side: keyboard IRQ (or any other input backend that wants
 * to surface bytes to user space). Calls `stdin_buf_push(c)` once per
 * decoded character (post-layout, post-modifier).
 *
 * Consumer side: SYS_READ on fd 0. Calls `stdin_buf_pop` until one byte is
 * available, then `stdin_buf_pop_many` to drain current backlog without
 * blocking (cooperative blocking semantics).
 *
 * Design notes:
 *   - Bounded ring of `STDIN_BUF_SIZE` bytes; pushes that overflow
 *     drop the new byte (count is preserved so user input never
 *     looks "shifted"). The drop counter is exposed for diagnostics.
 *   - Single-producer / single-consumer is the only contract today.
 *     The kernel runs single-CPU and the ring is touched from IRQ
 *     context (producer) and a syscall context (consumer); the
 *     irq side is short and self-contained, so no locking is added
 *     until SMP arrives.
 *   - Bytes are pushed/popped raw -- line discipline (echo,
 *     backspace handling, line buffering) lives on the producer
 *     side (TTY) and on the user-space shell, not here.
 *
 * Boot ordering: `stdin_buf_init` must be called before either side
 * is wired up; `kernel_main` calls it next to `pipe_system_init` and
 * `process_system_init`.
 */

#define STDIN_BUF_SIZE 256u

struct stdin_buf_snapshot {
  size_t capacity;
  size_t count;
  size_t space_available;
  size_t high_watermark;
  uint64_t dropped_total;
};

void stdin_buf_init(void);

/* Producer. Returns 1 if the byte was buffered, 0 on overflow. */
int stdin_buf_push(char c);

/* Consumer. Returns 1 and writes *out if a byte was available,
 * 0 if the buffer is empty (caller should yield and retry). */
int stdin_buf_pop(char *out);

size_t stdin_buf_pop_many(char *out, size_t max);

size_t stdin_buf_discard_many(size_t max);

size_t stdin_buf_discard_all(void);

int stdin_buf_ready(void);

/* Diagnostics: how many bytes were dropped on overflow since init.
 * Strictly increasing; never wraps (uint64_t). */
uint64_t stdin_buf_dropped_total(void);

/* Diagnostics: how many bytes are currently buffered (0..SIZE). */
size_t stdin_buf_count(void);

/* Diagnostics: fixed capacity of the ring buffer. */
size_t stdin_buf_capacity(void);

/* Diagnostics: free slots currently available (0..SIZE). */
size_t stdin_buf_space_available(void);

/* Diagnostics: highest live occupancy observed since init. */
size_t stdin_buf_high_watermark(void);

int stdin_buf_snapshot(struct stdin_buf_snapshot *out);

void stdin_buf_reset_diagnostics(void);

#endif /* KERNEL_STDIN_BUF_H */
