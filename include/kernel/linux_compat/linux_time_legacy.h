#ifndef KERNEL_LINUX_COMPAT_LINUX_TIME_LEGACY_H
#define KERNEL_LINUX_COMPAT_LINUX_TIME_LEGACY_H

/* Linux ABI legacy `time(2)` + `getcpu(2)` syscalls.
 *
 *   time_t time(time_t *tloc);
 *   int    getcpu(unsigned *cpu, unsigned *node);
 *
 * Why this matters for the Firefox port:
 *   - `time(NULL)` is the simplest "what time is it" call;
 *     glibc/musl typically map it onto clock_gettime(CLOCK_REALTIME)
 *     but musl falls back to the raw syscall on Linux when the
 *     vDSO is unavailable. Firefox build artifacts built with
 *     glibc statically link this path.
 *   - `sched_getcpu()` (used by Firefox profiler to label
 *     samples with originating CPU and by SpiderMonkey GC for
 *     NUMA hints) calls getcpu through the vDSO; on -ENOSYS
 *     it falls back to a CPUID-based detection that is much
 *     more expensive.
 *
 * Marco M1 is single-CPU; getcpu reports cpu = 0, node = 0
 * always. time() reads from an injectable now-callback that
 * defaults to 0 (epoch) -- userland code that needs real
 * wall-clock time should use clock_gettime(CLOCK_REALTIME)
 * which is wired with the platform clock. */

#include <stdint.h>
#include <stddef.h>

struct linux_time_legacy_ops {
    /* Returns the current wall-clock time in seconds since
     * the Unix epoch. NULL = caller falls back to 0. */
    int64_t (*now_seconds)(void);
};

void linux_time_legacy_install_ops(const struct linux_time_legacy_ops *ops);
void linux_time_legacy_reset_for_tests(void);

int64_t linux_time(int64_t *tloc);
int64_t linux_getcpu(uint32_t *cpu, uint32_t *node);

void linux_time_legacy_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_TIME_LEGACY_H */
