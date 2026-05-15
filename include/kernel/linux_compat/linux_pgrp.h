#ifndef KERNEL_LINUX_COMPAT_LINUX_PGRP_H
#define KERNEL_LINUX_COMPAT_LINUX_PGRP_H

/* Linux ABI `setpgid(2)`, `getpgid(2)`, `getpgrp(2)`,
 * `setsid(2)`, `getsid(2)` -- POSIX process groups and
 * sessions.
 *
 *   int    setpgid(pid_t pid, pid_t pgid);
 *   pid_t  getpgid(pid_t pid);
 *   pid_t  getpgrp(void);
 *   pid_t  setsid(void);
 *   pid_t  getsid(pid_t pid);
 *
 * These are the primitives that shells, daemons, and
 * `start_new_session()` callers depend on. Without them, bash
 * cannot start a foreground job, daemons cannot detach from a
 * controlling terminal, and `setsid(1)` aborts on any of its
 * users.
 *
 * Marco M1 has a single task and no real session/pgrp tracking,
 * so we model:
 *   - sid = pgid = pid = 1 by default.
 *   - setpgid(0, 0) and setpgid(self, self) succeed (no-op);
 *     other forms validate args and return -EPERM (we are not
 *     a parent of anyone, so we can't move them).
 *   - setsid() succeeds and answers the new sid (= self pid).
 *
 * Provider injection lets the boot init plug in a real per-task
 * pgrp/session table when one lands. */

#include <stdint.h>

struct linux_pgrp_ops {
    int32_t (*getpid)(void);
};

void linux_pgrp_install_ops(const struct linux_pgrp_ops *ops);
void linux_pgrp_reset_for_tests(void);

int64_t linux_setpgid(int32_t pid, int32_t pgid);
int64_t linux_getpgid(int32_t pid);
int64_t linux_getpgrp(void);
int64_t linux_setsid(void);
int64_t linux_getsid(int32_t pid);

void linux_pgrp_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_PGRP_H */
