#ifndef KERNEL_LINUX_COMPAT_LINUX_TYPES_H
#define KERNEL_LINUX_COMPAT_LINUX_TYPES_H

/* Linux ABI scalar and structure types as exposed to userland under
 * Strategy A (Linux ABI compatibility).
 *
 * These layouts MUST match the x86_64 Linux glibc/musl ABI so that
 * unmodified ELF binaries built with `--target=x86_64-unknown-linux-musl`
 * can interpret kernel results without translation. They are
 * intentionally distinct from the CapyOS-native types in
 * `<kernel/process.h>` and friends.
 *
 * Naming: every Linux type carried across the kernel ABI boundary
 * is prefixed `linux_` to avoid collisions with our own kernel types.
 * Inside this header we use plain `<stdint.h>` integer types.
 *
 * Reference: linux-6.x `include/uapi/asm-generic/posix_types.h` plus
 * `arch/x86/include/uapi/asm/posix_types_64.h`. Each field below is
 * commented with its Linux source location to keep diffs reviewable.
 */

#include <stdint.h>

/* `time_t`, `clockid_t`. On Linux x86_64 both are signed 64-bit.
 * Linux source: include/uapi/asm-generic/posix_types.h:84
 *               include/uapi/linux/types.h:32 (__kernel_time64_t) */
typedef int64_t linux_time_t;
typedef int32_t linux_clockid_t;

/* `struct timespec`. The Linux ABI uses two `long` words on x86_64,
 * which are 64-bit. Total size: 16 bytes; alignment: 8 bytes.
 * Linux source: include/uapi/linux/time.h:11 */
struct linux_timespec {
  linux_time_t tv_sec;   /* Seconds since the clock's epoch.       */
  int64_t      tv_nsec;  /* Nanoseconds within the current second. */
};

/* `clockid_t` constants. Subset relevant to Firefox/SpiderMonkey:
 * `mfbt/TimeStamp_posix.cpp` uses CLOCK_MONOTONIC; netwerk/http uses
 * CLOCK_REALTIME; profiler uses CLOCK_PROCESS_CPUTIME_ID and
 * CLOCK_THREAD_CPUTIME_ID.
 * Linux source: include/uapi/linux/time.h:48-58 */
#define LINUX_CLOCK_REALTIME            0
#define LINUX_CLOCK_MONOTONIC           1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID  2
#define LINUX_CLOCK_THREAD_CPUTIME_ID   3
#define LINUX_CLOCK_MONOTONIC_RAW       4
#define LINUX_CLOCK_REALTIME_COARSE     5
#define LINUX_CLOCK_MONOTONIC_COARSE    6
#define LINUX_CLOCK_BOOTTIME            7

/* Useful nanosecond constant. Linux's vdso uses the same literal. */
#define LINUX_NSEC_PER_SEC 1000000000ULL

#endif /* KERNEL_LINUX_COMPAT_LINUX_TYPES_H */
