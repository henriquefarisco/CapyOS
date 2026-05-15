#ifndef KERNEL_LINUX_COMPAT_LINUX_KILL_H
#define KERNEL_LINUX_COMPAT_LINUX_KILL_H

/* Linux ABI `kill(2)`, `tgkill(2)`, `tkill(2)` -- send a signal
 * to a process / thread group / thread.
 *
 *   int kill(pid_t pid, int sig);
 *   int tgkill(pid_t tgid, pid_t tid, int sig);
 *   int tkill(pid_t tid, int sig);
 *
 * Marco M1 runs as a single task; signal delivery infrastructure
 * does not exist yet. Strategy:
 *
 *   - sig == 0 is special: kill(0) probes whether `pid` exists
 *     and whether the caller is allowed to signal it. We answer
 *     truthfully: pid == self -> 0 (alive); pid <= 0 -> 0 (group
 *     probe always succeeds since we are in the only group);
 *     other pids -> -ESRCH.
 *
 *   - sig != 0:
 *       pid == getpid() (self-signal) -> 0 (no handler installed,
 *         so no observable effect; harmless from userland's point
 *         of view; tools like abort() that re-raise SIGABRT will
 *         observe success and continue to whatever fallback they
 *         use, e.g. _exit).
 *       pid == 0   -> 0 (signal whole pgrp; we are the only one).
 *       pid == -1  -> 0 (broadcast; no peers).
 *       pid <  -1  -> -ESRCH (no such pgrp).
 *       pid >  0   -> -ESRCH (no such process).
 *
 *   - Invalid sig (< 0 or > 64) -> -EINVAL.
 *
 * Provider injection (`linux_kill_install_ops`) lets the boot
 * init wire the real signal-delivery path when it lands. */

#include <stdint.h>

struct linux_kill_ops {
    /* Resolve "current pid" for self-signal detection. */
    int32_t (*getpid)(void);
    /* Real delivery hook (return 0 on success, -errno on
     * failure). Called only after generic validation. */
    int64_t (*deliver)(int32_t pid, int sig);
};

void linux_kill_install_ops(const struct linux_kill_ops *ops);
void linux_kill_reset_for_tests(void);

int64_t linux_kill(int32_t pid, int sig);
int64_t linux_tgkill(int32_t tgid, int32_t tid, int sig);
int64_t linux_tkill(int32_t tid, int sig);

void linux_kill_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_KILL_H */
