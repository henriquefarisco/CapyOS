#ifndef KERNEL_LINUX_COMPAT_LINUX_RLIMIT_LEGACY_H
#define KERNEL_LINUX_COMPAT_LINUX_RLIMIT_LEGACY_H

/* Linux ABI legacy resource-limit syscalls.
 *
 *   int getrlimit(int resource, struct rlimit *rlim);
 *   int setrlimit(int resource, const struct rlimit *rlim);
 *
 * Why this matters for the Firefox port:
 *   - Bash startup probes RLIMIT_NOFILE via getrlimit(2) before
 *     deciding whether to enable job-control file descriptors;
 *     -ENOSYS makes it default to a tiny ceiling.
 *   - musl `pthread_create` queries RLIMIT_STACK to size the
 *     default thread stack; if -ENOSYS, it falls back to a
 *     hard-coded 80 KiB which is too small for SpiderMonkey.
 *   - Firefox sandbox checks RLIMIT_AS (address space cap) when
 *     deciding whether to enable JIT pages.
 *
 * Modern userland prefers `prlimit64(2)` (NR 302), which we
 * already wired in `linux_process`. The legacy
 * getrlimit/setrlimit syscalls are still hit by older binaries
 * and by some libc init sequences. We delegate to the prlimit64
 * provider when present and synthesise sane defaults otherwise.
 *
 * Linux x86_64 `struct rlimit` layout:
 *   uint64_t rlim_cur;   // soft limit
 *   uint64_t rlim_max;   // hard limit
 * RLIM_INFINITY = ~0ULL = (uint64_t)-1. */

#include <stdint.h>
#include <stddef.h>

/* Linux <bits/resource.h> resource ids. */
#define LINUX_RLIMIT_CPU         0
#define LINUX_RLIMIT_FSIZE       1
#define LINUX_RLIMIT_DATA        2
#define LINUX_RLIMIT_STACK       3
#define LINUX_RLIMIT_CORE        4
#define LINUX_RLIMIT_RSS         5
#define LINUX_RLIMIT_NPROC       6
#define LINUX_RLIMIT_NOFILE      7
#define LINUX_RLIMIT_MEMLOCK     8
#define LINUX_RLIMIT_AS          9
#define LINUX_RLIMIT_LOCKS      10
#define LINUX_RLIMIT_SIGPENDING 11
#define LINUX_RLIMIT_MSGQUEUE   12
#define LINUX_RLIMIT_NICE       13
#define LINUX_RLIMIT_RTPRIO     14
#define LINUX_RLIMIT_RTTIME     15
#define LINUX_RLIMIT_NLIMITS    16

#define LINUX_RLIM_INFINITY      ((uint64_t)-1)

struct linux_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

struct linux_rlimit_legacy_ops {
    /* Optional callback. NULL = caller falls back to synthesised
     * defaults. The callback returns 0 on success, negative
     * errno otherwise. */
    int64_t (*get_limit)(int resource, struct linux_rlimit *out);
    int64_t (*set_limit)(int resource, const struct linux_rlimit *in);
};

void linux_rlimit_legacy_install_ops(const struct linux_rlimit_legacy_ops *o);
void linux_rlimit_legacy_reset_for_tests(void);

int64_t linux_getrlimit(int resource, struct linux_rlimit *rlim);
int64_t linux_setrlimit(int resource, const struct linux_rlimit *rlim);

void linux_rlimit_legacy_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_RLIMIT_LEGACY_H */
