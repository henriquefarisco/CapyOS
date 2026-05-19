#include "kernel/linux_compat/linux_clock.h"

/* This translation unit wires `linux_clock` to the real kernel
 * timebase. It is excluded from host unit tests (which inject their
 * own deterministic cycle counter) and included in the kernel build
 * via `Makefile`'s `CAPYOS64_OBJS`.
 *
 * The split keeps `linux_clock.c` host-friendly (pure arithmetic +
 * function-pointer indirection) and isolates the freestanding-only
 * x86_64 inline asm to this file.
 */

#if !defined(UNIT_TEST)

#include "arch/x86_64/timebase.h"

#include <stdint.h>

static inline uint64_t rdtsc_local(void) {
  uint32_t lo = 0;
  uint32_t hi = 0;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static uint64_t cycles_source(void) {
  return rdtsc_local();
}

static uint64_t hz_source(void) {
  return x64_timebase_hz();
}

/* Called once from `kernel_main` after `x64_timebase_init` has run.
 * Captures the current TSC as the boot epoch so monotonic time
 * starts at zero from the perspective of every Linux-ABI caller.
 *
 * Note: handler registration in the linux_syscall dispatcher is
 * the responsibility of `linux_syscall_init()` -> it calls
 * `linux_clock_register_syscalls()` (weak symbol resolved at link
 * time). That is independent of timebase install: this function
 * only seeds the time source. */
void linux_clock_init_boot(void) {
  /* Force timebase init in case the platform brought-up sequence
   * reorders. `x64_timebase_init` is idempotent. */
  x64_timebase_init();
  linux_clock_install_timebase(cycles_source, hz_source, rdtsc_local());

  /* Wall-clock epoch is left uninstalled until an RTC reader lands
   * (S2.4 / S2.5 territory). Portable userland must tolerate
   * CLOCK_REALTIME == CLOCK_MONOTONIC for now. */
}

#endif /* !UNIT_TEST */
